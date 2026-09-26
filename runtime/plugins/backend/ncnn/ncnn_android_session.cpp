#include "plugins/backend/ncnn/ncnn_android_session.h"
#include "plugins/backend/ncnn/ncnn_input_delivery.h"

#if defined(__ANDROID__)
#include "common/config_io.h"
#include "plugins/backend/ncnn/ncnn_dense_output.h"
#include "picosha2/picosha2.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <poll.h>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
#include <android/log.h>
#include <time.h>
#endif

namespace humanvision::runtime::ncnn_backend {
namespace {
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
int64_t TraceMs() noexcept {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}
void TraceDetectorStage(const char* stage, size_t output, int64_t begun) noexcept {
    __android_log_print(ANDROID_LOG_INFO, "HV_TOPDOWN_NCNN",
        "run_prepared stage=%s output=%zu elapsed_ms=%lld now_ms=%lld",
        stage, output, static_cast<long long>(TraceMs() - begun),
        static_cast<long long>(TraceMs()));
}
#endif
std::string HashFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open ncnn model asset: " + path.string());
    picosha2::hash256_one_by_one hash;
    std::array<char, 65536> bytes{};
    while (input) {
        input.read(bytes.data(), bytes.size());
        hash.process(bytes.begin(), bytes.begin() + input.gcount());
    }
    if (!input.eof()) throw std::runtime_error("Cannot read ncnn model asset: " + path.string());
    hash.finish();
    return picosha2::get_hash_hex_string(hash);
}

std::filesystem::path VerifiedAsset(const std::filesystem::path& root,
                                    const nlohmann::json& model,
                                    const char* path_key, const char* hash_key) {
    const auto relative = model.at(path_key).get<std::string>();
    if (relative.empty() || relative.find('\\') != std::string::npos ||
        relative.find(':') != std::string::npos)
        throw std::runtime_error(std::string("Invalid ncnn asset path: ") + path_key);
    const auto path = humanvision::runtime::ConfinedPath(root, std::filesystem::u8path(relative));
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Missing ncnn model asset: " + relative);
    const auto expected = model.at(hash_key).get<std::string>();
    if (expected.size() != 64 || HashFile(path) != expected)
        throw std::runtime_error("ncnn model SHA-256 mismatch: " + relative);
    return path;
}

bool SameUuid(const uint8_t* a, const std::array<uint8_t, 16>& b) {
    return std::equal(b.begin(), b.end(), a);
}
void RetainAhb(uintptr_t buffer) noexcept {
    AHardwareBuffer_acquire(reinterpret_cast<AHardwareBuffer*>(buffer));
}
// Pinned ncnn uses SimpleVK, whose header omits the external-semaphore-fd
// declarations even though the Vulkan loader exposes the extension. These
// structures match the Vulkan 1.1/KHR wire ABI without mixing Vulkan headers.
constexpr auto kExportSemaphoreCreateInfo = static_cast<VkStructureType>(1000077000);
constexpr auto kImportSemaphoreFdInfo = static_cast<VkStructureType>(1000079000);
constexpr VkFlags kSyncFdHandleType = 0x10;
constexpr VkFlags kTemporaryImport = 0x1;
struct ExportSemaphoreCreateInfo {
    VkStructureType sType;
    const void* pNext = nullptr;
    VkFlags handleTypes = 0;
};
struct ImportSemaphoreFdInfo {
    VkStructureType sType;
    const void* pNext = nullptr;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkFlags flags = 0;
    VkFlags handleType = 0;
    int fd = -1;
};
using ImportSemaphoreFd = VkResult (VKAPI_PTR*)(VkDevice, const ImportSemaphoreFdInfo*);
bool WaitProducerFd(gpu::SyncFd& fd) noexcept {
    if (!fd.HasPayload()) return true;
    if (fd.Get() == -1) return true;
    pollfd pfd{fd.Get(), POLLIN, 0};
    for (;;) {
        const int result = poll(&pfd, 1, -1);
        if (result > 0) return (pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0;
        if (result < 0 && errno != EINTR) return false;
    }
}
}

struct AndroidSession::Slot {
    const ncnn::VulkanDevice* device = nullptr;
    AHardwareBuffer* ahb = nullptr; // Owns one generation reference via ConsumerGeneration.
    VkSemaphore import_semaphore = VK_NULL_HANDLE;
    std::unique_ptr<ncnn::VkAndroidHardwareBufferImageAllocator> allocator;
    ncnn::VkImageMat image;
    std::unique_ptr<ncnn::ImportAndroidHardwareBufferPipeline> import_pipeline;
    std::unique_ptr<ncnn::VkCompute> compute;
    ncnn::VkMat imported_rgb, normalized, prepared_input;
    std::unique_ptr<ncnn::Extractor> extractor;
    std::unique_ptr<ncnn::Extractor> pristine_extractor;
    std::vector<ncnn::VkMat> gpu_outputs, fp32_outputs;
    std::vector<ncnn::Mat> cpu_outputs;
    std::vector<std::vector<float>> dense_outputs;
    ~Slot() {
        pristine_extractor.reset();
        extractor.reset();
        gpu_outputs.clear(); fp32_outputs.clear(); cpu_outputs.clear(); dense_outputs.clear();
        prepared_input.release(); normalized.release(); imported_rgb.release();
        compute.reset(); import_pipeline.reset(); image.release(); allocator.reset();
        if (import_semaphore && device && device->vkdevice())
            ncnn::vkDestroySemaphore(device->vkdevice(), import_semaphore, nullptr);
    }
};
void AndroidSession::SlotDeleter::operator()(Slot* slot) const noexcept { delete slot; }

AndroidSession::AndroidSession() = default;

AndroidSession::~AndroidSession() {
    const auto retain_unproved = [this]() noexcept {
        // A failed Vulkan submit/wait does not prove that the GPU stopped
        // reading the cached tensor, import images, semaphores or model
        // descriptors. Retain every dependent allocation and the ncnn GPU
        // instance until process exit. Even a caller that bypasses the V3
        // Destroy guard must never turn an uncertain fault into a UAF.
        for (auto& slot : slots_) (void)slot.release();
        (void)prepared_.release();
        (void)preprocess_.release();
        (void)net_.release();
    };
    if (prepared_state_.RequiresProcessLifetimeRetention(terminal_gpu_fault_)) {
        retain_unproved();
        return;
    }
    if (active_consumer_) {
        std::string ignored;
        FinishObservation(*active_consumer_, ignored);
        // The final release can itself fail after the first safety check.
        if (prepared_state_.RequiresProcessLifetimeRetention(terminal_gpu_fault_)) {
            retain_unproved();
            return;
        }
    }
    for (auto& slot : slots_) slot.reset();
    prepared_.reset();
    preprocess_.reset();
    net_.reset();
    for (const auto handle : generation_.retained_ahb)
        if (handle) AHardwareBuffer_release(reinterpret_cast<AHardwareBuffer*>(handle));
    if (device_) {
        if (blob_allocator_) device_->reclaim_blob_allocator(blob_allocator_);
        if (staging_allocator_) device_->reclaim_staging_allocator(staging_allocator_);
    }
    if (gpu_instance_lease_) gpu::ReleaseNcnnGpuInstance();
}

bool AndroidSession::ParseModel(const HV_GpuBackendConfigV1& config, std::string& error) {
    try {
        const auto manifest = nlohmann::json::parse(config.model_manifest_utf8);
        if (manifest.at("schema_version").get<int>() != 2 ||
            !manifest.at("models").is_array() || manifest.at("models").empty())
            throw std::runtime_error("ncnn backend requires a schema-2 ModelPack");
        const auto role = manifest.value("active_role", std::string{});
        const nlohmann::json* chosen = nullptr;
        for (const auto& model : manifest.at("models")) {
            if (role.empty() || model.at("role").get<std::string>() == role) {
                if (chosen) throw std::runtime_error("ncnn model role is ambiguous; set active_role");
                chosen = &model;
            }
        }
        if (!chosen) throw std::runtime_error("ncnn active_role is absent from ModelPack");
        detector_role_ = chosen->at("role").get<std::string>() == "detector";
        if (chosen->at("format").get<std::string>() != "ncnn")
            throw std::runtime_error("Selected model is not ncnn format");
        if (!ParseBackendOptions(*chosen, backend_options_, error)) return false;
        const auto& output = chosen->at("output_contract");
        auto input = chosen->at("input_contract");
        input["output_blobs"] = output.at("output_blobs");
        if (!ParseInputContract(input, contract_, error)) return false;
        if (chosen->at("role").get<std::string>() == "detector" &&
            (contract_.width != 320 || contract_.height != 320 ||
             contract_.output_type != HV_GPU_TENSOR_FP16 ||
             contract_.output_elempack != 1 || contract_.cast_type_to != 2 ||
             contract_.input_blob != "in0" ||
             contract_.output_blobs != std::vector<std::string>{"cls", "bbox"})) {
            throw std::runtime_error("RTMDet detector requires RGB 320x320 FP16 pack1 with cls/bbox");
        }
        if (chosen->at("role").get<std::string>() == "body" &&
            (contract_.width != 192 || contract_.height != 256 ||
             contract_.output_type != HV_GPU_TENSOR_FP16 ||
             contract_.output_elempack != 4 || contract_.cast_type_to != 2 ||
             contract_.input_blob != "in0" ||
             contract_.output_blobs != std::vector<std::string>{"simcc_x", "simcc_y"})) {
            throw std::runtime_error("RTMPose body requires RGB 192x256 FP16 pack4 with simcc_x/simcc_y");
        }
        const auto& limits = output.at("max_output_bytes");
        if (!limits.is_object()) throw std::runtime_error("Missing max_output_bytes object");
        output_byte_limits_.clear();
        for (const auto& name : contract_.output_blobs) {
            const uint64_t maximum = limits.at(name).get<uint64_t>();
            if (!maximum || maximum > 64ull * 1024ull * 1024ull)
                throw std::runtime_error("Invalid bounded output bytes for " + name);
            output_byte_limits_.push_back(maximum);
        }
        const auto root = std::filesystem::u8path(config.asset_root_utf8);
        param_path_ = VerifiedAsset(root, *chosen, "param_path", "param_sha256").string();
        bin_path_ = VerifiedAsset(root, *chosen, "bin_path", "bin_sha256").string();
        error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool AndroidSession::InitializeSlots(gpu::UnityVulkanBridge& bridge, std::string& error) {
    if (bridge.RetainGeneration(generation_, RetainAhb) != gpu::SlotResult::Ok) {
        error = "ncnn AHB generation is not configured"; return false;
    }
    if (generation_.contract.width == 0 || generation_.contract.height == 0) {
        error = "ncnn AHB generation has invalid geometry"; return false;
    }
    for (size_t i = 0; i < slots_.size(); ++i) {
        std::unique_ptr<Slot, SlotDeleter> slot(new Slot);
        slot->device = device_;
        slot->ahb = reinterpret_cast<AHardwareBuffer*>(generation_.retained_ahb[i]);
        slot->allocator = std::make_unique<ncnn::VkAndroidHardwareBufferImageAllocator>(device_, slot->ahb);
        if (slot->allocator->width() != static_cast<int>(generation_.contract.width) ||
            slot->allocator->height() != static_cast<int>(generation_.contract.height)) {
            error = "ncnn imported AHB dimensions differ from bridge generation"; return false;
        }
        slot->image = ncnn::VkImageMat::from_android_hardware_buffer(slot->allocator.get());
        if (slot->image.empty()) { error = "ncnn sampled AHB import failed"; return false; }
        slot->import_pipeline = std::make_unique<ncnn::ImportAndroidHardwareBufferPipeline>(device_);
        ncnn::Option import_option = option_;
        import_option.use_fp16_packed = false;
        import_option.use_fp16_storage = false;
        import_option.use_fp16_arithmetic = false;
        if (slot->import_pipeline->create(slot->allocator.get(), 1, 1, import_option) != 0) {
            error = "ncnn RGB AHB import pipeline creation failed"; return false;
        }
        ExportSemaphoreCreateInfo export_info{kExportSemaphoreCreateInfo};
        export_info.handleTypes = kSyncFdHandleType;
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &export_info};
        if (ncnn::vkCreateSemaphore(device_->vkdevice(), &sem, nullptr, &slot->import_semaphore) != VK_SUCCESS) {
            error = "ncnn external sync-fd semaphore creation failed"; return false;
        }
        slot->compute = std::make_unique<ncnn::VkCompute>(device_);
        slot->imported_rgb.create(generation_.contract.width, generation_.contract.height, 3,
                                  sizeof(float), 1, blob_allocator_);
        slot->normalized.create(contract_.width, contract_.height,
                                NormalizedChannelCount(contract_.output_elempack),
                                sizeof(float), 1, blob_allocator_);
        const int channels = (3 + contract_.output_elempack - 1) / contract_.output_elempack;
        const size_t elemsize = static_cast<size_t>(contract_.output_elempack) *
            (contract_.output_type == HV_GPU_TENSOR_FP16 ? 2u : 4u);
        slot->prepared_input.create(contract_.width, contract_.height, channels,
                                    elemsize, contract_.output_elempack, blob_allocator_);
        if (slot->imported_rgb.empty() || slot->normalized.empty() || slot->prepared_input.empty()) {
            error = "ncnn cached preprocessing tensor allocation failed"; return false;
        }
        if (!gate_mode_) {
            slot->extractor = std::make_unique<ncnn::Extractor>(net_->create_extractor());
            slot->extractor->set_blob_vkallocator(blob_allocator_);
            slot->extractor->set_workspace_vkallocator(blob_allocator_);
            slot->extractor->set_staging_vkallocator(staging_allocator_);
            slot->pristine_extractor = std::make_unique<ncnn::Extractor>(*slot->extractor);
        }
        slot->gpu_outputs.resize(contract_.output_blobs.size());
        slot->fp32_outputs.resize(contract_.output_blobs.size());
        slot->cpu_outputs.resize(contract_.output_blobs.size());
        slot->dense_outputs.resize(contract_.output_blobs.size());
        slots_[i] = std::move(slot);
    }
    error.clear(); return true;
}

bool AndroidSession::Initialize(const HV_GpuBackendConfigV1& config,
                                const HV_GpuDeviceContextV1& api_device,
                                std::string& error) {
    if (!api_device.host_context) { error = "Missing ncnn Vulkan host context"; return false; }
    const auto& host = *static_cast<const HostContext*>(api_device.host_context);
    if (host.struct_size < sizeof(host) || host.api_version != HV_GPU_FRAME_API_V1 ||
        !host.bridge || !host.unity_device.instance || !host.unity_device.physical_device) {
        error = "Invalid ncnn Vulkan host context or bridge"; return false;
    }
    bridge_ = host.bridge;
    if (!ParseModel(config, error)) return false;
    if (!gpu::AcquireNcnnGpuInstance()) { error = "ncnn Vulkan GPU instance creation failed"; return false; }
    gpu_instance_lease_ = true;
    const auto match = gpu::MatchNcnnDevice(host.unity_device);
    if (match.status != gpu::DeviceMatchStatus::Matched ||
        !SameUuid(api_device.device_uuid, match.identity.device_uuid) ||
        !SameUuid(api_device.driver_uuid, match.identity.driver_uuid)) {
        error = "ncnn Vulkan deviceUUID/driverUUID exact match failed: " + match.diagnostic;
        return false;
    }
    device_ = ncnn::get_gpu_device(match.index); // Explicit exact UUID-pair index; never default.
    if (!device_ || !device_->is_valid()) { error = "Matched ncnn VulkanDevice is invalid"; return false; }
    const auto& info = device_->info;
    if (!info.support_VK_ANDROID_external_memory_android_hardware_buffer()) {
        error = "Missing VK_ANDROID_external_memory_android_hardware_buffer"; return false;
    }
    if (!info.support_VK_KHR_external_semaphore_fd()) {
        error = "Missing VK_KHR_external_semaphore_fd"; return false;
    }
    if (contract_.output_type == HV_GPU_TENSOR_FP16) {
        if (!info.support_fp16_packed()) { error = "Missing fp16 packed storage"; return false; }
        if (!info.support_fp16_storage() || !info.query16BitStorageFeatures().storageBuffer16BitAccess) {
            error = "Missing fp16 storageBuffer16BitAccess"; return false;
        }
        if (!info.support_fp16_arithmetic() || !info.queryFloat16Int8Features().shaderFloat16) {
            error = "Missing fp16 shaderFloat16 arithmetic"; return false;
        }
    }
    if (!ncnn::vkGetDeviceProcAddr(device_->vkdevice(), "vkImportSemaphoreFdKHR")) {
        error = "Matched ncnn device lacks vkImportSemaphoreFdKHR"; return false;
    }
    option_.use_vulkan_compute = true;
    option_.vulkan_device_index = match.index;
    option_.use_fp16_packed = contract_.output_type == HV_GPU_TENSOR_FP16;
    option_.use_fp16_storage = contract_.output_type == HV_GPU_TENSOR_FP16;
    ApplyBackendOptions(option_, backend_options_);
    option_.use_packing_layout = contract_.output_elempack != 1;
    net_ = std::make_unique<ncnn::Net>();
    net_->opt = option_;
    net_->set_vulkan_device(match.index);
    if (net_->load_param(param_path_.c_str()) != 0 || net_->load_model(bin_path_.c_str()) != 0) {
        error = "ncnn Vulkan model param/bin loading failed"; return false;
    }
    blob_allocator_ = device_->acquire_blob_allocator();
    staging_allocator_ = device_->acquire_staging_allocator();
    if (!blob_allocator_ || !staging_allocator_) { error = "ncnn Vulkan allocator acquisition failed"; return false; }
    option_.blob_vkallocator = blob_allocator_;
    option_.workspace_vkallocator = blob_allocator_;
    option_.staging_vkallocator = staging_allocator_;
    preprocess_ = std::make_unique<GpuPreprocess>();
    if (!preprocess_->Initialize(device_, option_, error)) return false;
    if (!InitializeSlots(*host.bridge, error)) return false;
    if (detector_role_) {
        // VkCompute reuses its command pool/buffer. Without push descriptors
        // pinned ncnn allocates descriptor pools per reset, breaking the
        // one-job resource cache contract for this path.
        if (!device_->info.support_VK_KHR_push_descriptor()) {
            error = "ncnn detached detector requires reusable Vulkan push descriptors";
            return false;
        }
        prepared_ = std::make_unique<PreparedGpuResources>();
        if (!prepared_->Initialize(device_, *net_, blob_allocator_, staging_allocator_, contract_) ||
            !prepared_state_.Initialize(generation_.generation)) {
            error = "ncnn detached detector GPU resource initialization failed";
            return false;
        }
    }
    return true;
}

#if defined(HV_ANDROID_GPU_GATE)
bool AndroidSession::InitializeGate(const std::string& input_contract_json,
                                    const HostContext& host, std::string& error) {
    gate_mode_ = true;
    bridge_ = host.bridge;
    if (!bridge_ || !host.unity_device.instance || !host.unity_device.physical_device) {
        error = "Gate requires configured production Unity Vulkan bridge"; return false;
    }
    try {
        if (!ParseInputContract(nlohmann::json::parse(input_contract_json), contract_, error)) return false;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
    if (!gpu::AcquireNcnnGpuInstance()) { error = "Gate ncnn Vulkan GPU instance creation failed"; return false; }
    gpu_instance_lease_ = true;
    const auto match = gpu::MatchNcnnDevice(host.unity_device);
    if (match.status != gpu::DeviceMatchStatus::Matched) {
        error = "Gate ncnn UUID pair mismatch: " + match.diagnostic; return false;
    }
    device_ = ncnn::get_gpu_device(match.index);
    if (!device_ || !device_->is_valid()) { error = "Gate matched ncnn device invalid"; return false; }
    const auto& info = device_->info;
    if (!info.support_VK_ANDROID_external_memory_android_hardware_buffer() ||
        !info.support_VK_KHR_external_semaphore_fd() ||
        !ncnn::vkGetDeviceProcAddr(device_->vkdevice(), "vkImportSemaphoreFdKHR")) {
        error = "Gate ncnn device lacks AHB or external sync-fd import"; return false;
    }
    if (contract_.output_type == HV_GPU_TENSOR_FP16 &&
        (!info.support_fp16_packed() || !info.support_fp16_storage() ||
         !info.query16BitStorageFeatures().storageBuffer16BitAccess ||
         !info.support_fp16_arithmetic() || !info.queryFloat16Int8Features().shaderFloat16)) {
        error = "Gate ncnn device lacks required FP16 storage or arithmetic"; return false;
    }
    option_.use_vulkan_compute = true;
    option_.vulkan_device_index = match.index;
    option_.use_fp16_packed = option_.use_fp16_storage = option_.use_fp16_arithmetic =
        contract_.output_type == HV_GPU_TENSOR_FP16;
    option_.use_packing_layout = contract_.output_elempack != 1;
    blob_allocator_ = device_->acquire_blob_allocator();
    staging_allocator_ = device_->acquire_staging_allocator();
    if (!blob_allocator_ || !staging_allocator_) { error = "Gate ncnn allocator unavailable"; return false; }
    option_.blob_vkallocator = option_.workspace_vkallocator = blob_allocator_;
    option_.staging_vkallocator = staging_allocator_;
    preprocess_ = std::make_unique<GpuPreprocess>();
    if (!preprocess_->Initialize(device_, option_, error)) return false;
    return InitializeSlots(*bridge_, error);
}
#endif

bool AndroidSession::ValidateTransform(const HV_GpuFrameRefV1& frame,
                                       const HV_GpuImageTransformV1& transform,
                                       const gpu::ConsumerFrame& consumer,
                                       std::string& error) const {
    if (!consumer.claimed || consumer.token.index >= slots_.size() ||
        consumer.metadata.generation != generation_.generation ||
        frame.generation != generation_.generation ||
        frame.frame_id != static_cast<int64_t>(consumer.metadata.frame_id) ||
        frame.timestamp_us != consumer.metadata.timestamp_us ||
        frame.width != static_cast<int>(generation_.contract.width) ||
        frame.height != static_cast<int>(generation_.contract.height) ||
        frame.image_format != contract_.image_format || frame.flags ||
        consumer.ahb_buffer != generation_.retained_ahb[consumer.token.index] ||
        transform.output_width != contract_.width || transform.output_height != contract_.height ||
        transform.output_type != contract_.output_type ||
        transform.output_elempack != static_cast<uint32_t>(contract_.output_elempack) ||
        transform.channel_order != contract_.channel_order ||
        !std::equal(contract_.mean.begin(), contract_.mean.end(), transform.mean) ||
        !std::equal(contract_.norm.begin(), contract_.norm.end(), transform.norm)) {
        error = "ncnn GPU frame, slot generation, or input contract mismatch"; return false;
    }
    return true;
}

bool AndroidSession::DrainDropped(std::string& error) {
    for (;;) {
        gpu::ConsumerFrame frame;
        const auto claimed = bridge_->ClaimDropped(frame);
        if (claimed == gpu::SlotResult::NoReady) return true;
        if (claimed == gpu::SlotResult::Busy) continue;
        if (claimed != gpu::SlotResult::Ok) {
            error = "ncnn cannot claim superseded AHB slot for GPU drain"; return false;
        }
        auto& slot = *slots_[frame.token.index];
        const auto import = reinterpret_cast<ImportSemaphoreFd>(
            ncnn::vkGetDeviceProcAddr(device_->vkdevice(), "vkImportSemaphoreFdKHR"));
        ImportSemaphoreFdInfo fd_info{kImportSemaphoreFdInfo};
        fd_info.semaphore = slot.import_semaphore;
        fd_info.flags = kTemporaryImport;
        fd_info.handleType = kSyncFdHandleType;
        fd_info.fd = frame.producer_fd.Get();
        gpu::SyncFd producer_proof;
        if (frame.producer_fd.Get() >= 0) {
            const int duplicate = fcntl(frame.producer_fd.Get(), F_DUPFD_CLOEXEC, 0);
            if (duplicate < 0) {
                if (WaitProducerFd(frame.producer_fd))
                    bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent);
                else { terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(frame); }
                error = "ncnn cannot retain dropped-frame producer proof"; return false;
            }
            producer_proof = gpu::SyncFd(duplicate);
        }
        if (!import || import(device_->vkdevice(), &fd_info) != VK_SUCCESS) {
            // Error cleanup alone may block on CPU; accepted frames use GPU wait.
            if (WaitProducerFd(frame.producer_fd))
                bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent);
            else { terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(frame); }
            error = "ncnn superseded frame sync-fd import failed"; return false;
        }
        frame.producer_fd.Release();
        slot.compute->record_import_android_hardware_buffer(slot.import_pipeline.get(),
            slot.image, slot.imported_rgb, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL_KHR,
            device_->info.compute_queue_family_index());
        slot.compute->record_release_android_hardware_buffer(slot.image,
            device_->info.compute_queue_family_index(), VK_QUEUE_FAMILY_EXTERNAL_KHR);
        if (slot.compute->submit_and_wait(slot.import_semaphore,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0) {
            if (ncnn::vkDeviceWaitIdle(device_->vkdevice()) == VK_SUCCESS &&
                WaitProducerFd(producer_proof))
                bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent);
            else { terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(frame); }
            error = "ncnn superseded AHB GPU drain failed"; return false;
        }
        if (slot.compute->reset() != 0) {
            if (ncnn::vkDeviceWaitIdle(device_->vkdevice()) == VK_SUCCESS)
                bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent);
            else { terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(frame); }
            error = "ncnn superseded AHB command reset failed"; return false;
        }
        if (bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent) != gpu::SlotResult::Ok) {
            error = "ncnn superseded AHB retirement failed"; return false;
        }
    }
}

HV_Result AndroidSession::Prepare(const HV_GpuFrameRefV1& frame,
                                  const HV_GpuImageTransformV1& transform,
                                  HV_GpuPreparedRefV1& ref, std::string& error) {
    std::unique_lock<std::mutex> lock(run_mutex_);
    if (!detector_role_ || !prepared_ || !bridge_ || terminal_gpu_fault_ ||
        prepared_state_.Quarantined()) {
        error = "ncnn prepared input requires a healthy detector session";
        return HV_ERR_NOT_INITIALIZED;
    }
    if (!prepared_state_.Idle() || active_consumer_) {
        error = "ncnn detached detector job is busy";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (!frame.opaque_slot) { error = "ncnn prepared source slot is missing"; return HV_ERR_INVALID_ARGUMENT; }
    auto& consumer = *static_cast<gpu::ConsumerFrame*>(frame.opaque_slot);
    if (consumer.token.index >= slots_.size() ||
        !ValidateTransform(frame, transform, consumer, error)) return HV_ERR_INVALID_ARGUMENT;
    if (!DrainDropped(error)) return HV_ERR_INTERNAL;
    const auto start = gpu::NextNcnnRole(consumer);
    if (start == gpu::NcnnRoleStart::Invalid) {
        error = "ncnn prepared source has no valid producer or prior role proof";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (!prepared_state_.Begin(frame, ref)) {
        error = "ncnn detached detector token admission failed";
        return HV_ERR_INVALID_ARGUMENT;
    }
    auto fail = [&](const char* reason) {
        terminal_gpu_fault_ = true;
        prepared_state_.Quarantine();
        bridge_->QuarantineConsumer(consumer);
        active_consumer_ = nullptr;
        error = reason;
        return HV_ERR_INTERNAL;
    };
    auto& source = *slots_[consumer.token.index];
    auto& cmd = *prepared_->compute;
    const bool producer_wait = start == gpu::NcnnRoleStart::WaitForProducer;
    if (producer_wait) {
        const auto import = reinterpret_cast<ImportSemaphoreFd>(
            ncnn::vkGetDeviceProcAddr(device_->vkdevice(), "vkImportSemaphoreFdKHR"));
        ImportSemaphoreFdInfo fd_info{kImportSemaphoreFdInfo};
        fd_info.semaphore = source.import_semaphore;
        fd_info.flags = kTemporaryImport;
        fd_info.handleType = kSyncFdHandleType;
        fd_info.fd = consumer.producer_fd.Get();
        if (!import || import(device_->vkdevice(), &fd_info) != VK_SUCCESS)
            return fail("ncnn detached detector sync-fd import failed");
        consumer.producer_fd.Release(); // Vulkan takes ownership, including -1 sentinel.
    }
    cmd.record_import_android_hardware_buffer(source.import_pipeline.get(),
        source.image, source.imported_rgb, VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_EXTERNAL_KHR, device_->info.compute_queue_family_index());
    if (!preprocess_->Record(source.imported_rgb, transform, prepared_->normalized,
                             cmd, contract_.pad_rgb.data(), error))
        return fail("ncnn detached detector GPU preprocessing record failed");
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    if (frame.frame_id % 120 == 0)
        __android_log_print(ANDROID_LOG_INFO, "HV_TOPDOWN_NCNN",
            "input_contract frame=%lld source=%dx%d rect=%g,%g,%g,%g pad=%g,%g,%g",
            static_cast<long long>(frame.frame_id), source.imported_rgb.w,
            source.imported_rgb.h, transform.source_rect_px.x,
            transform.source_rect_px.y, transform.source_rect_px.width,
            transform.source_rect_px.height, contract_.pad_rgb[0],
            contract_.pad_rgb[1], contract_.pad_rgb[2]);
#endif
    const auto* cached_tensor_buffer = prepared_->tensor.data;
    device_->convert_packing(prepared_->normalized, prepared_->tensor,
        1, 2, cmd, option_);
    if (prepared_->tensor.empty() || prepared_->tensor.w != 320 ||
        prepared_->tensor.h != 320 || prepared_->tensor.c != 3 ||
        prepared_->tensor.elempack != 1 || prepared_->tensor.elembits() != 16 ||
        prepared_->tensor.data != cached_tensor_buffer)
        return fail("ncnn detached detector tensor is not 320x320 RGB FP16 pack1");
    const int submitted = producer_wait
        ? cmd.submit_and_wait(source.import_semaphore, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        : cmd.submit_and_wait();
    if (submitted != 0) return fail("ncnn detached detector GPU copy completion unproven");
    if (!prepared_state_.ProveGpuCopy() || cmd.reset() != 0)
        return fail("ncnn detached detector command reset failed after GPU copy");
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    // Sparse diagnostic: 5x5x3 floats from the already GPU-resident normalized
    // detector input. This never downloads the source image or full input.
    if (frame.frame_id % 30 == 0) {
        ncnn::VkMat sparse;
        sparse.create(5, 5, 3, sizeof(float), 1, blob_allocator_);
        if (sparse.empty()) return fail("ncnn sparse diagnostic allocation failed");
        HV_GpuImageTransformV1 sample = transform;
        sample.source_rect_px = {0, 0, float(prepared_->normalized.w), float(prepared_->normalized.h)};
        sample.output_width = 5; sample.output_height = 5;
        for (int i = 0; i < 3; ++i) { sample.mean[i] = 0; sample.norm[i] = 1; }
        constexpr float zero_pad[3] = {0, 0, 0};
        if (!preprocess_->Record(prepared_->normalized, sample, sparse, cmd, zero_pad, error))
            return fail("ncnn sparse diagnostic GPU record failed");
        ncnn::Mat sparse_cpu;
        cmd.record_download(sparse, sparse_cpu, option_);
        if (cmd.submit_and_wait() != 0 || cmd.reset() != 0 ||
            sparse_cpu.dims != 3 || sparse_cpu.w != 5 || sparse_cpu.h != 5 ||
            sparse_cpu.c != 3 || sparse_cpu.elembits() != 32)
            return fail("ncnn sparse diagnostic GPU completion or shape unproven");
        for (int c = 0; c < 3; ++c) {
            auto channel = sparse_cpu.channel(c);
            float low = std::numeric_limits<float>::infinity();
            float high = -std::numeric_limits<float>::infinity();
            for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
                const float v = channel.row<float>(y)[x];
                low = std::min(low, v); high = std::max(high, v);
            }
            __android_log_print(ANDROID_LOG_INFO, "HV_TOPDOWN_NCNN",
                "sparse_input frame=%lld channel=%d min=%g max=%g center=%g",
                static_cast<long long>(frame.frame_id), c, low, high,
                channel.row<float>(2)[2]);
            if (frame.frame_id % 120 == 0)
                for (int y = 0; y < 5; ++y) {
                    const float* row = channel.row<float>(y);
                    __android_log_print(ANDROID_LOG_INFO, "HV_TOPDOWN_NCNN",
                        "sparse_grid frame=%lld channel=%d row=%d values=%g,%g,%g,%g,%g",
                        static_cast<long long>(frame.frame_id), c, y,
                        row[0], row[1], row[2], row[3], row[4]);
                }
        }
    }
#endif
    // The copy is complete. The role release below transfers the sampled AHB
    // back to external ownership; it does not retire the observation slot.
    active_consumer_ = &consumer;
    active_token_ = consumer.token;
    consumer.role_owner = this;
    consumer.complete_role = &AndroidSession::CompleteRoleCallback;
    lock.unlock(); // CompleteGpuRole calls YieldObservation, which owns run_mutex_.
    if (!gpu::CompleteGpuRole(consumer, false, error)) {
        lock.lock();
        terminal_gpu_fault_ = true;
        prepared_state_.Quarantine();
        if (consumer.claimed) bridge_->QuarantineConsumer(consumer);
        active_consumer_ = nullptr;
        return HV_ERR_INTERNAL;
    }
    lock.lock();
    if (!prepared_state_.ReleaseSourceRole()) {
        terminal_gpu_fault_ = true;
        prepared_state_.Quarantine();
        error = "ncnn detached detector lost GPU completion proof";
        return HV_ERR_INTERNAL;
    }
    error.clear();
    return HV_OK;
}

HV_Result AndroidSession::RunPrepared(const HV_GpuPreparedRefV1& ref,
                                      HV_TensorViewV1* outputs, uint32_t capacity,
                                      uint32_t& count, std::string& error) {
    std::lock_guard<std::mutex> lock(run_mutex_);
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    const auto run_begun = TraceMs();
    TraceDetectorStage("begin", 0, run_begun);
#endif
    count = 0;
    if (!detector_role_ || !prepared_ || terminal_gpu_fault_ ||
        !prepared_state_.Ready(ref)) {
        error = "ncnn detector prepared token is stale or unavailable";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (!outputs || capacity < contract_.output_blobs.size()) {
        error = "ncnn prepared detector output capacity is insufficient";
        return HV_ERR_INVALID_ARGUMENT;
    }
    auto& slot = *prepared_;
    const auto unsafe_failure = [&](const char* message, HV_Result result) {
        // An extractor may have recorded GPU commands even when it reports
        // failure. Keep the command/tensor allocation alive; do not recycle it.
        terminal_gpu_fault_ = true;
        prepared_state_.Quarantine();
        error = message;
        return result;
    };
    // The pinned ncnn clear() empties blob_mats and blob_mats_gpu, making
    // subsequent input(index, VkMat) fail. Assignment restores sized, empty
    // vectors while retaining the active extractor's warmed vector capacity.
    *slot.extractor = *slot.pristine_extractor;
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    TraceDetectorStage("deliver_begin", 0, run_begun);
#endif
    const auto delivery = DeliverInput(*slot.extractor, contract_.input_blob.c_str(),
        slot.tensor, true);
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    TraceDetectorStage("deliver_end", 0, run_begun);
#endif
    if (delivery != InputDeliveryResult::Ok) {
        return unsafe_failure("ncnn detector rejected detached FP16 pack1 tensor",
            delivery == InputDeliveryResult::InvalidTensor ? HV_ERR_INVALID_ARGUMENT : HV_ERR_MODEL_LOAD);
    }
    for (size_t i = 0; i < contract_.output_blobs.size(); ++i) {
        slot.gpu_outputs[i].release();
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
        TraceDetectorStage("extract_begin", i, run_begun);
#endif
        if (slot.extractor->extract(contract_.output_blobs[i].c_str(),
                                    slot.gpu_outputs[i], *slot.compute) != 0 ||
            slot.gpu_outputs[i].empty()) {
            return unsafe_failure("ncnn prepared detector extraction failed", HV_ERR_INTERNAL);
        }
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
        TraceDetectorStage("extract_end", i, run_begun);
        TraceDetectorStage("convert_begin", i, run_begun);
#endif
        device_->convert_packing(slot.gpu_outputs[i], slot.fp32_outputs[i],
                                 1, 1, *slot.compute, option_);
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
        TraceDetectorStage("convert_end", i, run_begun);
#endif
        DenseOutputLayout layout;
        if (slot.fp32_outputs[i].empty() || slot.fp32_outputs[i].elempack != 1 ||
            slot.fp32_outputs[i].elembits() != 32 ||
            !DescribeDenseOutput(slot.fp32_outputs[i].dims, slot.fp32_outputs[i].w,
                slot.fp32_outputs[i].h, slot.fp32_outputs[i].d,
                slot.fp32_outputs[i].c, slot.fp32_outputs[i].cstep,
                output_byte_limits_[i], layout) ||
            !ValidateDenseDownload(layout, slot.fp32_outputs[i].total())) {
            return unsafe_failure("ncnn prepared detector output exceeds declared bounds", HV_ERR_MODEL_LOAD);
        }
        slot.compute->record_download(slot.fp32_outputs[i], slot.cpu_outputs[i], option_);
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
        TraceDetectorStage("download_recorded", i, run_begun);
#endif
        ++slot.output_download_calls;
    }
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    TraceDetectorStage("submit_begin", 0, run_begun);
#endif
    if (slot.compute->submit_and_wait() != 0 || slot.compute->reset() != 0) {
        terminal_gpu_fault_ = true;
        prepared_state_.Quarantine();
        error = "ncnn prepared detector inference GPU completion unproven";
        return HV_ERR_INTERNAL;
    }
#if defined(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    TraceDetectorStage("submit_end", 0, run_begun);
#endif
    for (size_t i = 0; i < contract_.output_blobs.size(); ++i) {
        const auto& mat = slot.cpu_outputs[i];
        DenseOutputLayout layout;
        if (mat.empty() || mat.elemsize != sizeof(float) ||
            !DescribeDenseOutput(mat.dims, mat.w, mat.h, mat.d, mat.c,
                mat.cstep, output_byte_limits_[i], layout) ||
            !ValidateDenseDownload(layout, mat.total())) {
            return unsafe_failure("ncnn prepared detector download violates output bounds", HV_ERR_MODEL_LOAD);
        }
        const auto values = layout.logical_bytes / sizeof(float);
        if (slot.dense_outputs[i].size() < values) slot.dense_outputs[i].resize(values);
        if (!CompactDenseFp32(reinterpret_cast<const float*>(mat.data), layout,
                              slot.dense_outputs[i].data(), slot.dense_outputs[i].size())) {
            return unsafe_failure("ncnn prepared detector output channel stride invalid", HV_ERR_MODEL_LOAD);
        }
        auto& view = outputs[i];
        view = {};
        view.struct_size = sizeof(view); view.api_version = HV_PLUGIN_API_V1;
        view.name = contract_.output_blobs[i].c_str();
        view.element_type = 1; view.rank = layout.rank;
        std::copy(layout.dimensions.begin(), layout.dimensions.end(), view.dimensions);
        view.data = slot.dense_outputs[i].data();
        view.byte_count = layout.logical_bytes;
    }
    count = static_cast<uint32_t>(contract_.output_blobs.size());
    if (!prepared_state_.Consume(ref)) {
        terminal_gpu_fault_ = true; prepared_state_.Quarantine();
        error = "ncnn prepared detector token consumption failed";
        return HV_ERR_INTERNAL;
    }
    error.clear(); return HV_OK;
}

HV_Result AndroidSession::DiscardPrepared(const HV_GpuPreparedRefV1& ref,
                                          std::string& error) {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!prepared_state_.Discard(ref)) {
        error = "ncnn prepared detector discard token is stale or unproved";
        return HV_ERR_INVALID_ARGUMENT;
    }
    error.clear(); return HV_OK;
}

#if defined(HV_ANDROID_GPU_GATE)
bool AndroidSession::VerifyPreparedGoldenForGate(const HV_GpuPreparedRefV1& ref,
                                                 std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!prepared_ || !prepared_state_.Ready(ref)) {
        diagnostic = "Prepared gate token unavailable for tensor proof";
        return false;
    }
    // Test-only readback of the detached 320x320x3 tensor. Production builds
    // never compile this method and never download source pixels or this input.
    ncnn::Mat cpu;
    prepared_->compute->record_download(prepared_->tensor, cpu, option_);
    if (prepared_->compute->submit_and_wait() != 0 ||
        prepared_->compute->reset() != 0) {
        terminal_gpu_fault_ = true; prepared_state_.Quarantine();
        diagnostic = "Prepared gate tensor diagnostic GPU completion unproven";
        return false;
    }
    if (cpu.dims != 3 || cpu.w != 320 || cpu.h != 320 || cpu.c != 3 ||
        cpu.elempack != 1 || (cpu.elemsize != 2 && cpu.elemsize != 4)) {
        diagnostic = "Prepared gate tensor diagnostic shape/dtype invalid";
        return false;
    }
    std::vector<unsigned char> fp16(320u * 320u * 3u * 2u);
    for (int channel = 0; channel < 3; ++channel) {
        const auto plane = cpu.channel(channel);
        for (int y = 0; y < 320; ++y)
            for (int x = 0; x < 320; ++x) {
                const auto index = static_cast<size_t>(channel) * 320u * 320u +
                    static_cast<size_t>(y) * 320u + x;
                const auto bits = cpu.elemsize == 4
                    ? ncnn::float32_to_float16(reinterpret_cast<const float*>(plane.data)[y * 320 + x])
                    : reinterpret_cast<const uint16_t*>(plane.data)[y * 320 + x];
                fp16[index * 2] = static_cast<unsigned char>(bits);
                fp16[index * 2 + 1] = static_cast<unsigned char>(bits >> 8);
            }
    }
    picosha2::hash256_one_by_one hash;
    hash.process(fp16.begin(), fp16.end()); hash.finish();
    const auto actual = picosha2::get_hash_hex_string(hash);
    // Strict full-tensor golden from the Snapdragon 888 ncnn Vulkan RTZ cast.
    // Other devices must independently pass this device gate.
    constexpr const char* expected =
        "9e598ea5d37ebe348bd8235c412d404d62ceb78018dfdfb942a88a26ad118d19";
    auto sample = [&fp16](int x, int y, int channel) {
        const auto index = (static_cast<size_t>(channel) * 320u * 320u +
            static_cast<size_t>(y) * 320u + x) * 2u;
        const uint16_t bits = static_cast<uint16_t>(fp16[index]) |
            static_cast<uint16_t>(fp16[index + 1] << 8);
        return ncnn::float16_to_float32(bits);
    };
    std::ostringstream samples;
    for (const auto point : {std::pair<int, int>{80, 80}, {240, 80},
                             {80, 240}, {240, 240}, {0, 0}, {319, 319}})
        samples << " (" << point.first << ',' << point.second << ":"
                << sample(point.first, point.second, 0) << ','
                << sample(point.first, point.second, 1) << ','
                << sample(point.first, point.second, 2) << ')';
    diagnostic = "Prepared gate tensor_fp16_sha256=" + actual +
        " expected=" + expected + " samples=" + samples.str();
    return actual == expected;
}
#endif

void AndroidSession::QuarantinePrepared(void* opaque_slot) noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    terminal_gpu_fault_ = true;
    prepared_state_.Quarantine();
    if (opaque_slot && bridge_) {
        auto& source = *static_cast<gpu::ConsumerFrame*>(opaque_slot);
        if (source.claimed) bridge_->QuarantineConsumer(source);
    }
    active_consumer_ = nullptr;
}

HV_Result AndroidSession::Run(const HV_GpuFrameRefV1& frame,
                              const HV_GpuImageTransformV1& transform,
                              HV_TensorViewV1* outputs, uint32_t capacity,
                              uint32_t& count, std::string& error) {
    std::lock_guard<std::mutex> lock(run_mutex_); // One ncnn worker/queue sequence per session.
    count = 0;
    if (terminal_gpu_fault_) {
        error = "ncnn GPU completion was not proved; session is quarantined";
        return HV_ERR_INTERNAL;
    }
    if (!frame.opaque_slot) { error = "ncnn borrowed slot is missing"; return HV_ERR_INVALID_ARGUMENT; }
    auto& consumer = *static_cast<gpu::ConsumerFrame*>(frame.opaque_slot);
    if (!consumer.claimed || consumer.token.index >= slots_.size()) {
        if (consumer.claimed) {
            if (active_consumer_ == &consumer) {
                terminal_gpu_fault_ = true;
                bridge_->QuarantineConsumer(consumer);
                active_consumer_ = nullptr;
            } else {
                RetireUnsubmitted(&consumer);
            }
        }
        error = "ncnn borrowed slot is invalid"; return HV_ERR_INVALID_ARGUMENT;
    }
    if (active_consumer_ && active_consumer_ != &consumer) {
        if (consumer.role_owner == this) {
            terminal_gpu_fault_ = true;
            bridge_->QuarantineConsumer(consumer);
        } else {
            RetireUnsubmitted(&consumer);
        }
        error = "ncnn observation must finish before another slot is run";
        return HV_ERR_INVALID_ARGUMENT;
    }
    const bool first_run = active_consumer_ == nullptr;
    if (first_run && consumer.role_owner) {
        if (consumer.role_owner == this) {
            terminal_gpu_fault_ = true;
            bridge_->QuarantineConsumer(consumer);
        } else {
            RetireUnsubmitted(&consumer);
        }
        error = "ncnn previous model role must release the observation before handoff";
        return HV_ERR_INVALID_ARGUMENT;
    }
    auto& slot = *slots_[consumer.token.index];
    bool imported = false, acquired = !first_run, released = false, pending = false, unproven = false;
    const auto retire = [&](gpu::ConsumerFrame* lease) noexcept {
        if (unproven) {
            terminal_gpu_fault_ = true;
            bridge_->QuarantineConsumer(*lease);
            active_consumer_ = nullptr;
            return;
        }
        if (acquired && !released) {
            slot.compute->record_release_android_hardware_buffer(slot.image,
                device_->info.compute_queue_family_index(), VK_QUEUE_FAMILY_EXTERNAL_KHR);
            released = true;
            pending = true;
        }
        if (imported) {
            const int submitted = slot.compute->submit_and_wait(slot.import_semaphore,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            if (submitted != 0) {
                terminal_gpu_fault_ = true;
                bridge_->QuarantineConsumer(*lease);
                active_consumer_ = nullptr;
                return;
            } else if (slot.compute->reset() != 0) {
                terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(*lease);
                active_consumer_ = nullptr; return;
            }
        } else if (pending) {
            if (slot.compute->submit_and_wait() != 0) {
                terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(*lease);
                active_consumer_ = nullptr; return;
            } else if (slot.compute->reset() != 0) {
                terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(*lease);
                active_consumer_ = nullptr; return;
            }
        }
        if (!WaitProducerFd(lease->producer_fd)) {
            terminal_gpu_fault_ = true; bridge_->QuarantineConsumer(*lease); return;
        }
        bridge_->RetireConsumer(*lease, gpu::CompletionProof::GpuQuiescent);
        active_consumer_ = nullptr;
    };
    std::unique_ptr<gpu::ConsumerFrame, decltype(retire)> lease_guard(&consumer, retire);
    if (!gate_mode_ && (capacity < contract_.output_blobs.size() || !outputs)) {
        error = "ncnn output capacity is insufficient"; return HV_ERR_INVALID_ARGUMENT;
    }
    if (!ValidateTransform(frame, transform, consumer, error)) return HV_ERR_INVALID_ARGUMENT;
    if (first_run && !DrainDropped(error)) return HV_ERR_INTERNAL;
    if (first_run) {
        const auto start = gpu::NextNcnnRole(consumer);
        if (start == gpu::NcnnRoleStart::Invalid) {
            error = "ncnn observation has no valid producer or prior GPU role proof";
            return HV_ERR_INVALID_ARGUMENT;
        }
        const bool producer_wait = start == gpu::NcnnRoleStart::WaitForProducer;
        if (producer_wait) {
            const auto import = reinterpret_cast<ImportSemaphoreFd>(
                ncnn::vkGetDeviceProcAddr(device_->vkdevice(), "vkImportSemaphoreFdKHR"));
            ImportSemaphoreFdInfo fd_info{kImportSemaphoreFdInfo};
            fd_info.semaphore = slot.import_semaphore;
            fd_info.flags = kTemporaryImport;
            fd_info.handleType = kSyncFdHandleType;
            fd_info.fd = consumer.producer_fd.Get();
            if (!import || import(device_->vkdevice(), &fd_info) != VK_SUCCESS) {
                error = "ncnn temporary sync-fd semaphore import failed"; return HV_ERR_INTERNAL;
            }
            consumer.producer_fd.Release(); // Vulkan owns the first role's fd.
            imported = true;
        }
        slot.compute->record_import_android_hardware_buffer(slot.import_pipeline.get(),
            slot.image, slot.imported_rgb, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL_KHR,
            device_->info.compute_queue_family_index());
        acquired = true;
        if (!preprocess_->Record(slot.imported_rgb, transform, slot.normalized, *slot.compute,
                                 contract_.pad_rgb.data(), error))
            return HV_ERR_INVALID_ARGUMENT;
        device_->convert_packing(slot.normalized, slot.prepared_input,
            contract_.output_elempack, contract_.cast_type_to, *slot.compute, option_);
        if (slot.prepared_input.empty() || slot.prepared_input.w != contract_.width ||
            slot.prepared_input.h != contract_.height ||
            slot.prepared_input.c != (3 + contract_.output_elempack - 1) / contract_.output_elempack ||
            slot.prepared_input.elempack != contract_.output_elempack ||
            slot.prepared_input.elembits() != (contract_.output_type == HV_GPU_TENSOR_FP16 ? 16 : 32)) {
            error = "ncnn explicit FP16/packing conversion produced the wrong tensor";
            return HV_ERR_INTERNAL;
        }
        const int submitted = producer_wait
            ? slot.compute->submit_and_wait(slot.import_semaphore, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            : slot.compute->submit_and_wait();
        if (submitted != 0) {
            unproven = true;
            imported = false;
            error = "ncnn GPU wait/import/preprocess submission failed"; return HV_ERR_INTERNAL;
        }
        imported = false;
        if (slot.compute->reset() != 0) {
            unproven = true;
            error = "ncnn GPU preprocessing command reset failed"; return HV_ERR_INTERNAL;
        }
        active_consumer_ = &consumer;
        active_token_ = consumer.token;
    } else {
        if (consumer.producer_fd.HasPayload()) {
            error = "ncnn repeated observation unexpectedly owns producer sync-fd";
            return HV_ERR_INVALID_ARGUMENT;
        }
        if (!preprocess_->Record(slot.imported_rgb, transform, slot.normalized, *slot.compute,
                                 contract_.pad_rgb.data(), error))
            return HV_ERR_INVALID_ARGUMENT;
        device_->convert_packing(slot.normalized, slot.prepared_input,
            contract_.output_elempack, contract_.cast_type_to, *slot.compute, option_);
        if (slot.compute->submit_and_wait() != 0) {
            unproven = true;
            error = "ncnn repeated observation preprocessing failed"; return HV_ERR_INTERNAL;
        }
        if (slot.compute->reset() != 0) {
            unproven = true;
            error = "ncnn repeated observation command reset failed"; return HV_ERR_INTERNAL;
        }
    }
    pending = true;
#if defined(HV_ANDROID_GPU_GATE)
    if (gate_mode_) {
        if (!ReleaseActiveRole(consumer, error)) return HV_ERR_INTERNAL;
        released = true;
        pending = false;
        const auto retired = bridge_->RetireConsumer(consumer, gpu::CompletionProof::GpuQuiescent);
        if (retired != gpu::SlotResult::Ok) {
            error = "Gate ncnn AHB retirement failed"; return HV_ERR_INTERNAL;
        }
        lease_guard.release();
        return HV_OK;
    }
#endif
    // The preceding preprocessing or inference command has completed/reset.
    *slot.extractor = *slot.pristine_extractor;
    const auto delivery = DeliverInput(*slot.extractor, contract_.input_blob.c_str(),
        slot.prepared_input, contract_.output_blobs == std::vector<std::string>{"cls", "bbox"});
    if (delivery == InputDeliveryResult::InvalidTensor) {
        error = "RTMDet ncnn extractor input is not 3-channel FP16 pack1";
        return HV_ERR_INTERNAL;
    }
    if (delivery == InputDeliveryResult::Rejected) {
        error = "ncnn model rejected explicit input tensor"; return HV_ERR_MODEL_LOAD;
    }
    for (size_t i = 0; i < contract_.output_blobs.size(); ++i) {
        slot.gpu_outputs[i].release();
        if (slot.extractor->extract(contract_.output_blobs[i].c_str(), slot.gpu_outputs[i], *slot.compute) != 0 ||
            slot.gpu_outputs[i].empty()) {
            error = "ncnn Vulkan output blob extraction failed: " + contract_.output_blobs[i];
            return HV_ERR_INTERNAL;
        }
        device_->convert_packing(slot.gpu_outputs[i], slot.fp32_outputs[i], 1, 1, *slot.compute, option_);
        DenseOutputLayout gpu_layout;
        if (slot.fp32_outputs[i].empty() || slot.fp32_outputs[i].elempack != 1 ||
            slot.fp32_outputs[i].elembits() != 32 ||
            !DescribeDenseOutput(slot.fp32_outputs[i].dims, slot.fp32_outputs[i].w,
                                 slot.fp32_outputs[i].h, slot.fp32_outputs[i].d,
                                 slot.fp32_outputs[i].c, slot.fp32_outputs[i].cstep,
                                 output_byte_limits_[i], gpu_layout) ||
            !ValidateDenseDownload(gpu_layout, slot.fp32_outputs[i].total())) {
            error = "ncnn output shape/dtype exceeds declared bounds: " + contract_.output_blobs[i];
            return HV_ERR_MODEL_LOAD;
        }
        slot.compute->record_download(slot.fp32_outputs[i], slot.cpu_outputs[i], option_);
    }
    if (slot.compute->submit_and_wait() != 0) {
        unproven = true;
        pending = false;
        error = "ncnn GPU inference/download failed"; return HV_ERR_INTERNAL;
    }
    pending = false;
    if (slot.compute->reset() != 0) {
        unproven = true;
        error = "ncnn GPU inference command reset failed"; return HV_ERR_INTERNAL;
    }
    pending = false;
    for (size_t i = 0; i < contract_.output_blobs.size(); ++i) {
        const auto& mat = slot.cpu_outputs[i];
        DenseOutputLayout layout;
        if (mat.empty() || mat.elemsize != sizeof(float) ||
            !DescribeDenseOutput(mat.dims, mat.w, mat.h, mat.d, mat.c, mat.cstep,
                                 output_byte_limits_[i], layout) ||
            !ValidateDenseDownload(layout, mat.total())) {
            error = "ncnn downloaded output violates shape/dtype bound"; return HV_ERR_MODEL_LOAD;
        }
        const size_t values = layout.logical_bytes / sizeof(float);
        if (slot.dense_outputs[i].size() < values) slot.dense_outputs[i].resize(values);
        if (!CompactDenseFp32(reinterpret_cast<const float*>(mat.data), layout,
                              slot.dense_outputs[i].data(), slot.dense_outputs[i].size())) {
            error = "ncnn downloaded output has invalid channel stride"; return HV_ERR_MODEL_LOAD;
        }
        auto& view = outputs[i];
        view = {};
        view.struct_size = sizeof(view); view.api_version = HV_PLUGIN_API_V1;
        view.name = contract_.output_blobs[i].c_str();
        view.element_type = 1;
        view.rank = layout.rank;
        std::copy(layout.dimensions.begin(), layout.dimensions.end(), view.dimensions);
        view.data = slot.dense_outputs[i].data();
        view.byte_count = layout.logical_bytes;
    }
    count = static_cast<uint32_t>(contract_.output_blobs.size());
    error.clear();
    consumer.role_owner = this;
    consumer.complete_role = &AndroidSession::CompleteRoleCallback;
    lease_guard.release(); // The observation worker calls FinishObservation after all roles.
    return HV_OK;
}

bool AndroidSession::CompleteRoleCallback(void* owner, gpu::ConsumerFrame& frame,
                                           bool final_role, std::string& error) noexcept {
    auto& session = *static_cast<AndroidSession*>(owner);
    return final_role ? session.FinishObservation(frame, error)
                      : session.YieldObservation(frame, error);
}

bool AndroidSession::ReleaseActiveRole(gpu::ConsumerFrame& frame, std::string& error) noexcept {
    if (active_consumer_ != &frame || !frame.claimed || frame.token.index >= slots_.size() ||
        frame.token.index != active_token_.index ||
        frame.token.generation != active_token_.generation ||
        frame.token.frame_id != active_token_.frame_id ||
        frame.metadata.generation != generation_.generation ||
        frame.ahb_buffer != generation_.retained_ahb[frame.token.index]) {
        terminal_gpu_fault_ = true;
        if (active_consumer_ && active_consumer_->claimed)
            bridge_->QuarantineConsumer(*active_consumer_);
        if (active_consumer_ != &frame && frame.claimed)
            bridge_->QuarantineConsumer(frame);
        active_consumer_ = nullptr;
        error = "ncnn observation lease identity changed before GPU release";
        return false;
    }
    auto& slot = *slots_[frame.token.index];
    slot.compute->record_release_android_hardware_buffer(slot.image,
        device_->info.compute_queue_family_index(), VK_QUEUE_FAMILY_EXTERNAL_KHR);
    if (slot.compute->submit_and_wait() != 0 || slot.compute->reset() != 0) {
        terminal_gpu_fault_ = true;
        bridge_->QuarantineConsumer(frame);
        active_consumer_ = nullptr;
        error = "ncnn observation release could not establish external ownership";
        return false;
    }
    active_consumer_ = nullptr;
    frame.ncnn_role_complete = true;
    error.clear(); return true;
}

bool AndroidSession::YieldObservation(gpu::ConsumerFrame& frame, std::string& error) noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    return ReleaseActiveRole(frame, error);
}

bool AndroidSession::FinishObservation(gpu::ConsumerFrame& frame, std::string& error) noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!ReleaseActiveRole(frame, error)) return false;
    const auto retired = bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent);
    if (retired != gpu::SlotResult::Ok) {
        error = "ncnn observation lease retirement failed"; return false;
    }
    error.clear(); return true;
}

HV_Result AndroidSession::Info(HV_BackendSessionInfoV1& info) const {
    info = {};
    info.struct_size = sizeof(info); info.api_version = HV_PLUGIN_API_V1;
    std::strncpy(info.requested, "backend.ncnn.vulkan", sizeof(info.requested) - 1);
    std::strncpy(info.actual, "backend.ncnn.vulkan", sizeof(info.actual) - 1);
    info.accelerated = 1;
    return HV_OK;
}
void AndroidSession::RetireUnsubmitted(void* opaque_slot) noexcept {
    if (!opaque_slot || !bridge_) return;
    auto& lease = *static_cast<gpu::ConsumerFrame*>(opaque_slot);
    if (!lease.claimed) return;
    if (active_consumer_ == &lease && !lease.role_owner) {
        std::string ignored;
        FinishObservation(lease, ignored);
        return;
    }
    const auto wait = [](void*, gpu::SyncFd& fd) noexcept { return WaitProducerFd(fd); };
    const auto retired = gpu::RetireUnsubmittedConsumer(*bridge_, lease, wait, nullptr);
    if (retired != gpu::SlotResult::Ok) {
        terminal_gpu_fault_ = true;
    }
}
}
#endif
