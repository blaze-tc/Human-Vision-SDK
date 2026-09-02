#include "models/rtmpose/rtmpose_model.h"

#include "models/rtmpose/simcc_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace humanvision {

namespace {

const Tensor* FindOutput(
    const std::vector<Tensor>& outputs,
    const std::string& name) {
    const auto found = std::find_if(
        outputs.begin(), outputs.end(), [&name](const Tensor& output) {
            return output.name == name;
        });
    return found == outputs.end() ? nullptr : &*found;
}

}  // namespace

RtmposeModel::RtmposeModel(std::unique_ptr<IInferenceBackend> backend)
    : backend_(std::move(backend)) {
    tensor_input_.name = "input";
    tensor_input_.shape = {1, 3, 256, 192};
}

bool RtmposeModel::Load(
    const std::filesystem::path& model_path,
    std::string& error) {
    if (!backend_) {
        error = "RTMPose inference backend is null";
        return false;
    }
    return backend_->Load(model_path, error);
}

bool RtmposeModel::Estimate(
    const FrameBuffer& frame,
    const Detection& detection,
    const float threshold,
    std::array<HV_Joint, HV_JOINT_COUNT>& joints,
    float& inference_ms,
    std::string& error) {
    if (threshold < 0.0F || threshold > 1.0F) {
        error = "invalid RTMPose threshold";
        return false;
    }
    if (!PreprocessRtmpose(frame, detection, input_buffer_, error)) {
        return false;
    }
    tensor_input_.values.swap(input_buffer_.normalized_chw);
    const auto start = std::chrono::steady_clock::now();
    const bool succeeded = backend_->Run(tensor_input_, output_buffers_, error);
    tensor_input_.values.swap(input_buffer_.normalized_chw);
    if (!succeeded) {
        return false;
    }
    inference_ms = std::chrono::duration<float, std::milli>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    const Tensor* simcc_x = FindOutput(output_buffers_, "simcc_x");
    const Tensor* simcc_y = FindOutput(output_buffers_, "simcc_y");
    if (simcc_x == nullptr || simcc_y == nullptr) {
        error = "RTMPose outputs do not contain simcc_x and simcc_y";
        return false;
    }
    std::array<DecodedJoint, HV_JOINT_COUNT> decoded{};
    if (!DecodeSimcc(
            *simcc_x, *simcc_y, input_buffer_.transform, decoded, error)) {
        return false;
    }
    for (std::size_t index = 0; index < joints.size(); ++index) {
        HV_Joint joint{};
        joint.x_px = decoded[index].x_px;
        joint.y_px = decoded[index].y_px;
        joint.x_norm = joint.x_px / static_cast<float>(frame.width);
        joint.y_norm = joint.y_px / static_cast<float>(frame.height);
        joint.confidence = decoded[index].confidence;
        joint.valid = static_cast<std::uint8_t>(
            joint.confidence >= threshold && std::isfinite(joint.x_px) &&
            std::isfinite(joint.y_px) && joint.x_px >= 0.0F &&
            joint.y_px >= 0.0F);
        joints[index] = joint;
    }
    error.clear();
    return true;
}

}  // namespace humanvision
