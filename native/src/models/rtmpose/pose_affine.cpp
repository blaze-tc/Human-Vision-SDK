#include "models/rtmpose/pose_affine.h"

#include <algorithm>
#include <cmath>

namespace humanvision {

namespace {

constexpr float kInputWidth = 192.0F;
constexpr float kInputHeight = 256.0F;
constexpr float kBboxPadding = 1.25F;

}  // namespace

bool BuildPoseAffine(
    const Detection& detection,
    PoseAffineTransform& destination,
    std::string& error) {
    const float width = detection.x2 - detection.x1;
    const float height = detection.y2 - detection.y1;
    if (!std::isfinite(detection.x1) || !std::isfinite(detection.y1) ||
        !std::isfinite(detection.x2) || !std::isfinite(detection.y2) ||
        width <= 0.0F || height <= 0.0F) {
        error = "RTMPose requires a finite, positive person bounding box";
        return false;
    }

    destination.center_x = (detection.x1 + detection.x2) * 0.5F;
    destination.center_y = (detection.y1 + detection.y2) * 0.5F;
    destination.scale_width = width * kBboxPadding;
    destination.scale_height = height * kBboxPadding;
    const float aspect_ratio = kInputWidth / kInputHeight;
    if (destination.scale_width > destination.scale_height * aspect_ratio) {
        destination.scale_height = destination.scale_width / aspect_ratio;
    } else {
        destination.scale_width = destination.scale_height * aspect_ratio;
    }

    const float scale = kInputWidth / destination.scale_width;
    destination.source_to_input = {
        scale,
        0.0F,
        kInputWidth * 0.5F - destination.center_x * scale,
        0.0F,
        scale,
        kInputHeight * 0.5F - destination.center_y * scale};
    const float inverse_scale = 1.0F / scale;
    destination.input_to_source = {
        inverse_scale,
        0.0F,
        destination.center_x - kInputWidth * 0.5F * inverse_scale,
        0.0F,
        inverse_scale,
        destination.center_y - kInputHeight * 0.5F * inverse_scale};
    error.clear();
    return true;
}

Point2f TransformPoint(
    const std::array<float, 6>& matrix,
    const Point2f& point) noexcept {
    return Point2f{
        matrix[0] * point.x + matrix[1] * point.y + matrix[2],
        matrix[3] * point.x + matrix[4] * point.y + matrix[5]};
}

}  // namespace humanvision
