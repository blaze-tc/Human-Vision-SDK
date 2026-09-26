#pragma once

#include "humanvision/humanvision_types.h"
#include "humanvision/humanvision_v2.h"

#include <array>
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

class StatsCollectorV2 {
public:
    bool CanPublish(std::int64_t frame_id,std::uint32_t body_count,
                    std::int64_t capture_us,std::int64_t published_us,
                    std::int64_t pose_frame_id,float pose_total_ms=0.0F) const;
    // A provenance frame ID equal to the published frame proves current-image
    // pose. Empty observations pass their own source frame as provenance.
    // clock_domain_prevalidated is for the serialized GPU publication path:
    // CanPublish checked the clock before composing the public snapshot, so
    // processing time may truthfully exceed the sanity bound at commit.
    bool Publish(std::int64_t frame_id, std::uint32_t body_count,
                 std::int64_t capture_us, std::int64_t published_us,
                 std::int64_t pose_frame_id = -1, float pose_total_ms = 0.0F,
                 bool detector_keyframe = false, std::uint32_t posed_people = 0,
                 bool clock_domain_prevalidated = false);
    void Sample(std::int64_t frame_id, std::int64_t sampled_us = 0);
    HV_RuntimeStatsV2 Snapshot(std::int64_t now_us = 0,
        std::uint32_t provenance = HV_CAPTURE_PROVENANCE_UNKNOWN) const;
private:
    static constexpr std::size_t kWindow = 512;
    mutable std::mutex mutex_;
    HV_RuntimeStatsV2 stats_{};
    std::array<float,kWindow> ages_{}, pose_ages_{}, detector_ages_{}, pose_body_ms_{};
    std::array<std::int64_t,kWindow> publication_times_{};
    std::array<std::int64_t,kWindow> sample_times_{};
    std::size_t next_ = 0, count_ = 0, pose_next_ = 0, pose_count_ = 0,
                detector_next_ = 0, detector_count_ = 0, pose_body_next_ = 0,
                pose_body_count_ = 0, sample_next_ = 0, sample_count_ = 0;
    std::int64_t last_frame_ = 0, last_capture_ = 0;
    std::int64_t first_publication_ = 0, last_sample_ = 0, first_sample_ = 0;
};

}  // namespace humanvision
