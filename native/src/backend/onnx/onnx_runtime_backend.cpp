#include "backend/onnx/onnx_runtime_backend.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace humanvision {

struct OnnxRuntimeBackend::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "HumanVisionSDK"};
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::vector<std::string> output_names;
};

namespace {

std::size_t ElementCount(const std::vector<std::int64_t>& shape) {
    if (shape.empty()) {
        return 0;
    }
    std::size_t count = 1;
    for (const std::int64_t dimension : shape) {
        if (dimension <= 0) {
            return 0;
        }
        count *= static_cast<std::size_t>(dimension);
    }
    return count;
}

}  // namespace

OnnxRuntimeBackend::OnnxRuntimeBackend() : impl_(std::make_unique<Impl>()) {
    impl_->session_options.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    impl_->session_options.SetIntraOpNumThreads(0);
}

OnnxRuntimeBackend::~OnnxRuntimeBackend() = default;

bool OnnxRuntimeBackend::Load(
    const std::filesystem::path& model_path,
    std::string& error) {
    if (!std::filesystem::is_regular_file(model_path)) {
        error = "ONNX model file does not exist: " + model_path.string();
        return false;
    }
    try {
        auto session = std::make_unique<Ort::Session>(
            impl_->environment, model_path.c_str(), impl_->session_options);
        if (session->GetInputCount() != 1) {
            error = "ONNX backend requires exactly one input: " + model_path.string();
            return false;
        }

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session->GetInputNameAllocated(0, allocator);
        std::vector<std::string> output_names;
        output_names.reserve(session->GetOutputCount());
        for (std::size_t index = 0; index < session->GetOutputCount(); ++index) {
            auto name = session->GetOutputNameAllocated(index, allocator);
            output_names.emplace_back(name.get());
            const auto type_info = session->GetOutputTypeInfo(index).GetTensorTypeAndShapeInfo();
            if (type_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                error = "ONNX backend currently supports float32 outputs only: " +
                        output_names.back();
                return false;
            }
        }
        const auto input_type = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        if (input_type.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            error = "ONNX backend currently supports float32 input only";
            return false;
        }

        impl_->input_name = input_name.get();
        impl_->output_names = std::move(output_names);
        impl_->session = std::move(session);
        error.clear();
        return true;
    } catch (const Ort::Exception& exception) {
        error = "Failed to load ONNX model '" + model_path.string() + "': " +
                exception.what();
        return false;
    } catch (const std::exception& exception) {
        error = "Failed to load ONNX model '" + model_path.string() + "': " +
                exception.what();
        return false;
    }
}

bool OnnxRuntimeBackend::Run(
    const Tensor& input,
    std::vector<Tensor>& outputs,
    std::string& error) {
    if (!impl_->session) {
        error = "ONNX backend is not loaded";
        return false;
    }
    const std::size_t expected_count = ElementCount(input.shape);
    if (expected_count == 0 || expected_count != input.values.size()) {
        error = "input tensor shape does not match its float value count";
        return false;
    }
    if (!input.name.empty() && input.name != impl_->input_name) {
        error = "input tensor name mismatch: expected '" + impl_->input_name +
                "', received '" + input.name + "'";
        return false;
    }

    try {
        auto memory_info = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
        auto input_value = Ort::Value::CreateTensor<float>(
            memory_info,
            const_cast<float*>(input.values.data()),
            input.values.size(),
            input.shape.data(),
            input.shape.size());
        const char* input_names[] = {impl_->input_name.c_str()};
        std::vector<const char*> output_names;
        output_names.reserve(impl_->output_names.size());
        for (const auto& name : impl_->output_names) {
            output_names.push_back(name.c_str());
        }

        auto values = impl_->session->Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_value,
            1,
            output_names.data(),
            output_names.size());
        outputs.resize(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto tensor_info = values[index].GetTensorTypeAndShapeInfo();
            Tensor& output = outputs[index];
            output.name = impl_->output_names[index];
            output.shape = tensor_info.GetShape();
            const std::size_t count = tensor_info.GetElementCount();
            const float* data = values[index].GetTensorData<float>();
            output.values.assign(data, data + count);
        }
        error.clear();
        return true;
    } catch (const Ort::Exception& exception) {
        error = std::string("ONNX inference failed: ") + exception.what();
        return false;
    } catch (const std::exception& exception) {
        error = std::string("ONNX inference failed: ") + exception.what();
        return false;
    }
}

}  // namespace humanvision
