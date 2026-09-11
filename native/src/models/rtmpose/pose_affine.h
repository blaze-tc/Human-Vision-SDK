#pragma once

#include "models/rtmdet/rtmdet_model.h"

#include <array>
#include <string>

namespace humanvision {

struct Point2f {
    float x = 0.0F;
    float y = 0.0F;
};

struct PoseAffineTransform {
    float center_x = 0.0F;
    float center_y = 0.0F;
    float scale_width = 0.0F;
    float scale_height = 0.0F;
    std::array<float, 6> source_to_input{};
    std::array<float, 6> input_to_source{};
};

bool BuildPoseAffine(
    const Detection& detection,
    PoseAffineTransform& destination,
    std::string& error, int input_width = 192, int input_height = 256);

Point2f TransformPoint(
    const std::array<float, 6>& matrix,
    const Point2f& point) noexcept;

}  // namespace humanvision
