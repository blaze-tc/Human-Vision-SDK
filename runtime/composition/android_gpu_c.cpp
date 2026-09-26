#include "humanvision/humanvision_android_gpu.h"
#include "composition/session.h"
#include "host/gpu_runtime_host.h"
#include "gpu/android/unity_vulkan_plugin.h"

namespace {
void SetLeaseActive(void* runtime,bool active) noexcept {
    static_cast<humanvision::runtime::RuntimeSession*>(runtime)->SetGpuSourceLeaseActive(active);
}
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

// A4's existing exports and layouts remain frozen. B4 adds source-lease
// exports so the Unity owner can drain before destroying its RenderTexture.
extern "C" {
HV_Result HV_CALL HV_RuntimeBeginAndroidGpuSourceLease(
    HV_RuntimeHandle runtime, void* unity_texture) {
    if (!runtime || !unity_texture) return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    auto* session=static_cast<humanvision::runtime::RuntimeSession*>(runtime);
    if(!session->UsesGpuRoute()){session->ReportError("Android GPU source lease requires the GPU runtime profile");return HV_ERR_INVALID_ARGUMENT;}
    std::string error;
    if(!humanvision::runtime::GpuSourceLeaseCoordinator::Instance().Begin(runtime,unity_texture,
          humanvision::gpu::BeginUnityVulkanSourceLease,
          humanvision::gpu::EndUnityVulkanSourceLease,SetLeaseActive,error)){
        session->ReportError(error.c_str());
        return HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM;
    }
    return HV_OK;
#else
    return Unsupported(runtime);
#endif
}

HV_Result HV_CALL HV_RuntimeEndAndroidGpuSourceLease(HV_RuntimeHandle runtime) {
    if (!runtime) return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    std::string error;
    if(!humanvision::runtime::GpuSourceLeaseCoordinator::Instance().End(runtime,error)){
        static_cast<humanvision::runtime::RuntimeSession*>(runtime)->ReportError(error.c_str());
        return HV_ERR_INVALID_ARGUMENT;
    }
    return HV_OK;
#else
    return Unsupported(runtime);
#endif
}

HV_Result HV_CALL HV_RuntimePrepareAndroidGpuFrame(HV_RuntimeHandle runtime,
    const HV_AndroidGpuSubmissionV1* submission, void** out_render_event_data) {
    if (out_render_event_data) *out_render_event_data = nullptr;
    if (!runtime || !submission || !out_render_event_data ||
        submission->struct_size < sizeof(*submission) || submission->api_version != HV_ANDROID_GPU_API_V1 ||
        !submission->unity_texture || submission->width <= 0 || submission->height <= 0 ||
        submission->frame_id <= 0 || submission->timestamp_us < 0 ||
        (submission->rotation_degrees != 0 && submission->rotation_degrees != 90 &&
         submission->rotation_degrees != 180 && submission->rotation_degrees != 270) || submission->mirrored > 1)
        return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
    static_cast<humanvision::runtime::RuntimeSession*>(runtime)->RecordGpuCaptureAttempt();
    const auto bridge_result=humanvision::runtime::GpuSourceLeaseCoordinator::Instance().Prepare(runtime,*submission,
          out_render_event_data,humanvision::gpu::PrepareUnityVulkanFrame,
          [](void* owner,uint32_t width,uint32_t height) noexcept {
              static_cast<humanvision::runtime::RuntimeSession*>(owner)->RecordGpuDimensions(width,height);
          });
    const HV_Result result=humanvision::gpu::AndroidBridgeResultCode(bridge_result);
    if(bridge_result==humanvision::gpu::BridgeResult::DroppedNoSlot)
        static_cast<humanvision::runtime::RuntimeSession*>(runtime)->RecordGpuNoSlotDrop();
    return result == HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM
               ? BridgeFailure(runtime,
                               "Android Vulkan producer bridge is not configured")
               : result;
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
