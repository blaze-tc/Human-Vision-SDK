#pragma once

#include "backend/i_inference_backend.h"

#include <memory>

namespace humanvision {

class OnnxRuntimeBackend final : public IInferenceBackend {
public:
    explicit OnnxRuntimeBackend(bool use_gpu = false);
    ~OnnxRuntimeBackend() override;
    OnnxRuntimeBackend(const OnnxRuntimeBackend&) = delete;
    OnnxRuntimeBackend& operator=(const OnnxRuntimeBackend&) = delete;

    bool Load(const std::filesystem::path& model_path, std::string& error) override;
    bool Run(
        const Tensor& input,
        std::vector<Tensor>& outputs,
        std::string& error) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace humanvision
