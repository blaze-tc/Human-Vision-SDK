#include "host/runtime_host.h"
#include <exception>
#include <algorithm>

namespace humanvision::runtime {
bool RuntimeHost::Start(std::shared_ptr<const PluginModule> module, const HV_PipelineConfigV1& config,
        const HV_HostServicesV1& services, std::string& error) {
    if (!module || module->api.type != HV_PLUGIN_PIPELINE || config.struct_size < sizeof(config) ||
        config.api_version != HV_PLUGIN_API_V1 || config.max_bodies < 1 || config.max_bodies > static_cast<int>(module->api.max_people) ||
        services.struct_size < sizeof(services) || services.api_version != HV_PLUGIN_API_V1) {
        error = "Invalid pipeline configuration or plugin capabilities"; return false;
    }
    char message[1024]{};
    HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, message, sizeof(message)};
    void* replacement = nullptr;
    try {
        const auto status = module->api.pipeline->create(&config, &services, &replacement, &buffer);
        if (status != HV_OK || !replacement) {
            if (replacement) {
                void* failed_instance = replacement;
                replacement = nullptr;
                module->api.pipeline->destroy(failed_instance);
            }
            message[sizeof(message)-1] = 0;
            error = *message ? message : "Pipeline instance creation failed"; return false;
        }
    } catch (...) {
        if (replacement) { try { module->api.pipeline->destroy(replacement); } catch (...) {} }
        error = "Pipeline creation raised an exception across C ABI"; return false;
    }
    const auto destroy = [module](void* p) { if (p) { try { module->api.pipeline->destroy(p); } catch (...) {} } };
    std::unique_ptr<void, decltype(destroy)> replacement_guard(replacement, destroy);
    std::unique_ptr<LatestFrameSlot> new_slot;
    try { new_slot = std::make_unique<LatestFrameSlot>(); }
    catch (...) { error = "Cannot allocate frame slot"; return false; }
    Stop();
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    module_ = std::move(module); instance_ = replacement; max_bodies_ = config.max_bodies;
    slot_ = std::move(new_slot);
    { std::lock_guard<std::mutex> result_lock(result_mutex_); has_result_ = false; result_ = {}; diagnostics_={}; error_.clear(); }
    running_ = true;
    try { worker_ = std::thread(&RuntimeHost::Run, this); }
    catch (...) {
        running_ = false; instance_ = nullptr; module_.reset();
        error = "Cannot start pipeline worker"; return false;
    }
    replacement_guard.release();
    error.clear(); return true;
}

bool RuntimeHost::Submit(const HV_VideoFrame& frame, std::string& error) {
    return Submit(frame,nullptr,0,0,error);
}
bool RuntimeHost::Submit(const HV_VideoFrame& frame,const HV_RegionOfInterestV1* rois,uint32_t count,int64_t revision,std::string& error) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (!running_) { error = "Runtime Host is stopped"; return false; }
    if(count>16||(count&&!rois)){error="Invalid ROI count";return false;}
    FrameSubmitStatus status;
    if(!slot_->Submit(frame,status,error))return false;
    auto& meta=metadata_[uint64_t(frame.frame_id)%64];meta.frame=frame.frame_id;meta.time=frame.timestamp_us;meta.revision=revision;meta.count=count;
    if(count)std::copy_n(rois,count,meta.rois.begin());return true;
}

bool RuntimeHost::CopyLatest(HV_ObservationFrameV1& out) const {
    int64_t revision;return CopyLatest(out,revision);
}
bool RuntimeHost::CopyLatest(HV_ObservationFrameV1& out,int64_t& revision) const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!has_result_) return false;
    out = result_; revision=result_revision_;return true;
}

int64_t RuntimeHost::DroppedFrames() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return slot_ ? slot_->dropped_frames() : 0;
}

std::string RuntimeHost::LastError() const {
    std::lock_guard<std::mutex> lock(result_mutex_); return error_;
}

PipelineDiagnostics RuntimeHost::Diagnostics() const {
    std::lock_guard<std::mutex> lock(result_mutex_);return diagnostics_;
}

void RuntimeHost::Stop() {
    { std::lock_guard<std::mutex> lock(lifecycle_mutex_); running_ = false; if (slot_) slot_->Stop(); }
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (instance_) { try { module_->api.pipeline->destroy(instance_); } catch (...) {} instance_ = nullptr; }
    module_.reset();
    { std::lock_guard<std::mutex> result_lock(result_mutex_); has_result_ = false; diagnostics_={}; }
}

void RuntimeHost::Run() {
    FrameBuffer frame;
    int64_t sequence = 0;
    while (slot_->WaitTake(frame)) {
        Metadata metadata;
        {std::lock_guard<std::mutex> lock(lifecycle_mutex_);metadata=metadata_[uint64_t(frame.frame_id)%64];}
        if(metadata.frame!=frame.frame_id||metadata.time!=frame.timestamp_us)continue;
        busy_=true;
        struct BusyReset {std::atomic<bool>& value;~BusyReset(){value=false;}} reset{busy_};
        HV_ObservationFrameV1 next{};
        next.struct_size = sizeof(next); next.api_version = HV_PLUGIN_API_V1;
        HV_PipelineInputV1 input{};
        input.struct_size = sizeof(input); input.api_version = HV_PLUGIN_API_V1;
        input.frame = {sizeof(HV_VideoFrame), frame.width, frame.height, frame.stride_bytes,
            frame.pixel_format, frame.frame_id, frame.timestamp_us, frame.bytes.data(), static_cast<int32_t>(frame.bytes.size())};
        input.rois=metadata.count?metadata.rois.data():nullptr;input.roi_count=metadata.count;
        HV_PipelineOutputV1 output{};
        output.struct_size = sizeof(output); output.api_version = HV_PLUGIN_API_V1;
        output.bodies = next.bodies; output.body_capacity = max_bodies_;
        output.hands = next.hands; output.hand_capacity = HV_MAX_PEOPLE * 2;
        char message[1024]{};
        HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, message, sizeof(message)};
        HV_Result status = HV_ERR_INTERNAL;
        try { status = module_->api.pipeline->process(instance_, &input, &output, &buffer); }
        catch (...) { std::lock_guard<std::mutex> lock(result_mutex_); error_ = "Pipeline processing raised an exception across C ABI"; continue; }
        message[sizeof(message)-1] = 0;
        if (status != HV_OK || output.body_count > static_cast<uint32_t>(max_bodies_) || output.hand_count > HV_MAX_PEOPLE * 2) {
            std::lock_guard<std::mutex> lock(result_mutex_);
            error_ = *message ? message : "Pipeline returned an error or invalid output count"; continue;
        }
        next.sequence = ++sequence; next.source_frame_id = frame.frame_id; next.source_timestamp_us = frame.timestamp_us;
        next.width = frame.width; next.height = frame.height;
        next.body_count = output.body_count; next.hand_count = output.hand_count;
        next.preprocess_ms = output.preprocess_ms; next.inference_ms = output.inference_ms; next.postprocess_ms = output.postprocess_ms;
        PipelineDiagnostics diagnostics{};CopyPipelineDiagnostics(instance_,diagnostics);
        diagnostics.accepted_detection_count=next.body_count;
        std::lock_guard<std::mutex> lock(result_mutex_); result_ = next;diagnostics_=diagnostics;result_revision_=metadata.revision;has_result_ = true; error_.clear();
    }
}
}
