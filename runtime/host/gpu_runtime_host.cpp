#include "host/gpu_runtime_host.h"
#include <chrono>
#include <cstring>
#include <exception>
#include <thread>
#if defined(__ANDROID__)
#include <poll.h>
#endif

namespace humanvision::runtime {
namespace {
bool WaitProducer(void*, gpu::SyncFd& fd) noexcept {
    if (!fd.HasPayload()) return false;
    if (fd.State() == gpu::SyncPayloadState::AlreadySignaled) return true;
#if defined(__ANDROID__)
    pollfd descriptor{fd.Get(), POLLIN, 0};
    return poll(&descriptor, 1, 5000) == 1 && (descriptor.revents & POLLIN) != 0;
#else
    return false;
#endif
}
std::string Message(const char* text, const char* fallback) {
    return text && *text ? text : fallback;
}
}

gpu::SlotResult BridgeGpuConsumerSource::Claim(gpu::ConsumerFrame& frame) noexcept {
    return bridge_ && lease_active_.load(std::memory_order_acquire)
        ? bridge_->ClaimConsumer(frame) : gpu::SlotResult::NoReady;
}
gpu::SlotResult BridgeGpuConsumerSource::ClaimDropped(gpu::ConsumerFrame& frame) noexcept {
    return bridge_ && lease_active_.load(std::memory_order_acquire)
        ? bridge_->ClaimDropped(frame) : gpu::SlotResult::NoReady;
}
gpu::SlotResult BridgeGpuConsumerSource::Retire(gpu::ConsumerFrame& frame) noexcept {
    return bridge_ ? bridge_->RetireConsumer(frame, gpu::CompletionProof::GpuQuiescent) : gpu::SlotResult::Closed;
}
void BridgeGpuConsumerSource::Quarantine(gpu::ConsumerFrame& frame) noexcept {
    if (bridge_) bridge_->QuarantineConsumer(frame);
}
bool BridgeGpuConsumerSource::DrainUnsubmitted(gpu::ConsumerFrame& frame) noexcept {
    return bridge_ && gpu::RetireUnsubmittedConsumer(*bridge_, frame, WaitProducer, nullptr) == gpu::SlotResult::Ok;
}
uint64_t BridgeGpuConsumerSource::Generation() const noexcept { return bridge_ ? bridge_->Generation() : 0; }

bool GpuRuntimeHost::Start(std::shared_ptr<const GpuPluginModuleV3> module,
                           const HV_HostServicesV3& services, const HV_PipelineConfigV1& config,
                           std::string& error) {
    if (!module || module->api.v1.type != HV_PLUGIN_PIPELINE || !module->api.gpu_pipeline ||
        !module->api.gpu_pipeline->create || !module->api.gpu_pipeline->destroy ||
        !module->api.gpu_pipeline->process_gpu || config.struct_size < sizeof(config) ||
        config.api_version != HV_PLUGIN_API_V1 || config.max_bodies < 1 ||
        config.max_bodies > static_cast<int>(module->api.v1.max_people)) {
        error = "Invalid V3 GPU pipeline configuration"; return false;
    }
    Stop();
    char text[1024]{};
    HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, text, sizeof(text)};
    void* created = nullptr;
    HV_Result status = HV_ERR_INTERNAL;
    try { status = module->api.gpu_pipeline->create(&config, &services, &created, &buffer); }
    catch (...) {
        if (created) { try { module->api.gpu_pipeline->destroy(created); } catch (...) {} }
        error = "V3 GPU pipeline creation threw across C ABI"; return false;
    }
    text[sizeof(text)-1] = 0;
    if (status != HV_OK || !created) {
        if (created) { try { module->api.gpu_pipeline->destroy(created); } catch (...) {} }
        error = Message(text, "V3 GPU pipeline creation failed"); return false;
    }
    module_ = std::move(module); instance_ = created; capacity_ = config.max_bodies;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        latest_ = {}; result_revision_ = 0; sequence_ = 0; has_result_ = false; error_.clear();
    }
    running_ = true;
    try { worker_ = std::thread(&GpuRuntimeHost::Run, this); }
    catch (...) {
        running_ = false; module_->api.gpu_pipeline->destroy(instance_); instance_ = nullptr;
        module_.reset(); error = "Cannot start V3 GPU worker"; return false;
    }
    error.clear(); return true;
}

bool GpuRuntimeHost::CopyLatest(HV_ObservationFrameV1& out, int64_t& revision) const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!has_result_) return false;
    out = latest_; revision = result_revision_; return true;
}
std::string GpuRuntimeHost::LastError() const {
    std::lock_guard<std::mutex> lock(result_mutex_); return error_;
}
void GpuRuntimeHost::Stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    if (instance_) { try { module_->api.gpu_pipeline->destroy(instance_); } catch (...) {} instance_ = nullptr; }
    module_.reset();
    std::lock_guard<std::mutex> lock(result_mutex_); has_result_ = false;
}

void GpuRuntimeHost::Run() {
    while (running_) {
        gpu::ConsumerFrame frame;
        const auto dropped = source_.ClaimDropped(frame);
        if (dropped == gpu::SlotResult::Ok) {
            if (!source_.DrainUnsubmitted(frame) && frame.claimed) source_.Quarantine(frame);
            continue;
        }
        const auto claimed = source_.Claim(frame);
        if (claimed != gpu::SlotResult::Ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        const auto revision = revision_.load();
        const auto generation = frame.metadata.generation;
        if (!frame.claimed || !frame.ahb_buffer || !generation ||
            frame.token.generation != generation ||
            frame.token.frame_id != frame.metadata.frame_id ||
            generation != source_.Generation() || frame.metadata.timestamp_us < 0) {
            if (frame.claimed && !source_.DrainUnsubmitted(frame) && frame.claimed) source_.Quarantine(frame);
            std::lock_guard<std::mutex> lock(result_mutex_);
            error_ = "GPU bridge claimed an invalid or stale frame lease";
            continue;
        }
        HV_ObservationFrameV1 next{};
        next.struct_size = sizeof(next); next.api_version = HV_PLUGIN_API_V1;
        HV_GpuFrameRefV1 input{sizeof(input), HV_GPU_FRAME_API_V1, &frame,
            static_cast<int32_t>(source_.Width()), static_cast<int32_t>(source_.Height()),
            static_cast<int64_t>(frame.metadata.frame_id), frame.metadata.timestamp_us,
            generation, HV_GPU_IMAGE_RGBA8_UNORM, 0};
        char text[1024]{}; HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, text, sizeof(text)};
        HV_Result status = HV_ERR_INTERNAL;
        try { status = module_->api.gpu_pipeline->process_gpu(instance_, &input, &next, &buffer); }
        catch (...) { std::strncpy(text, "V3 GPU process threw across C ABI", sizeof(text)-1); }
        text[sizeof(text)-1] = 0;
        bool retired = !frame.claimed;
        if (frame.claimed) {
            if (status == HV_OK && !frame.role_owner && !frame.producer_fd.HasPayload() && frame.ncnn_role_complete)
                retired = source_.Retire(frame) == gpu::SlotResult::Ok;
            else if (status != HV_OK) retired = source_.DrainUnsubmitted(frame);
            if (!retired && frame.claimed) source_.Quarantine(frame);
        }
        if (status != HV_OK || !retired || next.body_count > static_cast<uint32_t>(capacity_) ||
            next.hand_count > HV_MAX_PEOPLE*2 || generation != source_.Generation()) {
            std::lock_guard<std::mutex> lock(result_mutex_);
            error_ = !retired ? "V3 GPU completion unproven; source generation quarantined" :
                status != HV_OK ? Message(text, "V3 GPU processing failed") :
                "V3 GPU output count or source generation invalid";
            continue;
        }
        next.sequence = ++sequence_;
        next.source_frame_id = input.frame_id; next.source_timestamp_us = input.timestamp_us;
        next.width = input.width; next.height = input.height;
        std::lock_guard<std::mutex> lock(result_mutex_);
        latest_ = next; result_revision_ = revision; has_result_ = true; error_.clear();
    }
}

GpuSourceLeaseCoordinator& GpuSourceLeaseCoordinator::Instance() noexcept {
    static GpuSourceLeaseCoordinator coordinator;
    return coordinator;
}
bool GpuSourceLeaseCoordinator::Begin(void* owner,void* texture,BeginFn begin,EndFn end,
                                      ActiveFn active,std::string& error) {
    if (!owner || !texture || !begin || !end || !active) {
        error="Invalid Android GPU source lease request";return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (owner_) {error="Android GPU producer source lease is already owned by a runtime";return false;}
    if (!begin(texture)) {error="Android Vulkan source lease unavailable";return false;}
    owner_=owner;end_=end;active_=active;
    active_(owner_,true);
    error.clear();return true;
}
bool GpuSourceLeaseCoordinator::End(void* owner,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!owner_ || owner_!=owner) {
        error="Runtime does not own the Android GPU source lease";return false;
    }
    active_(owner_,false);
    end_();
    owner_=nullptr;end_=nullptr;active_=nullptr;
    error.clear();return true;
}
bool GpuSourceLeaseCoordinator::Owns(void* owner) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);return owner && owner_==owner;
}
gpu::BridgeResult GpuSourceLeaseCoordinator::Prepare(void* owner,
    const HV_AndroidGpuSubmissionV1& submission,void** out,PrepareFn prepare,
    DimensionsFn dimensions) noexcept {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if (!lock.owns_lock()) return gpu::BridgeResult::Busy;
    if (!owner || owner_!=owner || !prepare || !dimensions) return gpu::BridgeResult::Closed;
    const auto result=prepare(submission,out);
    if (result==gpu::BridgeResult::Ok)
        dimensions(owner,static_cast<uint32_t>(submission.width),static_cast<uint32_t>(submission.height));
    return result;
}
}
