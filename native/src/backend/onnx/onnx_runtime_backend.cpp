#include "backend/onnx/onnx_runtime_backend.h"

#include <onnxruntime_cxx_api.h>
#if defined(HV_USE_DIRECTML)
#include <dml_provider_factory.h>
#endif

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#if defined(__ANDROID__)
#include <sys/stat.h>
#endif

namespace humanvision {

struct OnnxRuntimeBackend::Impl {
    bool use_gpu = false;
    // Declared before the session/environment so the DLL outlives their teardown.
    std::shared_ptr<void> directml_module;
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "HumanVisionSDK"};
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::vector<std::string> output_names;
};

namespace {

#if defined(HV_USE_DIRECTML)
std::shared_ptr<void> LoadPrivateDirectMl(std::string& error) {
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&LoadPrivateDirectMl), &owner)) {
        error = "Cannot locate HumanVision native module";
        return {};
    }
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(owner, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        error = "Cannot resolve HumanVision native module path";
        return {};
    }
    const auto dependency = std::filesystem::path(path.data()).parent_path() / L"hv_dml.dll";
    HMODULE module = LoadLibraryExW(dependency.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        error = "Cannot load private GPU dependency hv_dml.dll beside humanvision.dll; Windows error " +
                std::to_string(GetLastError());
        return {};
    }
    return std::shared_ptr<void>(module, [](void* value) { FreeLibrary(static_cast<HMODULE>(value)); });
}
#endif

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

OnnxRuntimeBackend::OnnxRuntimeBackend(bool use_gpu) : impl_(std::make_unique<Impl>()) {
    impl_->use_gpu = use_gpu;
    impl_->session_options.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    impl_->session_options.SetIntraOpNumThreads(0);
}

OnnxRuntimeBackend::~OnnxRuntimeBackend() = default;

bool OnnxRuntimeBackend::Load(
    const std::filesystem::path& model_path,
    std::string& error) {
#if defined(__ANDROID__)
    struct stat file_info{};
    const bool regular_file = stat(model_path.c_str(), &file_info) == 0 && S_ISREG(file_info.st_mode);
#else
    const bool regular_file = std::filesystem::is_regular_file(model_path);
#endif
    if (!regular_file) {
        error = "ONNX model file does not exist: " + model_path.string();
        return false;
    }
    try {
#if defined(HV_USE_DIRECTML)
        if (impl_->use_gpu) {
            // Unity resolves plugins separately; the process DLL search path does
            // not necessarily contain this directory. Preflight before delay-load
            // can raise an uncatchable loader SEH exception inside the provider.
            impl_->directml_module = LoadPrivateDirectMl(error);
            if (!impl_->directml_module) return false;
            impl_->session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            impl_->session_options.DisableMemPattern();
            impl_->session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            impl_->session_options.SetIntraOpNumThreads(1);
            const OrtDmlApi* dml = nullptr;
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
                "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
            OrtDmlDeviceOptions device{OrtDmlPerformancePreference::HighPerformance, OrtDmlDeviceFilter::Gpu};
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML2(
                impl_->session_options, &device));
        }
#else
        if (impl_->use_gpu) {
            error = "GPU support is not enabled in this native build";
            return false;
        }
#endif
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
