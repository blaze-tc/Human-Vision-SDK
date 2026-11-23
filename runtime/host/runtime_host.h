#pragma once
#include "host/plugin_registry.h"
#include "core/latest_frame_slot.h"
#include "common/frame_envelope.h"
#include "common/pipeline_diagnostics.h"
#include <thread>
#include <array>
#include <atomic>
#include <vector>

namespace humanvision::runtime {
/* Start/Stop belong to one control thread; Submit and CopyLatest are thread-safe.
 * Stop joins inference and therefore is not a per-frame operation. */
class RuntimeHost {
public:
    ~RuntimeHost() { Stop(); }
    bool Start(std::shared_ptr<const PluginModule>, const HV_PipelineConfigV1&, const HV_HostServicesV1&, std::string& error);
    // Explicit opt-in for independent/stateless pipeline instances. Config strings
    // are borrowed only during creation. One pending latest frame serves all workers.
    bool StartPool(std::shared_ptr<const PluginModule>, const std::vector<HV_PipelineConfigV1>&,
                   const HV_HostServicesV1&, std::string& error);
    bool Submit(const HV_VideoFrame&, std::string& error);
    bool Submit(const HV_VideoFrame&, const HV_RegionOfInterestV1*, uint32_t count, int64_t revision, std::string& error);
    // The submitted frame supplies the copied image layout and frame ID.
    // Source clock provenance, identity, epochs and transforms come from the producer.
    bool Submit(const HV_VideoFrame&, const FrameEnvelope&, const HV_RegionOfInterestV1*,
                uint32_t count, std::string& error);
    bool Busy() const { const auto n=worker_count_.load();return n&&active_workers_.load()>=n; }
    uint32_t ActiveWorkers() const { return active_workers_.load(); }
    uint32_t WorkerCount() const { return worker_count_.load(); }
    bool CopyLatest(HV_ObservationFrameV1&) const;
    bool CopyLatest(HV_ObservationFrameV1&,int64_t& revision) const;
    bool CopyLatest(HV_ObservationFrameV1&, FrameEnvelope&) const;
    int64_t DroppedFrames() const;
    std::string LastError() const;
    PipelineDiagnostics Diagnostics() const;
    void Stop();
private:
    struct FrameJobMetadata {
        FrameEnvelope envelope;
        std::array<HV_RegionOfInterestV1, 16> rois{};
        uint32_t roi_count = 0;
    };
    using FrameJobSlot = LatestFrameSlotWithMetadata<FrameJobMetadata>;

    void Run(uint32_t index);
    mutable std::mutex lifecycle_mutex_, result_mutex_;
    std::shared_ptr<const PluginModule> module_;
    std::array<void*,3> instances_{};
    std::unique_ptr<FrameJobSlot> slot_;
    std::array<std::thread,3> workers_;
    bool running_ = false, has_result_ = false;
    int max_bodies_ = 0;
    HV_ObservationFrameV1 result_{};
    FrameEnvelope result_envelope_;
    PipelineDiagnostics diagnostics_{};
    std::string error_;
    std::atomic<uint32_t> active_workers_{0},worker_count_{0};
    std::atomic<int64_t> late_drops_{0};
    int64_t last_submitted_frame_=-1,last_submitted_time_=-1,sequence_=0;
};
}
