#pragma once
#include "host/plugin_registry.h"
#include "core/latest_frame_slot.h"
#include <thread>

namespace humanvision::runtime {
/* Start/Stop belong to one control thread; Submit and CopyLatest are thread-safe.
 * Stop joins inference and therefore is not a per-frame operation. */
class RuntimeHost {
public:
    ~RuntimeHost() { Stop(); }
    bool Start(std::shared_ptr<const PluginModule>, const HV_PipelineConfigV1&, const HV_HostServicesV1&, std::string& error);
    bool Submit(const HV_VideoFrame&, std::string& error);
    bool CopyLatest(HV_ObservationFrameV1&) const;
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
};
}
