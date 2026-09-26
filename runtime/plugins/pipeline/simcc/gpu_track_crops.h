#pragma once

#include "tracking/center_iou_tracker.h"
#include "humanvision/humanvision_v2.h"

#include <array>
#include <cstdint>
#include <vector>

namespace humanvision::runtime {

struct DetectorResultMeta {
    std::int64_t frame_id = 0, capture_us = 0, arrival_us = 0, region_revision = 0;
    std::uint64_t generation = 0;
};

struct GpuCrop {
    int track_id = -1;
    Detection box{};
    float center_x = 0, center_y = 0;
    std::int64_t detector_anchor_frame_id = -1, detector_age_us = 0;
    std::uint8_t missed_detections = 0;
};

class GpuTrackCrops {
public:
    GpuTrackCrops(int capacity, int width, int height,
                  std::int64_t region_revision, std::uint64_t generation);
    bool ApplyDetection(const DetectorResultMeta& meta, const std::vector<Detection>& boxes);
    const std::vector<GpuCrop>& NextCrops(std::int64_t frame_id, std::int64_t capture_us);
    bool ApplyPose(std::int64_t frame_id, std::int64_t capture_us, int track_id,
                   const std::vector<HV_CanonicalJointV1>& joints, bool valid);
    std::int64_t PublishedJointSourceFrame(int track_id) const;
    bool NeedsReacquisition() const { return reacquisition_; }
    void InvalidateGeneration();

private:
    struct Slot {
        int id = -1;
        Detection crop{}, anchor{};
        std::int64_t anchor_frame = -1, anchor_us = 0, pose_frame = -1,
                     pose_us = 0, published_frame = -1;
        int misses = 0;
        bool pose_valid = false, pending_expiry = false;
    };
    Slot* Find(int id);
    const Slot* Find(int id) const;
    void Expire(int id);
    bool ValidBox(const Detection& box) const;

    int capacity_, width_, height_;
    std::int64_t region_revision_;
    std::uint64_t generation_;
    std::int64_t last_detection_frame_ = -1, last_pose_frame_ = -1,
                 last_pose_capture_us_ = -1;
    bool reacquisition_ = true;
    CenterIouTracker identity_{1};
    std::array<Slot, 8> slots_{};
    std::vector<GpuCrop> crops_;
    std::vector<TrackedDetection> matches_;
    std::vector<Detection> accepted_;
};

}  // namespace humanvision::runtime
