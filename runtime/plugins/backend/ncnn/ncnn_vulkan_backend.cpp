#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "humanvision_plugin_v3.h"
#include <cmath>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#if defined(__ANDROID__)
#include "plugins/backend/ncnn/ncnn_android_session.h"
#include <memory>
#endif

namespace humanvision::runtime::ncnn_backend {
bool ParseInputContract(const nlohmann::json& value, InputContract& result, std::string& error) {
    try {
        InputContract parsed;
        const auto required = [&](const char* key) -> const nlohmann::json& {
            if (!value.contains(key) || value.at(key).is_null())
                throw std::runtime_error(std::string("Missing input contract field: ") + key);
            return value.at(key);
        };
        if (required("image_format").get<std::string>() != "rgba8-unorm")
            throw std::runtime_error("Input image_format must be rgba8-unorm");
        parsed.image_format = HV_GPU_IMAGE_RGBA8_UNORM;
        if (required("color_order").get<std::string>() != "rgb")
            throw std::runtime_error("Input color_order must be rgb");
        parsed.channel_order = 1;
        const auto& normalization = required("normalization");
        if (!normalization.is_object()) throw std::runtime_error("normalization must be an object");
        for (const auto key : {"mean", "norm"}) {
            if (!normalization.contains(key) || !normalization.at(key).is_array() || normalization.at(key).size() != 3)
                throw std::runtime_error(std::string("Missing or invalid normalization.") + key);
            for (size_t i = 0; i < 3; ++i) {
                if (!normalization.at(key).at(i).is_number())
                    throw std::runtime_error(std::string("Non-numeric normalization.") + key);
                const float number = normalization.at(key).at(i).get<float>();
                if (!std::isfinite(number)) throw std::runtime_error(std::string("Non-finite normalization.") + key);
                (std::string(key) == "mean" ? parsed.mean : parsed.norm)[i] = number;
            }
        }
        const auto dtype = required("tensor_dtype").get<std::string>();
        if (dtype == "fp32") { parsed.output_type = HV_GPU_TENSOR_FP32; parsed.cast_type_to = 1; }
        else if (dtype == "fp16") { parsed.output_type = HV_GPU_TENSOR_FP16; parsed.cast_type_to = 2; }
        else throw std::runtime_error("Unsupported tensor_dtype: " + dtype);
        parsed.output_elempack = required("elempack").get<int>();
        if (parsed.output_elempack != 1 && parsed.output_elempack != 4)
            throw std::runtime_error("elempack must be 1 or 4");
        parsed.width = required("width").get<int>(); parsed.height = required("height").get<int>();
        if (parsed.width <= 0 || parsed.height <= 0)
            throw std::runtime_error("Input dimensions must be positive");
        parsed.input_blob = required("input_blob").get<std::string>();
        if (parsed.input_blob.empty()) throw std::runtime_error("input_blob is empty");
        const auto& blobs = required("output_blobs");
        if (!blobs.is_array() || blobs.empty()) throw std::runtime_error("output_blobs must be nonempty");
        std::set<std::string> unique;
        for (const auto& blob : blobs) {
            if (!blob.is_string()) throw std::runtime_error("output_blobs contains a non-string");
            auto name = blob.get<std::string>();
            if (name.empty() || !unique.insert(name).second) throw std::runtime_error("output_blobs contains empty or duplicate name");
            parsed.output_blobs.push_back(std::move(name));
        }
        result = std::move(parsed); error.clear(); return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
}

namespace {
void WriteError(HV_ErrorBufferV1* out, const char* message) {
    if (!out || out->struct_size < sizeof(*out) || out->api_version != HV_PLUGIN_API_V1 ||
        !out->data || !out->capacity) return;
    const auto n = std::min<size_t>(std::strlen(message), out->capacity - 1);
    std::memcpy(out->data, message, n);
    out->data[n] = 0;
}
bool HasUuid(const uint8_t (&bytes)[HV_GPU_UUID_SIZE]) {
    return std::any_of(std::begin(bytes), std::end(bytes), [](uint8_t b) { return b != 0; });
}
HV_Result HV_CALL Create(const HV_GpuBackendConfigV1* config, const HV_GpuDeviceContextV1* device,
                         void** out, HV_ErrorBufferV1* error) {
    if (out) *out = nullptr;
    if (!config || !device || !out || config->struct_size < sizeof(*config) ||
        config->api_version != HV_GPU_FRAME_API_V1 || device->struct_size < sizeof(*device) ||
        device->api_version != HV_GPU_FRAME_API_V1 || !config->model_manifest_utf8 ||
        !config->asset_root_utf8 || !config->requested_provider_utf8 ||
        std::strcmp(config->requested_provider_utf8, "backend.ncnn.vulkan") != 0) {
        WriteError(error, "backend.ncnn.vulkan requires an explicit V2 GPU config and provider");
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (!HasUuid(device->device_uuid) || !HasUuid(device->driver_uuid)) {
        WriteError(error, "Unity Vulkan deviceUUID and driverUUID are required for exact ncnn device matching");
        return HV_ERR_NOT_INITIALIZED;
    }
#if defined(__ANDROID__)
    try {
        auto session = std::make_unique<humanvision::runtime::ncnn_backend::AndroidSession>();
        std::string reason;
        if (!session->Initialize(*config, *device, reason)) {
            WriteError(error, reason.c_str()); return HV_ERR_NOT_INITIALIZED;
        }
        *out = session.release();
        return HV_OK;
    } catch (const std::exception& ex) {
        WriteError(error, ex.what()); return HV_ERR_INTERNAL;
    } catch (...) {
        WriteError(error, "ncnn Vulkan session creation failed"); return HV_ERR_INTERNAL;
    }
#else
    WriteError(error, "ncnn Vulkan backend requires Android API 26 ARM64");
    return HV_ERR_NOT_INITIALIZED;
#endif
}
void HV_CALL Destroy(void* instance) {
#if defined(__ANDROID__)
    auto* session = static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance);
    // On an unprovable device fault the session owns potentially in-flight
    // ncnn Vulkan objects. Keep them alive until process-level GPU recovery.
    if (session && !session->IsQuarantined()) delete session;
#else
    (void)instance;
#endif
}
HV_Result HV_CALL Run(void* instance, const HV_GpuFrameRefV1* frame, const HV_GpuImageTransformV1* transform,
                      HV_TensorViewV1* outputs, uint32_t capacity, uint32_t* count, HV_ErrorBufferV1* error) {
    if (count) *count = 0;
    if (!instance || !frame || !transform || !count ||
        frame->struct_size < sizeof(*frame) || frame->api_version != HV_GPU_FRAME_API_V1 ||
        transform->struct_size < sizeof(*transform) || transform->api_version != HV_GPU_FRAME_API_V1 ||
        (capacity && !outputs)) {
#if defined(__ANDROID__)
        if (instance && frame && frame->struct_size >= sizeof(*frame) &&
            frame->api_version == HV_GPU_FRAME_API_V1)
            static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance)->RetireUnsubmitted(frame->opaque_slot);
#endif
        return HV_ERR_INVALID_ARGUMENT;
    }
#if defined(__ANDROID__)
    try {
        std::string reason;
        const auto result = static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance)->Run(
            *frame, *transform, outputs, capacity, *count, reason);
        if (result != HV_OK) WriteError(error, reason.c_str());
        return result;
    } catch (const std::exception& ex) {
        *count = 0; WriteError(error, ex.what()); return HV_ERR_INTERNAL;
    } catch (...) {
        *count = 0; WriteError(error, "ncnn Vulkan run failed"); return HV_ERR_INTERNAL;
    }
#else
    WriteError(error, "ncnn Vulkan backend requires Android API 26 ARM64");
    return HV_ERR_NOT_INITIALIZED;
#endif
}
HV_Result HV_CALL Info(void* instance, HV_BackendSessionInfoV1* info) {
    if (!instance || !info || info->struct_size < sizeof(*info) ||
        info->api_version != HV_PLUGIN_API_V1) return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    return static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance)->Info(*info);
#else
    return HV_ERR_NOT_INITIALIZED;
#endif
}
const HV_GpuBackendApiV1 backend_api{sizeof(backend_api), HV_GPU_FRAME_API_V1, Create, Destroy, Run, Info};

HV_Result HV_CALL Prepare(void* instance, const HV_GpuFrameRefV1* frame,
                          const HV_GpuImageTransformV1* transform,
                          HV_GpuPreparedRefV1* out, HV_ErrorBufferV1* error) {
    if (!instance || !frame || !transform || !out ||
        out->struct_size < sizeof(*out) || out->api_version != HV_GPU_PREPARED_API_V1 ||
        frame->struct_size < sizeof(*frame) || frame->api_version != HV_GPU_FRAME_API_V1 ||
        transform->struct_size < sizeof(*transform) ||
        transform->api_version != HV_GPU_FRAME_API_V1) return HV_ERR_INVALID_ARGUMENT;
    *out = {sizeof(*out), HV_GPU_PREPARED_API_V1};
#if defined(__ANDROID__)
    auto* session = static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance);
    try {
        std::string reason;
        const auto result = session->Prepare(*frame, *transform, *out, reason);
        if (result != HV_OK) WriteError(error, reason.c_str());
        return result;
    } catch (const std::exception& ex) {
        session->QuarantinePrepared(frame->opaque_slot);
        WriteError(error, ex.what()); return HV_ERR_INTERNAL;
    } catch (...) {
        session->QuarantinePrepared(frame->opaque_slot);
        WriteError(error, "ncnn prepared GPU copy raised an exception"); return HV_ERR_INTERNAL;
    }
#else
    WriteError(error, "ncnn Vulkan prepared input requires Android API 26 ARM64");
    return HV_ERR_NOT_INITIALIZED;
#endif
}
HV_Result HV_CALL RunPrepared(void* instance, const HV_GpuPreparedRefV1* ref,
                              HV_TensorViewV1* outputs, uint32_t capacity,
                              uint32_t* count, HV_ErrorBufferV1* error) {
    if (count) *count = 0;
    if (!instance || !ref || !count || (capacity && !outputs) ||
        ref->struct_size < sizeof(*ref) || ref->api_version != HV_GPU_PREPARED_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    auto* session = static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance);
    try {
        std::string reason;
        const auto result = session->RunPrepared(*ref, outputs, capacity, *count, reason);
        if (result != HV_OK) WriteError(error, reason.c_str());
        return result;
    } catch (const std::exception& ex) {
        session->QuarantinePrepared(nullptr);
        WriteError(error, ex.what()); return HV_ERR_INTERNAL;
    } catch (...) {
        session->QuarantinePrepared(nullptr);
        WriteError(error, "ncnn prepared inference raised an exception"); return HV_ERR_INTERNAL;
    }
#else
    WriteError(error, "ncnn Vulkan prepared input requires Android API 26 ARM64");
    return HV_ERR_NOT_INITIALIZED;
#endif
}
HV_Result HV_CALL DiscardPrepared(void* instance, const HV_GpuPreparedRefV1* ref,
                                  HV_ErrorBufferV1* error) {
    if (!instance || !ref || ref->struct_size < sizeof(*ref) ||
        ref->api_version != HV_GPU_PREPARED_API_V1) return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    try {
        std::string reason;
        const auto result = static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance)
            ->DiscardPrepared(*ref, reason);
        if (result != HV_OK) WriteError(error, reason.c_str());
        return result;
    } catch (...) {
        static_cast<humanvision::runtime::ncnn_backend::AndroidSession*>(instance)
            ->QuarantinePrepared(nullptr);
        WriteError(error, "ncnn prepared discard raised an exception"); return HV_ERR_INTERNAL;
    }
#else
    WriteError(error, "ncnn Vulkan prepared input requires Android API 26 ARM64");
    return HV_ERR_NOT_INITIALIZED;
#endif
}
const HV_GpuPreparedApiV1 prepared_api{sizeof(prepared_api), HV_GPU_PREPARED_API_V1,
    Prepare, RunPrepared, DiscardPrepared};
const HV_GpuBackendApiV2 backend_api_v3{{sizeof(backend_api_v3), HV_GPU_FRAME_API_V1,
    Create, Destroy, Run, Info}, &prepared_api};
}

extern "C" HV_Result HV_CALL HV_QueryNcnnVulkanPluginV2(uint32_t version, HV_PluginApiV2* out) {
    if (version != HV_PLUGIN_API_V2 || !out || out->v1.struct_size < sizeof(*out) ||
        out->v1.api_version != HV_PLUGIN_API_V2) return HV_ERR_INVALID_ARGUMENT;
    constexpr uint64_t caps = HV_CAP_GPU_INPUT | HV_CAP_TENSOR_INFERENCE | HV_CAP_VULKAN |
        HV_CAP_FP16_STORAGE | HV_CAP_FP16_ARITHMETIC | HV_CAP_ANDROID_HARDWARE_BUFFER |
        HV_CAP_EXTERNAL_SYNC_FD;
    *out = {};
    out->v1 = {sizeof(*out), HV_PLUGIN_API_V2, "backend.ncnn.vulkan", "0.4.0-preview.4",
        HV_PLUGIN_BACKEND, caps, 0, nullptr, nullptr, 0};
    out->gpu_backend = &backend_api;
    return HV_OK;
}

extern "C" HV_Result HV_CALL HV_QueryNcnnVulkanPluginV3(uint32_t version, HV_PluginApiV3* out) {
    if (version != HV_PLUGIN_API_V3 || !out || out->v1.struct_size < sizeof(*out) ||
        out->v1.api_version != HV_PLUGIN_API_V3) return HV_ERR_INVALID_ARGUMENT;
    constexpr uint64_t caps = HV_CAP_GPU_INPUT | HV_CAP_TENSOR_INFERENCE | HV_CAP_VULKAN |
        HV_CAP_FP16_STORAGE | HV_CAP_FP16_ARITHMETIC | HV_CAP_ANDROID_HARDWARE_BUFFER |
        HV_CAP_EXTERNAL_SYNC_FD;
    *out = {};
    out->v1 = {sizeof(*out), HV_PLUGIN_API_V3, "backend.ncnn.vulkan", "0.4.0-preview.4",
        HV_PLUGIN_BACKEND, caps, 0, nullptr, nullptr, 0};
    out->gpu_backend = &backend_api_v3;
    return HV_OK;
}
