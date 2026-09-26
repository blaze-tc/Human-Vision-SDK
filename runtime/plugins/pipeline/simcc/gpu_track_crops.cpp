#include "plugins/pipeline/simcc/gpu_track_crops.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace humanvision::runtime {
namespace {
constexpr std::int64_t kMaxDetectorAgeUs = 500000;
constexpr std::int64_t kMaxNewDetectionLagUs = 200000;
constexpr float kMinDetectionScore = .35F;
constexpr float kCropMargin = 0.30F;
float CenterX(const Detection& box) { return (box.x1 + box.x2) * .5F; }
float CenterY(const Detection& box) { return (box.y1 + box.y2) * .5F; }
}  // namespace

GpuTrackCrops::GpuTrackCrops(int capacity, int width, int height,
    std::int64_t region_revision, std::uint64_t generation)
    : capacity_(capacity), width_(width), height_(height),
      region_revision_(region_revision), generation_(generation) {
    if (capacity < 1 || capacity > 8 || width < 1 || height < 1)
        throw std::invalid_argument("GPU crop capacity/image size invalid");
    crops_.reserve(8);
    matches_.reserve(8);
    accepted_.reserve(8);
    identity_.ReserveCapacity(static_cast<std::size_t>(capacity_));
}

GpuTrackCrops::Slot* GpuTrackCrops::Find(int id) {
    for (auto& slot : slots_) if (slot.id == id) return &slot;
    return nullptr;
}
const GpuTrackCrops::Slot* GpuTrackCrops::Find(int id) const {
    for (const auto& slot : slots_) if (slot.id == id) return &slot;
    return nullptr;
}
void GpuTrackCrops::Expire(int id) {
    if (auto* slot = Find(id)) *slot = {};
    identity_.ForgetTrack(id);
    reacquisition_ = true;
}
bool GpuTrackCrops::ValidBox(const Detection& box) const {
    return std::isfinite(box.x1) && std::isfinite(box.y1) &&
           std::isfinite(box.x2) && std::isfinite(box.y2) &&
           std::isfinite(box.score) && box.score >= kMinDetectionScore &&
           box.x1 >= 0 && box.y1 >= 0 && box.x2 <= width_ &&
           box.y2 <= height_ && box.x2 > box.x1 && box.y2 > box.y1;
}

bool GpuTrackCrops::ApplyDetection(const DetectorResultMeta& meta,
                                    const std::vector<Detection>& boxes) {
    if (meta.generation != generation_ || meta.region_revision != region_revision_ ||
        meta.frame_id <= last_detection_frame_ || meta.capture_us < 0 ||
        meta.arrival_us < meta.capture_us ||
        meta.arrival_us - meta.capture_us > kMaxDetectorAgeUs)
        return false;
    const bool can_discover = meta.arrival_us - meta.capture_us <= kMaxNewDetectionLagUs;
    if (!can_discover && std::none_of(slots_.begin(), slots_.end(),
            [](const Slot& slot) { return slot.id >= 0; })) return false;
    last_detection_frame_ = meta.frame_id;
    accepted_.clear();
    for (const auto& box : boxes) {
        if (!ValidBox(box)) continue;
        if (accepted_.size() == static_cast<std::size_t>(capacity_)) break;
        accepted_.push_back(box);
    }
    identity_.UpdateBounded(accepted_, meta.capture_us, matches_,
                            static_cast<std::size_t>(capacity_));
    bool expired_or_changed = false;
    for (auto& slot : slots_) {
        if (slot.id < 0) continue;
        const bool matched = std::any_of(matches_.begin(), matches_.end(),
            [&slot](const TrackedDetection& match) { return match.track_id == slot.id; });
        if (!matched) {
            ++slot.misses;
            if (slot.misses >= 2) {
                expired_or_changed = true;
                const bool selected = std::any_of(crops_.begin(), crops_.end(),
                    [&slot](const GpuCrop& crop) { return crop.track_id == slot.id; });
                if (selected) slot.pending_expiry = true;
                else Expire(slot.id);
            }
        }
    }
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        const auto& match = matches_[i];
        if (match.track_id < 0) continue;
        auto* slot = Find(match.track_id);
        if (!slot) {
            if (!can_discover) {
                identity_.ForgetTrack(match.track_id);
                continue;
            }
            auto free = std::find_if(slots_.begin(), slots_.begin() + capacity_,
                [](const Slot& candidate) { return candidate.id < 0; });
            if (free == slots_.begin() + capacity_) {
                identity_.ForgetTrack(match.track_id);
                continue;
            }
            slot = &*free;
            *slot = {};
            slot->id = match.track_id;
            slot->crop = match.detection;
        }
        slot->anchor = match.detection;
        slot->anchor_frame = meta.frame_id;
        slot->anchor_us = meta.capture_us;
        slot->misses = 0;
        slot->pending_expiry = false;
        // A detector captured before the latest valid pose is association only.
        if (slot->pose_frame < 0 || meta.frame_id > slot->pose_frame)
            slot->crop = match.detection;
    }
    reacquisition_ = expired_or_changed || matches_.empty();
    return true;
}

const std::vector<GpuCrop>& GpuTrackCrops::NextCrops(
    std::int64_t frame_id, std::int64_t capture_us) {
    crops_.clear();
    if (frame_id <= last_pose_frame_) return crops_;
    last_pose_frame_ = frame_id;
    last_pose_capture_us_ = capture_us;
    for (auto& slot : slots_) {
        if (slot.id < 0) continue;
        slot.published_frame = -1;
        slot.pose_valid = false;
        if (slot.pending_expiry || capture_us < slot.anchor_us ||
            capture_us - slot.anchor_us > kMaxDetectorAgeUs ||
            slot.misses >= 2) {
            Expire(slot.id);
            continue;
        }
        crops_.push_back({slot.id, slot.crop, CenterX(slot.crop), CenterY(slot.crop),
                          slot.anchor_frame, capture_us - slot.anchor_us,
                          static_cast<std::uint8_t>(slot.misses)});
    }
    if (crops_.empty()) reacquisition_ = true;
    return crops_;
}

bool GpuTrackCrops::ApplyPose(std::int64_t frame_id, std::int64_t capture_us,
    int track_id, const std::vector<HV_CanonicalJointV1>& joints, bool valid) {
    auto* slot = Find(track_id);
    const bool selected = std::any_of(crops_.begin(), crops_.end(),
        [track_id](const GpuCrop& crop) { return crop.track_id == track_id; });
    if (!slot || !selected || frame_id < slot->pose_frame || frame_id != last_pose_frame_ ||
        capture_us != last_pose_capture_us_ || capture_us < slot->pose_us) return false;
    slot->published_frame = -1;
    slot->pose_valid = false;
    if (!valid) {
        reacquisition_ = true;
        identity_.RejectPose(track_id);
        return false;
    }
    float left = static_cast<float>(width_), top = static_cast<float>(height_);
    float right = 0, bottom = 0;
    int count = 0;
    for (const auto& joint : joints) {
        if (!joint.valid || !std::isfinite(joint.x_px) || !std::isfinite(joint.y_px) ||
            joint.confidence <= 0 || joint.x_px < 0 || joint.y_px < 0 ||
            joint.x_px >= width_ || joint.y_px >= height_) continue;
        left = std::min(left, joint.x_px); top = std::min(top, joint.y_px);
        right = std::max(right, joint.x_px); bottom = std::max(bottom, joint.y_px);
        ++count;
    }
    if (count < 5 || right <= left || bottom <= top) {
        reacquisition_ = true;
        identity_.RejectPose(track_id);
        return false;
    }
    const float cx = (left + right) * .5F, cy = (top + bottom) * .5F;
    const float w = std::max((right-left) * (1 + kCropMargin),
                             (slot->crop.x2-slot->crop.x1) * .95F);
    const float h = std::max((bottom-top) * (1 + kCropMargin),
                             (slot->crop.y2-slot->crop.y1) * .95F);
    const Detection pose_crop{std::max(0.F,cx-w*.5F), std::max(0.F,cy-h*.5F),
                  std::min(static_cast<float>(width_),cx+w*.5F),
                  std::min(static_cast<float>(height_),cy+h*.5F),slot->crop.score};
    slot->pose_frame = slot->published_frame = frame_id;
    slot->pose_us = capture_us;
    slot->pose_valid = true;
    // A detector captured after this pose was selected already owns the
    // future crop/identity anchor. This older pose is publishable only.
    if (slot->anchor_frame <= frame_id) {
        slot->crop = pose_crop;
        identity_.ObservePose(track_id, slot->crop, capture_us);
    }
    return true;
}

std::int64_t GpuTrackCrops::PublishedJointSourceFrame(int track_id) const {
    const auto* slot = Find(track_id);
    return slot && slot->pose_valid ? slot->published_frame : -1;
}

void GpuTrackCrops::InvalidateGeneration() {
    for (auto& slot : slots_) slot = {};
    identity_.Reset();
    crops_.clear(); matches_.clear(); accepted_.clear();
    last_detection_frame_ = last_pose_frame_ = last_pose_capture_us_ = -1;
    reacquisition_ = true;
    ++generation_;
}
}  // namespace humanvision::runtime
