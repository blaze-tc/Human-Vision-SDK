#pragma once

#include "backend/i_inference_backend.h"
#include "core/frame_buffer.h"
#include "humanvision/humanvision_types.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/rtmpose_preprocess.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace humanvision {

class RtmposeModel {
public:
    explicit RtmposeModel(std::unique_ptr<IInferenceBackend> backend);

    bool Load(const std::filesystem::path& model_path, std::string& error);
    bool Estimate(
        const FrameBuffer& frame,
        const Detection& detection,
        float threshold,
        std::array<HV_Joint, HV_JOINT_COUNT>& joints,
        float& inference_ms,
        std::string& error, std::array<HV_Joint, 6>* hands = nullptr);

private:
    std::unique_ptr<IInferenceBackend> backend_;
    PoseInput input_buffer_;
    Tensor tensor_input_;
    std::vector<Tensor> output_buffers_;
};

}  // namespace humanvision
