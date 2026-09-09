#pragma once

#include "humanvision/humanvision_types.h"

#include <chrono>
#include <cstdint>
#include <mutex>

namespace humanvision {

struct StageTimings {
    float detection_ms = 0.0F;
    float pose_ms = 0.0F;
    float tracking_ms = 0.0F;
    float total_ms = 0.0F;
};

class StatsCollector {
public:
    StatsCollector();

    void RecordSubmitted(bool replaced_pending);
    void RecordProcessed(const StageTimings& timings);
    void RecordTimings(const StageTimings& timings);
    HV_Stats Snapshot(std::int64_t authoritative_dropped_frames = -1) const;

private:
    mutable std::mutex mutex_;
    HV_Stats stats_{};
    std::chrono::steady_clock::time_point started_at_;
};

}  // namespace humanvision
