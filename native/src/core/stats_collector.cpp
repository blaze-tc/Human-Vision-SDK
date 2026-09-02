#include "core/stats_collector.h"

#include <algorithm>

namespace humanvision {

StatsCollector::StatsCollector() : started_at_(std::chrono::steady_clock::now()) {
    stats_.struct_size = sizeof(HV_Stats);
}

void StatsCollector::RecordSubmitted(const bool replaced_pending) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.submitted_frames;
    if (replaced_pending) {
        ++stats_.dropped_frames;
    }
}

void StatsCollector::RecordProcessed(const StageTimings& timings) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.processed_frames;
    stats_.detection_ms = timings.detection_ms;
    stats_.pose_ms = timings.pose_ms;
    stats_.tracking_ms = timings.tracking_ms;
    stats_.total_ms = timings.total_ms;
}

HV_Stats StatsCollector::Snapshot(
    const std::int64_t authoritative_dropped_frames) const {
    std::lock_guard<std::mutex> lock(mutex_);
    HV_Stats result = stats_;
    if (authoritative_dropped_frames >= 0) {
        result.dropped_frames = authoritative_dropped_frames;
    }
    const float elapsed_seconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - started_at_)
            .count();
    if (elapsed_seconds > 0.0F) {
        result.input_fps =
            static_cast<float>(result.submitted_frames) / elapsed_seconds;
        result.inference_fps =
            static_cast<float>(result.processed_frames) / elapsed_seconds;
    }
    return result;
}

}  // namespace humanvision
