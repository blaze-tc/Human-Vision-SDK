#pragma once
#include "host/plugin_registry.h"
#include "core/latest_frame_slot.h"
#include <thread>
#include <array>
#include <atomic>

namespace humanvision::runtime {
/* Start/Stop belong to one control thread; Submit and CopyLatest are thread-safe.
 * Stop joins inference and therefore is not a per-frame operation. */
class RuntimeHost {
public:
    ~RuntimeHost() { Stop(); }
    bool Start(std::shared_ptr<const PluginModule>, const HV_PipelineConfigV1&, const HV_HostServicesV1&, std::string& error);
    bool Submit(const HV_VideoFrame&, std::string& error);
    bool Submit(const HV_VideoFrame&, const HV_RegionOfInterestV1*, uint32_t count, int64_t revision, std::string& error);
    bool Busy() const { return busy_.load(); }
    bool CopyLatest(HV_ObservationFrameV1&) const;
    bool CopyLatest(HV_ObservationFrameV1&,int64_t& revision) const;
    int64_t DroppedFrames() const;
    std::string LastError() const;
    void Stop();
private:
    void Run();
    mutable std::mutex lifecycle_mutex_, result_mutex_;
    std::shared_ptr<const PluginModule> module_;
    void* instance_ = nullptr;
    std::unique_ptr<LatestFrameSlot> slot_;
    std::thread worker_;
    bool running_ = false, has_result_ = false;
    int max_bodies_ = 0;
    HV_ObservationFrameV1 result_{};
    std::string error_;
    struct Metadata {int64_t frame=-1,time=0,revision=0;uint32_t count=0;std::array<HV_RegionOfInterestV1,16> rois{};};
    std::array<Metadata,64> metadata_{};
    int64_t result_revision_=0;
    std::atomic<bool> busy_{false};
};
}
