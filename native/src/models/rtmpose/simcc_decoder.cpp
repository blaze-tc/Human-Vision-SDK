#include "models/rtmpose/simcc_decoder.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>

namespace humanvision {

namespace {

constexpr std::size_t kXBins = 384U;
constexpr std::size_t kYBins = 512U;
constexpr float kSplitRatio = 2.0F;

bool ValidTensor(
    const Tensor& tensor,
    const std::string& expected_name,
    const std::size_t bins, int count) {
    return tensor.name == expected_name && tensor.shape.size() == 3U &&
           tensor.shape[0] == 1 && tensor.shape[1] == count &&
           tensor.shape[2] == static_cast<std::int64_t>(bins) &&
           tensor.values.size() == static_cast<std::size_t>(count) * bins;
}

}  // namespace

bool DecodeSimcc(
    const Tensor& simcc_x,
    const Tensor& simcc_y,
    const PoseAffineTransform& transform,
    std::array<DecodedJoint, HV_JOINT_COUNT>& joints,
    std::string& error) {
    return DecodeSimccJoints(simcc_x, simcc_y, transform, joints.data(), HV_JOINT_COUNT, error);
}

bool DecodeSimccJoints(const Tensor& simcc_x, const Tensor& simcc_y,
    const PoseAffineTransform& transform, DecodedJoint* joints, int count, std::string& error) {
    if (!joints || (count != 17 && count != 133) ||
        !ValidTensor(simcc_x, "simcc_x", kXBins, count) ||
        !ValidTensor(simcc_y, "simcc_y", kYBins, count)) {
        error =
            "RTMPose outputs must be matched float tensors [1,K,384]/[1,K,512], K=17 or 133";
        return false;
    }

    if (!std::all_of(simcc_x.values.begin(), simcc_x.values.end(), [](float value) { return std::isfinite(value); }) ||
        !std::all_of(simcc_y.values.begin(), simcc_y.values.end(), [](float value) { return std::isfinite(value); })) {
        error = "RTMPose output contains nonfinite values"; return false;
    }
    for (std::size_t joint = 0; joint < static_cast<size_t>(count); ++joint) {
        const auto x_begin = simcc_x.values.begin() + joint * kXBins;
        const auto y_begin = simcc_y.values.begin() + joint * kYBins;
        const auto x_peak = std::max_element(x_begin, x_begin + kXBins);
        const auto y_peak = std::max_element(y_begin, y_begin + kYBins);
        const float confidence = std::min(*x_peak, *y_peak);
        DecodedJoint& decoded = joints[joint];
        decoded.confidence = confidence;
        if (confidence <= 0.0F) {
            decoded.x_px = -1.0F;
            decoded.y_px = -1.0F;
            continue;
        }
        const Point2f input{
            static_cast<float>(std::distance(x_begin, x_peak)) / kSplitRatio,
            static_cast<float>(std::distance(y_begin, y_peak)) / kSplitRatio};
        const Point2f source = TransformPoint(transform.input_to_source, input);
        decoded.x_px = source.x;
        decoded.y_px = source.y;
    }
    error.clear();
    return true;
}

}  // namespace humanvision
