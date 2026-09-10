#pragma once

#include "models/rtmdet/rtmdet_model.h"

#include <cstdint>
#include <vector>

namespace humanvision {

struct TrackedDetection {
    Detection detection;
    int track_id = -1;
};

class IBodyTracker {
public:
    virtual ~IBodyTracker() = default;
    virtual void Reset() = 0;
    virtual void Update(
        const std::vector<Detection>& detections,
        std::int64_t timestamp_us,
        std::vector<TrackedDetection>& output) = 0;
    virtual void Predict(
        std::int64_t timestamp_us,
        std::vector<TrackedDetection>& output) = 0;
    // Current-image pose observations refresh only the crop, never synthesize joints.
    virtual void ObservePose(int track_id, const Detection& crop, std::int64_t timestamp_us) = 0;
    virtual void RejectPose(int track_id) = 0;
};

}  // namespace humanvision
