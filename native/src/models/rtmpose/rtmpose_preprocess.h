#pragma once

#include "core/frame_buffer.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/pose_affine.h"

#include <string>
#include <vector>

namespace humanvision {

struct PoseInput {
    std::vector<float> normalized_chw;
    PoseAffineTransform transform;
};

bool PreprocessRtmpose(
    const FrameBuffer& frame,
    const Detection& detection,
    PoseInput& destination,
    std::string& error, int input_width = 192, int input_height = 256);

}  // namespace humanvision
