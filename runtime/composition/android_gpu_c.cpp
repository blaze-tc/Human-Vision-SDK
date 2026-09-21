#include "humanvision/humanvision_android_gpu.h"
#include "composition/session.h"
#include "gpu/android/unity_vulkan_plugin.h"

namespace {
HV_Result Unsupported(HV_RuntimeHandle runtime) noexcept {
    try {
        static_cast<humanvision::runtime::RuntimeSession*>(runtime)->ReportError(
            "Android GPU bridge unavailable in this build: requires Android ARM64 API 26+ "
            "and the Vulkan bridge; rebuild with Vulkan first in Project Settings.");
        return HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM;
    } catch (...) { return HV_ERR_INTERNAL; }
}

HV_Result BridgeFailure(HV_RuntimeHandle runtime, const char* fallback) noexcept {
    try {
        const char* diagnostic = humanvision::gpu::UnityVulkanProducerDiagnostic();
        static_cast<humanvision::runtime::RuntimeSession*>(runtime)->ReportError(
            diagnostic && *diagnostic ? diagnostic : fallback);
        return HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM;
    } catch (...) { return HV_ERR_INTERNAL; }
}
}

// A4 freezes the exports on all builds. The production Android Vulkan bridge is
// added by B4; until then no platform advertises GPU submission support.
extern "C" {
HV_Result HV_CALL HV_RuntimePrepareAndroidGpuFrame(HV_RuntimeHandle runtime,
    const HV_AndroidGpuSubmissionV1* submission, void** out_render_event_data) {
    if (out_render_event_data) *out_render_event_data = nullptr;
    if (!runtime || !submission || !out_render_event_data ||
        submission->struct_size < sizeof(*submission) || submission->api_version != HV_ANDROID_GPU_API_V1 ||
        !submission->unity_texture || submission->width <= 0 || submission->height <= 0 ||
        (submission->rotation_degrees != 0 && submission->rotation_degrees != 90 &&
         submission->rotation_degrees != 180 && submission->rotation_degrees != 270) || submission->mirrored > 1)
        return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    switch (humanvision::gpu::PrepareUnityVulkanFrame(*submission, out_render_event_data)) {
    case humanvision::gpu::BridgeResult::Ok: return HV_OK;
    case humanvision::gpu::BridgeResult::DroppedNoSlot:
    case humanvision::gpu::BridgeResult::Busy: return HV_NO_NEW_RESULT;
    case humanvision::gpu::BridgeResult::Invalid: return HV_ERR_INVALID_ARGUMENT;
    default: return BridgeFailure(runtime, "Android Vulkan producer bridge is not configured");
    }
#else
    return Unsupported(runtime);
#endif
}

void* HV_CALL HV_GetAndroidGpuRenderEventAndDataFunction(void) {
    return humanvision::gpu::UnityVulkanRenderEventFunction();
}

HV_Result HV_CALL HV_RuntimeGetAndroidGpuBridgeStatus(HV_RuntimeHandle runtime,
    HV_AndroidGpuBridgeStatusV1* out_status) {
    if (!runtime || !out_status || out_status->struct_size < sizeof(*out_status) ||
        out_status->api_version != HV_ANDROID_GPU_API_V1) return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    HV_AndroidGpuBridgeStatusV1 status{sizeof(status), HV_ANDROID_GPU_API_V1};
    humanvision::gpu::GetUnityVulkanProducerStatus(status);
    *out_status = status;
    if (status.copy_path != HV_ANDROID_GPU_COPY_UNAVAILABLE) return HV_OK;
    return BridgeFailure(runtime, "Android Vulkan producer bridge is not configured");
#else
    *out_status = {};
    out_status->struct_size = sizeof(*out_status);
    out_status->api_version = HV_ANDROID_GPU_API_V1;
    out_status->copy_path = HV_ANDROID_GPU_COPY_UNAVAILABLE;
    return Unsupported(runtime);
#endif
}
}
