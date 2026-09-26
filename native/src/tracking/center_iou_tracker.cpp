#include "tracking/center_iou_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

namespace humanvision {

namespace {

float CenterX(const Detection& detection) {
    return (detection.x1 + detection.x2) * 0.5F;
}

float CenterY(const Detection& detection) {
    return (detection.y1 + detection.y2) * 0.5F;
}

float IntersectionOverUnion(
    const Detection& left,
    const Detection& right) {
    const float intersection_width =
        std::max(0.0F, std::min(left.x2, right.x2) - std::max(left.x1, right.x1));
    const float intersection_height =
        std::max(0.0F, std::min(left.y2, right.y2) - std::max(left.y1, right.y1));
    const float intersection = intersection_width * intersection_height;
    const float left_area =
        std::max(0.0F, left.x2 - left.x1) * std::max(0.0F, left.y2 - left.y1);
    const float right_area =
        std::max(0.0F, right.x2 - right.x1) * std::max(0.0F, right.y2 - right.y1);
    const float united = left_area + right_area - intersection;
    return united > 0.0F ? intersection / united : 0.0F;
}

Detection Translate(
    const Detection& detection,
    const float dx,
    const float dy) {
    Detection result = detection;
    result.x1 += dx;
    result.x2 += dx;
    result.y1 += dy;
    result.y2 += dy;
    return result;
}

float MatchCost(
    const Detection& predicted,
    const Detection& observed,
    bool& eligible) {
    const float dx = CenterX(predicted) - CenterX(observed);
    const float dy = CenterY(predicted) - CenterY(observed);
    const float diagonal = std::max(
        1.0F,
        std::hypot(
            (predicted.x2 - predicted.x1 + observed.x2 - observed.x1) * 0.5F,
            (predicted.y2 - predicted.y1 + observed.y2 - observed.y1) * 0.5F));
    const float normalized_distance = std::hypot(dx, dy) / diagonal;
    const float iou = IntersectionOverUnion(predicted, observed);
    eligible = iou >= 0.01F || normalized_distance <= 1.5F;
    return 0.65F * normalized_distance + 0.35F * (1.0F - iou);
}

}  // namespace

CenterIouTracker::CenterIouTracker(const int max_lost_frames)
    : max_lost_frames_(std::max(0, max_lost_frames)) {}

void CenterIouTracker::Reset() {
    tracks_.clear();
    next_track_id_ = 1;
}

void CenterIouTracker::Update(
    const std::vector<Detection>& detections,
    const std::int64_t timestamp_us,
    std::vector<TrackedDetection>& output) {
    UpdateImpl(detections, timestamp_us, output,
               std::numeric_limits<std::size_t>::max());
}

void CenterIouTracker::ReserveCapacity(std::size_t capacity) {
    tracks_.reserve(capacity);
    candidates_.reserve(capacity * capacity);
    matched_tracks_.reserve(capacity);
    matched_detections_.reserve(capacity);
}

void CenterIouTracker::UpdateBounded(
    const std::vector<Detection>& detections,
    const std::int64_t timestamp_us,
    std::vector<TrackedDetection>& output,
    std::size_t capacity) {
    if (capacity > 8) capacity = 8;
    UpdateImpl(detections, timestamp_us, output, capacity);
}

void CenterIouTracker::UpdateImpl(
    const std::vector<Detection>& detections,
    const std::int64_t timestamp_us,
    std::vector<TrackedDetection>& output,
    std::size_t capacity) {
    output.clear();
    output.resize(detections.size());
    for (std::size_t index = 0; index < detections.size(); ++index) {
        output[index].detection = detections[index];
        output[index].track_id = -1;
    }

    candidates_.clear();
    candidates_.reserve(tracks_.size() * detections.size());
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        const Track& track = tracks_[track_index];
        const std::int64_t delta_us = std::max<std::int64_t>(
            0, timestamp_us - track.timestamp_us);
        const Detection predicted = Translate(
            track.detection,
            track.velocity_x_per_us * static_cast<float>(delta_us),
            track.velocity_y_per_us * static_cast<float>(delta_us));
        for (std::size_t detection_index = 0;
             detection_index < detections.size();
             ++detection_index) {
            bool eligible = false;
            float cost = MatchCost(predicted, detections[detection_index], eligible);
            // A delayed detector observes an older image than the pose crop.
            if (track.pose_timestamp_us > timestamp_us) {
                bool anchor_eligible = false;
                const float anchor_cost = MatchCost(track.detector_anchor, detections[detection_index], anchor_eligible);
                cost = anchor_cost;
                eligible = anchor_eligible;
            }
            if (eligible) {
                candidates_.push_back(MatchCandidate{
                    track_index, detection_index, cost, track.id});
            }
        }
    }
    std::sort(
        candidates_.begin(), candidates_.end(), [](const auto& left, const auto& right) {
            if (left.cost != right.cost) {
                return left.cost < right.cost;
            }
            if (left.track_id != right.track_id) {
                return left.track_id < right.track_id;
            }
            return left.detection_index < right.detection_index;
        });

    matched_tracks_.assign(tracks_.size(), 0);
    matched_detections_.assign(detections.size(), 0);
    for (const MatchCandidate& candidate : candidates_) {
        if (matched_tracks_[candidate.track_index] ||
            matched_detections_[candidate.detection_index]) {
            continue;
        }
        Track& track = tracks_[candidate.track_index];
        const Detection& detection = detections[candidate.detection_index];
        const std::int64_t delta_us = timestamp_us - track.timestamp_us;
        if (delta_us > 0) {
            track.velocity_x_per_us =
                (CenterX(detection) - CenterX(track.detection)) /
                static_cast<float>(delta_us);
            track.velocity_y_per_us =
                (CenterY(detection) - CenterY(track.detection)) /
                static_cast<float>(delta_us);
        }
        track.detector_anchor = detection;
        if (track.pose_timestamp_us <= timestamp_us) {
            track.detection = detection;
            track.timestamp_us = timestamp_us;
            track.pose_timestamp_us = 0; // A newer detection can reacquire an expired pose.
            track.pose_rejected = false;
        }
        track.lost_frames = 0;
        output[candidate.detection_index].track_id = track.id;
        matched_tracks_[candidate.track_index] = 1;
        matched_detections_[candidate.detection_index] = 1;
    }

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        if (!matched_tracks_[index]) {
            ++tracks_[index].lost_frames;
        }
    }
    tracks_.erase(
        std::remove_if(
            tracks_.begin(), tracks_.end(), [this](const Track& track) {
                return track.lost_frames > max_lost_frames_;
            }),
        tracks_.end());

    for (std::size_t index = 0; index < detections.size(); ++index) {
        if (matched_detections_[index] || tracks_.size() >= capacity) {
            continue;
        }
        Track track;
        track.id = next_track_id_++;
        track.detection = detections[index];
        track.detector_anchor = detections[index];
        track.timestamp_us = timestamp_us;
        tracks_.push_back(track);
        output[index].track_id = track.id;
    }
}

void CenterIouTracker::Predict(
    const std::int64_t timestamp_us,
    std::vector<TrackedDetection>& output) {
    output.clear();
    output.reserve(tracks_.size());
    for (const Track& track : tracks_) {
        if (track.pose_rejected) continue;
        if (track.pose_timestamp_us > 0) {
            // This is a crop retry budget, not permission to publish old joints.
            // Several people can take over 500ms on a slow device; validate their
            // next image before deciding they disappeared.
            if (timestamp_us < track.pose_timestamp_us || timestamp_us - track.pose_timestamp_us > 3000000) continue;
        } else if (track.lost_frames != 0) continue;
        // Predict only a crop, never joints; limit extrapolation after a slow detector.
        const std::int64_t delta_us = std::min<std::int64_t>(250000, std::max<std::int64_t>(
            0, timestamp_us - track.timestamp_us));
        const Detection predicted = Translate(
            track.detection,
            track.velocity_x_per_us * static_cast<float>(delta_us),
            track.velocity_y_per_us * static_cast<float>(delta_us));
        output.push_back(TrackedDetection{predicted, track.id});
    }
}

void CenterIouTracker::ObservePose(int track_id, const Detection& crop, std::int64_t timestamp_us) {
    for (auto& track : tracks_) if (track.id == track_id) {
        track.detection = crop;
        track.timestamp_us = track.pose_timestamp_us = timestamp_us;
        track.velocity_x_per_us = track.velocity_y_per_us = 0;
        track.lost_frames = 0;
        track.pose_rejected = false;
        return;
    }
}

void CenterIouTracker::RejectPose(int track_id) {
    for (auto& track : tracks_) if (track.id == track_id) { track.pose_rejected = true; return; }
}

void CenterIouTracker::ForgetTrack(int track_id) {
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
        [track_id](const Track& track) { return track.id == track_id; }), tracks_.end());
}

}  // namespace humanvision
