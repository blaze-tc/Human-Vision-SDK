#if defined(__ANDROID__) && defined(HV_ANDROID_GPU_GATE)
#include "gpu/android/unity_vulkan_plugin.h"
#include "plugins/backend/ncnn/ncnn_android_session.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <functional>
#include <vector>
#include <poll.h>
#include <cerrno>

namespace {
class Gate {
public:
    static Gate& Get() { static Gate gate; return gate; }
    bool Begin(void* texture, const char* contract) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable() || !texture || !contract || !*contract ||
            !humanvision::gpu::BeginUnityVulkanSourceLease(texture)) return false;
        contract_ = contract;
        stopped_.store(false);
        converted_.store(0);
        restart_ready_.store(false);
        restart_verified_.store(false);
        error_.clear();
        worker_ = std::thread([this] { Work(); });
        return true;
    }
    void End() {
        stopped_.store(true);
        if (worker_.joinable()) worker_.join();
        humanvision::gpu::EndUnityVulkanSourceLease();
    }
    HV_Result Submit(const HV_AndroidGpuSubmissionV1& frame, void** data, int* event_id) {
        if (!data || !event_id) return HV_ERR_INVALID_ARGUMENT;
        *event_id = 0;
        const auto result = humanvision::gpu::PrepareUnityVulkanFrame(frame, data);
        if (result == humanvision::gpu::BridgeResult::Busy && *data) *event_id = 1;
        return humanvision::gpu::AndroidBridgeResultCode(result);
    }
    void Status(HV_AndroidGpuBridgeStatusV1& status, uint64_t& converted,
                char* error, uint32_t capacity) {
        humanvision::gpu::GetUnityVulkanProducerStatus(status);
        converted = converted_.load();
        std::string message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            message = error_;
        }
        if (message.empty()) message = humanvision::gpu::UnityVulkanProducerGateError();
        if (error && capacity) {
            const auto length = std::min<size_t>(message.size(), capacity - 1);
            std::memcpy(error, message.data(), length);
            error[length] = 0;
        }
    }
    uint32_t Probe(char* probe, uint32_t capacity) {
        const char* diagnostic = humanvision::gpu::UnityVulkanProducerGateProbe();
        const size_t length = std::strlen(diagnostic);
        if (probe && capacity) {
            const auto copied = std::min<size_t>(length, capacity - 1);
            std::memcpy(probe, diagnostic, copied);
            probe[copied] = 0;
        }
        return static_cast<uint32_t>(length + 1);
    }
    uint32_t RestartReady() const noexcept { return restart_ready_.load() ? 1u : 0u; }
    uint32_t RestartVerified() const noexcept { return restart_verified_.load() ? 1u : 0u; }
private:
    void Fail(const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = error;
        stopped_.store(true);
    }
    void Work() {
        using namespace humanvision;
        auto* bridge = gpu::UnityVulkanProducerBridge();
        while (!stopped_.load() && bridge->IsClosed())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (stopped_.load()) return;
        gpu::VulkanDeviceContext context{};
        if (!gpu::UnityVulkanProducerContext(context)) { Fail("Gate Unity Vulkan context unavailable"); return; }
        if (contract_.find("\"schema_version\"") != std::string::npos) {
            WorkPrepared(context, *bridge);
            return;
        }
        runtime::ncnn_backend::HostContext host{};
        host.unity_device = context;
        host.bridge = bridge;
        runtime::ncnn_backend::AndroidSession session;
        std::string error;
        if (!session.InitializeGate(contract_, host, error)) { Fail(error); return; }
        const auto& contract = session.GateContract();
        while (!stopped_.load()) {
            gpu::ConsumerFrame lease;
            const auto claimed = bridge->ClaimConsumer(lease);
            if (claimed == gpu::SlotResult::NoReady || claimed == gpu::SlotResult::Busy) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (claimed != gpu::SlotResult::Ok) { Fail("Gate AHB consumer claim failed"); break; }
            HV_GpuFrameRefV1 frame{};
            frame.struct_size = sizeof(frame); frame.api_version = HV_GPU_FRAME_API_V1;
            frame.opaque_slot = &lease;
            frame.width = static_cast<int>(session.GateGenerationContract().width);
            frame.height = static_cast<int>(session.GateGenerationContract().height);
            frame.frame_id = static_cast<int64_t>(lease.metadata.frame_id);
            frame.timestamp_us = lease.metadata.timestamp_us;
            frame.generation = lease.metadata.generation;
            frame.image_format = HV_GPU_IMAGE_RGBA8_UNORM;
            HV_GpuImageTransformV1 transform{};
            transform.struct_size = sizeof(transform); transform.api_version = HV_GPU_FRAME_API_V1;
            transform.source_rect_px = {0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)};
            transform.output_width = contract.width; transform.output_height = contract.height;
            transform.output_type = contract.output_type;
            transform.output_elempack = static_cast<uint32_t>(contract.output_elempack);
            transform.channel_order = contract.channel_order;
            for (int i = 0; i < 3; ++i) { transform.mean[i] = contract.mean[i]; transform.norm[i] = contract.norm[i]; }
            uint32_t count = 0;
            const auto result = session.Run(frame, transform, nullptr, 0, count, error);
            if (result != HV_OK) { Fail(error); break; }
            converted_.fetch_add(1);
        }
    }
    void WorkPrepared(const humanvision::gpu::VulkanDeviceContext& context,
                      humanvision::gpu::UnityVulkanBridge& bridge) {
        using namespace humanvision;
        try {
            const auto manifest = nlohmann::json::parse(contract_);
            const bool restart_with_pending_job = manifest.value(
                "prepared_gate_restart_during_job", false);
            if (!manifest.contains("asset_root") || !manifest.at("asset_root").is_string()) {
                Fail("Prepared gate requires extracted local detector asset_root"); return;
            }
            const std::string root = manifest.at("asset_root").get<std::string>();
            auto model = manifest.at("models").at(0);
            for (const auto& candidate : manifest.at("models"))
                if (candidate.at("role").get<std::string>() == "detector") model = candidate;
            if (model.at("role").get<std::string>() != "detector") {
                Fail("Prepared gate ModelPack has no detector role"); return;
            }
            auto input = model.at("input_contract");
            input["output_blobs"] = model.at("output_contract").at("output_blobs");
            runtime::ncnn_backend::InputContract contract;
            std::string error;
            if (!runtime::ncnn_backend::ParseInputContract(input, contract, error)) {
                Fail(error); return;
            }
            HV_PluginApiV3 plugin{};
            plugin.v1.struct_size = sizeof(plugin);
            plugin.v1.api_version = HV_PLUGIN_API_V3;
            if (HV_QueryNcnnVulkanPluginV3(HV_PLUGIN_API_V3, &plugin) != HV_OK ||
                !plugin.gpu_backend || !plugin.gpu_backend->prepared) {
                Fail("Prepared gate V3 ncnn query failed"); return;
            }
            runtime::ncnn_backend::HostContext host{};
            host.unity_device = context;
            host.bridge = &bridge;
            HV_GpuDeviceContextV1 device{};
            device.struct_size = sizeof(device); device.api_version = HV_GPU_FRAME_API_V1;
            device.host_context = &host;
            const auto identity = gpu::QueryDeviceIdentity(context);
            if (!identity.queried) { Fail("Prepared gate Unity Vulkan identity query failed"); return; }
            std::copy(identity.device_uuid.begin(), identity.device_uuid.end(), device.device_uuid);
            std::copy(identity.driver_uuid.begin(), identity.driver_uuid.end(), device.driver_uuid);
            HV_GpuBackendConfigV1 config{sizeof(config), HV_GPU_FRAME_API_V1,
                contract_.c_str(), root.c_str(), "backend.ncnn.vulkan"};
            char message[512]{};
            HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, message, sizeof(message)};
            void* instance = nullptr;
            const auto* api = plugin.gpu_backend;
            if (api->v1.create(&config, &device, &instance, &buffer) != HV_OK || !instance) {
                Fail(std::string("Prepared gate V3 backend creation failed: ") + message);
                return;
            }
            std::unique_ptr<void, std::function<void(void*)>> owner(instance,
                [api](void* value) { api->v1.destroy(value); });
            while (!stopped_.load()) {
                gpu::ConsumerFrame lease;
                const auto claimed = bridge.ClaimConsumer(lease);
                if (claimed == gpu::SlotResult::NoReady || claimed == gpu::SlotResult::Busy) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                if (claimed != gpu::SlotResult::Ok) { Fail("Prepared gate AHB claim failed"); return; }
                struct LeaseGuard {
                    gpu::UnityVulkanBridge& bridge;
                    gpu::ConsumerFrame& lease;
                    static bool WaitProducer(void*, gpu::SyncFd& fd) noexcept {
                        if (!fd.HasPayload() || fd.Get() < 0) return true;
                        pollfd pending{fd.Get(), POLLIN, 0};
                        int result;
                        do { result = poll(&pending, 1, 1000); }
                        while (result < 0 && errno == EINTR);
                        return result == 1 && (pending.revents & (POLLIN | POLLHUP)) != 0;
                    }
                    ~LeaseGuard() {
                        if (!lease.claimed) return;
                        const auto result = gpu::RetireUnsubmittedConsumer(
                            bridge, lease, &WaitProducer, nullptr);
                        if (result != gpu::SlotResult::Ok && lease.claimed)
                            bridge.QuarantineConsumer(lease);
                    }
                } lease_guard{bridge, lease};
                HV_GpuFrameRefV1 frame{};
                frame.struct_size = sizeof(frame); frame.api_version = HV_GPU_FRAME_API_V1;
                frame.opaque_slot = &lease;
                // The prepared fixture's source RenderTexture is pinned at
                // 320x320; AndroidSession also verifies the AHB generation.
                frame.width = 320;
                frame.height = 320;
                frame.frame_id = static_cast<int64_t>(lease.metadata.frame_id);
                frame.timestamp_us = lease.metadata.timestamp_us;
                frame.generation = lease.metadata.generation;
                frame.image_format = HV_GPU_IMAGE_RGBA8_UNORM;
                HV_GpuImageTransformV1 transform{};
                transform.struct_size = sizeof(transform); transform.api_version = HV_GPU_FRAME_API_V1;
                transform.source_rect_px = {0, 0, static_cast<float>(frame.width),
                    static_cast<float>(frame.height)};
                transform.output_width = contract.width; transform.output_height = contract.height;
                transform.output_type = contract.output_type;
                transform.output_elempack = contract.output_elempack;
                transform.channel_order = contract.channel_order;
                for (int i = 0; i < 3; ++i) {
                    transform.mean[i] = contract.mean[i]; transform.norm[i] = contract.norm[i];
                }
                HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
                if (api->prepared->prepare_image(instance, &frame, &transform, &ref, &buffer) != HV_OK) {
                    Fail(std::string("Prepared gate V3 prepare failed: ") + message); return;
                }
                if (prior_restart_ref_.token && !restart_with_pending_job) {
                    char stale_message[512]{};
                    HV_ErrorBufferV1 stale_error{sizeof(stale_error), HV_PLUGIN_API_V1,
                        stale_message, sizeof(stale_message)};
                    if (api->prepared->discard_prepared(instance, &prior_restart_ref_,
                                                        &stale_error) == HV_OK) {
                        Fail("Prepared gate old-generation token was accepted after restart");
                        return;
                    }
                    prior_restart_ref_ = {};
                }
                if (!lease.claimed || !lease.ncnn_role_complete || lease.role_owner ||
                    lease.producer_fd.HasPayload()) {
                    Fail("Prepared gate detector AHB handoff was not proved"); return;
                }
                std::string tensor_proof;
                if (!static_cast<runtime::ncnn_backend::AndroidSession*>(instance)
                        ->VerifyPreparedGoldenForGate(ref, tensor_proof)) {
                    Fail(tensor_proof); return;
                }
                if (restart_with_pending_job) {
                    // Keep the prepared token active while Unity requests a
                    // source restart. The AHB is already released by prepare
                    // and can be finally retired before detector inference.
                    if (bridge.RetireConsumer(lease, gpu::CompletionProof::GpuQuiescent) !=
                        gpu::SlotResult::Ok || lease.claimed) {
                        Fail("Prepared gate pending-job AHB retirement failed"); return;
                    }
                    restart_ready_.store(true);
                    const auto restart_deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
                    while (!stopped_.load() &&
                           std::chrono::steady_clock::now() < restart_deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if (!stopped_.load()) {
                        Fail("Prepared gate source restart request timed out"); return;
                    }
                    HV_TensorViewV1 pending[2]{};
                    uint32_t pending_count = 0;
                    if (api->prepared->run_prepared(instance, &ref, pending, 2,
                                                    &pending_count, &buffer) != HV_OK ||
                        pending_count != prior_direct_outputs_.size() ||
                        static_cast<runtime::ncnn_backend::AndroidSession*>(instance)
                            ->GatePreparedOutputDownloadCalls() != 2) {
                        Fail(std::string("Prepared gate outstanding job drain failed: ") + message);
                        return;
                    }
                    for (uint32_t i = 0; i < pending_count; ++i)
                        if (prior_direct_outputs_[i].size() != pending[i].byte_count ||
                            std::memcmp(prior_direct_outputs_[i].data(), pending[i].data,
                                        pending[i].byte_count) != 0) {
                            Fail("Prepared gate outstanding result changed across restart");
                            return;
                        }
                    prior_restart_ref_ = ref;
                    restart_verified_.store(true);
                    __android_log_print(ANDROID_LOG_INFO, "HumanVision",
                        "HV_PREPARED_GATE RESTART_DRAIN_PASS source_retired=1 "
                        "restart_requested_while_token_active=1 old_generation=%llu token=%llu "
                        "bounded_output_download_calls=2 %s",
                        static_cast<unsigned long long>(ref.generation),
                        static_cast<unsigned long long>(ref.token), tensor_proof.c_str());
                    return;
                }
                HV_TensorViewV1 direct[2]{};
                uint32_t direct_count = 0;
                if (api->v1.run_image(instance, &frame, &transform, direct, 2,
                                      &direct_count, &buffer) != HV_OK || direct_count != 2) {
                    Fail(std::string("Prepared gate second AHB role failed: ") + message); return;
                }
                // Tensor views borrow one session-owned output buffer. Snapshot
                // direct output before run_prepared can reuse that storage.
                std::vector<std::vector<unsigned char>> direct_bytes(direct_count);
                for (uint32_t i = 0; i < direct_count; ++i) {
                    const auto* begin = static_cast<const unsigned char*>(direct[i].data);
                    direct_bytes[i].assign(begin, begin + direct[i].byte_count);
                }
                prior_direct_outputs_ = direct_bytes;
                if (!gpu::CompleteGpuRole(lease, true, error) || lease.claimed) {
                    Fail("Prepared gate final AHB role/retirement failed: " + error); return;
                }
                HV_TensorViewV1 detached[2]{};
                uint32_t detached_count = 0;
                if (api->prepared->run_prepared(instance, &ref, detached, 2,
                                                &detached_count, &buffer) != HV_OK ||
                    detached_count != direct_count ||
                    static_cast<runtime::ncnn_backend::AndroidSession*>(instance)
                        ->GatePreparedOutputDownloadCalls() != 2) {
                    Fail(std::string("Prepared gate run after AHB retirement failed: ") + message); return;
                }
                for (uint32_t i = 0; i < detached_count; ++i)
                    if (direct_bytes[i].size() != detached[i].byte_count ||
                        std::strcmp(direct[i].name, detached[i].name) != 0 ||
                        std::memcmp(direct_bytes[i].data(), detached[i].data,
                                    direct[i].byte_count) != 0) {
                        Fail("Prepared gate source and detached detector outputs differ"); return;
                    }
                __android_log_print(ANDROID_LOG_INFO, "HumanVision",
                    "HV_PREPARED_GATE PASS frame=%lld generation=%llu token=%llu "
                    "source_retired=1 cls_bytes=%llu bbox_bytes=%llu "
                    "bounded_output_download_calls=2 %s",
                    static_cast<long long>(frame.frame_id),
                    static_cast<unsigned long long>(ref.generation),
                    static_cast<unsigned long long>(ref.token),
                    static_cast<unsigned long long>(detached[0].byte_count),
                    static_cast<unsigned long long>(detached[1].byte_count),
                    tensor_proof.c_str());
                converted_.fetch_add(1);
                return;
            }
        } catch (const std::exception& ex) { Fail(ex.what()); }
    }
    std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> stopped_{true};
    std::atomic<uint64_t> converted_{0};
    std::atomic<bool> restart_ready_{false}, restart_verified_{false};
    HV_GpuPreparedRefV1 prior_restart_ref_{};
    std::vector<std::vector<unsigned char>> prior_direct_outputs_;
    std::string contract_, error_;
};
}

extern "C" {
__attribute__((visibility("default"))) int HV_CALL HV_AndroidGpuGateBegin(void* texture, const char* contract) {
    return Gate::Get().Begin(texture, contract) ? 0 : -1;
}
__attribute__((visibility("default"))) void HV_CALL HV_AndroidGpuGateEnd() { Gate::Get().End(); }
__attribute__((visibility("default"))) int HV_CALL HV_AndroidGpuGateSubmit(
    const HV_AndroidGpuSubmissionV1* frame, void** data, int* event_id) {
    if (!frame || frame->struct_size != sizeof(*frame) || frame->api_version != HV_ANDROID_GPU_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    return Gate::Get().Submit(*frame, data, event_id);
}
__attribute__((visibility("default"))) void HV_CALL HV_AndroidGpuGateStatus(
    HV_AndroidGpuBridgeStatusV1* status, uint64_t* converted, char* error, uint32_t capacity) {
    if (!status || !converted || status->struct_size != sizeof(*status) ||
        status->api_version != HV_ANDROID_GPU_API_V1) return;
    Gate::Get().Status(*status, *converted, error, capacity);
}
__attribute__((visibility("default"))) uint32_t HV_CALL HV_AndroidGpuGateProbe(char* probe, uint32_t capacity) {
    return Gate::Get().Probe(probe, capacity);
}
__attribute__((visibility("default"))) uint32_t HV_CALL HV_AndroidGpuGateRestartReady() {
    return Gate::Get().RestartReady();
}
__attribute__((visibility("default"))) uint32_t HV_CALL HV_AndroidGpuGateRestartVerified() {
    return Gate::Get().RestartVerified();
}
}
#endif
