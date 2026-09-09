#include "models/rtmpose/rtmpose_model.h"

#include "models/rtmpose/simcc_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
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
    std::string& error, std::array<HV_Joint, 6>* hands) {
    if (hands) hands->fill(HV_Joint{});
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
    const int count = simcc_x->shape.size() == 3 ? static_cast<int>(simcc_x->shape[1]) : 0;
    std::array<DecodedJoint, 133> decoded{};
    if (!DecodeSimccJoints(*simcc_x, *simcc_y, input_buffer_.transform, decoded.data(), count, error)) {
        return false;
    }
    auto convert = [&](const DecodedJoint& point) {
        HV_Joint result{};
        result.x_px = point.x_px; result.y_px = point.y_px;
        result.x_norm = point.x_px / frame.width; result.y_norm = point.y_px / frame.height;
        result.confidence = point.confidence;
        result.valid = std::isfinite(point.confidence) && point.confidence > 0 && point.confidence >= threshold &&
            std::isfinite(point.x_px) && std::isfinite(point.y_px) && point.x_px >= 0 && point.y_px >= 0 &&
            point.x_px < frame.width && point.y_px < frame.height;
        return result;
    };
    if (count == 133 && hands) {
        const int palms[2][5] = {{91,96,100,104,108}, {112,117,121,125,129}};
        for (int side = 0; side < 2; ++side) {
            DecodedJoint palm{}; palm.confidence = std::numeric_limits<float>::infinity(); bool valid = true;
            for (int index : palms[side]) {
                palm.x_px += decoded[index].x_px / 5.0F; palm.y_px += decoded[index].y_px / 5.0F;
                palm.confidence = std::min(palm.confidence, decoded[index].confidence);
                valid = valid && convert(decoded[index]).valid;
            }
            auto& hand = (*hands)[side * 3]; hand = convert(palm);
            hand.valid = hand.valid && valid; hand.reserved[0] = 1; // derived from real hand landmarks
            (*hands)[side * 3 + 1] = convert(decoded[side == 0 ? 103 : 124]);
            (*hands)[side * 3 + 2] = convert(decoded[side == 0 ? 95 : 116]);
        }
    }
    for (std::size_t index = 0; index < joints.size(); ++index) {
        joints[index] = convert(decoded[index]);
    }
    error.clear();
    return true;
}

}  // namespace humanvision
