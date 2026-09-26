#pragma once

#include "tracking/i_body_tracker.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace humanvision {

class CenterIouTracker final : public IBodyTracker {
public:
    explicit CenterIouTracker(int max_lost_frames = 3);

    void Reset() override;
    void Update(
        const std::vector<Detection>& detections,
        std::int64_t timestamp_us,
        std::vector<TrackedDetection>& output) override;
    void Predict(
        std::int64_t timestamp_us,
        std::vector<TrackedDetection>& output) override;

    void ObservePose(int track_id, const Detection& crop, std::int64_t timestamp_us) override;
    void RejectPose(int track_id) override;
    // GPU policy owns its shorter expiry bound; remove that identity immediately.
    void ForgetTrack(int track_id);
    // GPU path: bounded identity storage with reusable association scratch.
    void ReserveCapacity(std::size_t capacity);
    void UpdateBounded(const std::vector<Detection>& detections,
                       std::int64_t timestamp_us,
                       std::vector<TrackedDetection>& output,
                       std::size_t capacity);
    std::size_t ActiveTrackCount() const { return tracks_.size(); }
private:
    struct MatchCandidate {
        std::size_t track_index = 0, detection_index = 0;
        float cost = 0.0F;
        int track_id = -1;
    };
    void UpdateImpl(const std::vector<Detection>& detections,
                    std::int64_t timestamp_us,
                    std::vector<TrackedDetection>& output,
                    std::size_t capacity);
    struct Track {
        int id = -1;
        Detection detection;
        Detection detector_anchor;
        std::int64_t pose_timestamp_us = 0;
        bool pose_rejected = false;
        float velocity_x_per_us = 0.0F;
        float velocity_y_per_us = 0.0F;
        std::int64_t timestamp_us = 0;
        int lost_frames = 0;
    };

    int max_lost_frames_ = 3;
    int next_track_id_ = 1;
    std::vector<Track> tracks_;
    std::vector<MatchCandidate> candidates_;
    std::vector<std::uint8_t> matched_tracks_, matched_detections_;
};

}  // namespace humanvision
