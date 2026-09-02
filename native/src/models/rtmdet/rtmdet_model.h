#pragma once

#include "backend/i_inference_backend.h"
#include "core/frame_buffer.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace humanvision {

struct DetectorInput {
    std::vector<float> normalized_chw;
    int resized_width = 0;
    int resized_height = 0;
    float scale_x = 0.0F;
    float scale_y = 0.0F;
};

struct Detection {
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
    float score = 0.0F;
};

class RtmdetModel {
public:
    explicit RtmdetModel(std::unique_ptr<IInferenceBackend> backend);

    bool Load(const std::filesystem::path& model_path, std::string& error);
    bool Preprocess(
        const FrameBuffer& frame,
        DetectorInput& destination,
        std::string& error) const;
    bool Detect(
        const FrameBuffer& frame,
        float threshold,
        int max_bodies,
        std::vector<Detection>& detections,
        float& inference_ms,
        std::string& error);

private:
    std::unique_ptr<IInferenceBackend> backend_;
    DetectorInput input_buffer_;
    Tensor tensor_input_;
    std::vector<Tensor> output_buffers_;
};

}  // namespace humanvision
