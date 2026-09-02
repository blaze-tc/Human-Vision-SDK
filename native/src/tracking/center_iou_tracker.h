#pragma once

#include "tracking/i_body_tracker.h"

#include <cstdint>
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

private:
    struct Track {
        int id = -1;
        Detection detection;
        float velocity_x_per_us = 0.0F;
        float velocity_y_per_us = 0.0F;
        std::int64_t timestamp_us = 0;
        int lost_frames = 0;
    };

    int max_lost_frames_ = 3;
    int next_track_id_ = 1;
    std::vector<Track> tracks_;
};

}  // namespace humanvision
