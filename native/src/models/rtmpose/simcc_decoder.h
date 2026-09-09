#pragma once

#include "backend/i_inference_backend.h"
#include "humanvision/humanvision_types.h"
#include "models/rtmpose/pose_affine.h"

#include <array>
#include <string>

namespace humanvision {

struct DecodedJoint {
    float x_px = 0.0F;
    float y_px = 0.0F;
    float confidence = 0.0F;
};

bool DecodeSimccJoints(const Tensor& x, const Tensor& y, const PoseAffineTransform& transform,
    DecodedJoint* joints, int count, std::string& error);

bool DecodeSimcc(
    const Tensor& simcc_x,
    const Tensor& simcc_y,
    const PoseAffineTransform& transform,
    std::array<DecodedJoint, HV_JOINT_COUNT>& joints,
    std::string& error);

}  // namespace humanvision
