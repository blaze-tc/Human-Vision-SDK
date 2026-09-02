#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace humanvision {

struct Tensor {
    std::string name;
    std::vector<std::int64_t> shape;
    std::vector<float> values;
};

class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;
    virtual bool Load(const std::filesystem::path& model_path, std::string& error) = 0;
    virtual bool Run(
        const Tensor& input,
        std::vector<Tensor>& outputs,
        std::string& error) = 0;
};

}  // namespace humanvision
