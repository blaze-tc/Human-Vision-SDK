#include "host/runtime_host.h"
#include <exception>
#include <algorithm>

namespace humanvision::runtime {
bool RuntimeHost::Start(std::shared_ptr<const PluginModule> module, const HV_PipelineConfigV1& config,
        const HV_HostServicesV1& services, std::string& error) {
    return StartPool(std::move(module),{config},services,error);
}
bool RuntimeHost::StartPool(std::shared_ptr<const PluginModule> module,const std::vector<HV_PipelineConfigV1>& configs,
        const HV_HostServicesV1& services,std::string& error) {
    if (!module || module->api.type != HV_PLUGIN_PIPELINE || configs.empty() || configs.size()>3 ||
        services.struct_size < sizeof(services) || services.api_version != HV_PLUGIN_API_V1) {
        error = "Invalid pipeline configuration or plugin capabilities"; return false;
    }
    for(const auto& config:configs)if(config.struct_size<sizeof(config)||config.api_version!=HV_PLUGIN_API_V1||
        config.max_bodies<1||config.max_bodies>static_cast<int>(module->api.max_people)||config.max_bodies!=configs[0].max_bodies) {
        error="Invalid or inconsistent pipeline pool configuration";return false;
    }
    const auto destroy = [module](void* p) { if (p) { try { module->api.pipeline->destroy(p); } catch (...) {} } };
    std::vector<std::unique_ptr<void,decltype(destroy)>> candidates;
    try {candidates.reserve(configs.size());}catch(...){error="Cannot allocate pipeline pool";return false;}
    for(const auto& config:configs) {
    char message[1024]{};
    HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, message, sizeof(message)};
    void* replacement = nullptr;
    try {
        const auto status = module->api.pipeline->create(&config, &services, &replacement, &buffer);
        if (status != HV_OK || !replacement) {
            if (replacement) {
                void* failed_instance = replacement;
                replacement = nullptr;
                destroy(failed_instance);
            }
            message[sizeof(message)-1] = 0;
            error = *message ? message : "Pipeline instance creation failed"; return false;
        }
    } catch (...) {
        if (replacement) { try { module->api.pipeline->destroy(replacement); } catch (...) {} }
        error = "Pipeline creation raised an exception across C ABI"; return false;
    }
    candidates.emplace_back(replacement,destroy);
    }
    std::unique_ptr<LatestFrameSlot> new_slot;
    try { new_slot = std::make_unique<LatestFrameSlot>(); }
    catch (...) { error = "Cannot allocate frame slot"; return false; }
    Stop();
    std::unique_lock<std::mutex> lock(lifecycle_mutex_);
    module_ = std::move(module); max_bodies_ = configs[0].max_bodies;
    for(size_t i=0;i<candidates.size();++i)instances_[i]=candidates[i].release();
    worker_count_=uint32_t(configs.size());active_workers_=0;late_drops_=0;
    last_submitted_frame_=last_submitted_time_=-1;sequence_=0;
    slot_ = std::move(new_slot);
    { std::lock_guard<std::mutex> result_lock(result_mutex_); has_result_ = false; result_ = {}; diagnostics_={}; error_.clear(); }
    running_ = true;
    try { for(uint32_t i=0;i<configs.size();++i)workers_[i]=std::thread(&RuntimeHost::Run,this,i); }
    catch (...) {
        lock.unlock();Stop();
        error = "Cannot start pipeline worker"; return false;
    }
    error.clear(); return true;
}

bool RuntimeHost::Submit(const HV_VideoFrame& frame, std::string& error) {
    return Submit(frame,nullptr,0,0,error);
}
bool RuntimeHost::Submit(const HV_VideoFrame& frame,const HV_RegionOfInterestV1* rois,uint32_t count,int64_t revision,std::string& error) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (!running_) { error = "Runtime Host is stopped"; return false; }
    if(count>16||(count&&!rois)){error="Invalid ROI count";return false;}
    if(worker_count_>1&&(frame.frame_id<=last_submitted_frame_||frame.timestamp_us<=last_submitted_time_)){
        error="Pipeline pool requires increasing source frame and timestamp";return false;
    }
    FrameSubmitStatus status;
    if(!slot_->Submit(frame,status,error))return false;
    last_submitted_frame_=frame.frame_id;last_submitted_time_=frame.timestamp_us;
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
    return (slot_ ? slot_->dropped_frames() : 0)+late_drops_.load();
}

std::string RuntimeHost::LastError() const {
    std::lock_guard<std::mutex> lock(result_mutex_); return error_;
}

PipelineDiagnostics RuntimeHost::Diagnostics() const {
    std::lock_guard<std::mutex> lock(result_mutex_);return diagnostics_;
}

void RuntimeHost::Stop() {
    { std::lock_guard<std::mutex> lock(lifecycle_mutex_); running_ = false; if (slot_) slot_->Stop(); }
    for(auto& worker:workers_)if(worker.joinable())worker.join();
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    for(auto& instance:instances_)if(instance){try {module_->api.pipeline->destroy(instance);}catch(...){}instance=nullptr;}
    worker_count_=0;active_workers_=0;
    module_.reset();
    { std::lock_guard<std::mutex> result_lock(result_mutex_); has_result_ = false; diagnostics_={}; }
}

void RuntimeHost::Run(uint32_t index) {
    FrameBuffer frame;
    auto* instance=instances_[index];
    while (slot_->WaitTake(frame)) {
        Metadata metadata;
        {std::lock_guard<std::mutex> lock(lifecycle_mutex_);metadata=metadata_[uint64_t(frame.frame_id)%64];}
        if(metadata.frame!=frame.frame_id||metadata.time!=frame.timestamp_us)continue;
        ++active_workers_;
        struct BusyReset {std::atomic<uint32_t>& value;~BusyReset(){--value;}} reset{active_workers_};
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
        try { status = module_->api.pipeline->process(instance, &input, &output, &buffer); }
        catch (...) { std::lock_guard<std::mutex> lock(result_mutex_); error_ = "Pipeline processing raised an exception across C ABI"; continue; }
        message[sizeof(message)-1] = 0;
        if (status != HV_OK || output.body_count > static_cast<uint32_t>(max_bodies_) || output.hand_count > HV_MAX_PEOPLE * 2) {
            std::lock_guard<std::mutex> lock(result_mutex_);
            error_ = *message ? message : "Pipeline returned an error or invalid output count"; continue;
        }
        next.source_frame_id = frame.frame_id; next.source_timestamp_us = frame.timestamp_us;
        next.width = frame.width; next.height = frame.height;
        next.body_count = output.body_count; next.hand_count = output.hand_count;
        next.preprocess_ms = output.preprocess_ms; next.inference_ms = output.inference_ms; next.postprocess_ms = output.postprocess_ms;
        PipelineDiagnostics diagnostics{};CopyPipelineDiagnostics(instance,diagnostics);
        diagnostics.accepted_detection_count=next.body_count;
        std::lock_guard<std::mutex> lock(result_mutex_);
        if(worker_count_>1&&has_result_&&(next.source_frame_id<=result_.source_frame_id||next.source_timestamp_us<=result_.source_timestamp_us)){
            ++late_drops_;continue;
        }
        next.sequence=++sequence_;
        result_ = next;diagnostics_=diagnostics;result_revision_=metadata.revision;has_result_ = true; error_.clear();
    }
}
}
