#pragma once

#include "backend/i_inference_backend.h"

#include <memory>

namespace humanvision {

enum class OnnxRuntimeProvider {
    Cpu,
    PlatformAccelerated,
    Qnn,
    Xnnpack
};

class OnnxRuntimeBackend final : public IInferenceBackend {
public:
    explicit OnnxRuntimeBackend(
        OnnxRuntimeProvider provider = OnnxRuntimeProvider::Cpu,
        bool allow_fallback = true);
    explicit OnnxRuntimeBackend(bool use_gpu, bool use_qnn = false)
        : OnnxRuntimeBackend(
            use_qnn ? OnnxRuntimeProvider::Qnn :
            (use_gpu ? OnnxRuntimeProvider::PlatformAccelerated : OnnxRuntimeProvider::Cpu),
            true) {}
    ~OnnxRuntimeBackend() override;
    OnnxRuntimeBackend(const OnnxRuntimeBackend&) = delete;
    OnnxRuntimeBackend& operator=(const OnnxRuntimeBackend&) = delete;

    bool Load(const std::filesystem::path& model_path, std::string& error) override;
    std::string ActualProvider() const;
    std::string FallbackReason() const;
    bool Run(
        const Tensor& input,
        std::vector<Tensor>& outputs,
        std::string& error) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace humanvision
