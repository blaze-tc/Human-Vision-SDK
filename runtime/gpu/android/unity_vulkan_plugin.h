#pragma once

#include "gpu/android/unity_vulkan_bridge.h"

namespace humanvision::gpu {

// Called by the Android backend control path after B2 has measured the exact
// source/AHB/device contract. This never probes or silently chooses a path.
bool ConfigureUnityVulkanProducer(const AhbSelection &,
                                  const SlotContract &) noexcept;
// The caller retains this Unity-owned texture until End returns. End closes
// admission and synchronously drains the source cache before Release/Destroy.
bool BeginUnityVulkanSourceLease(void *unity_texture) noexcept;
void EndUnityVulkanSourceLease() noexcept;
void ShutdownUnityVulkanProducer() noexcept;
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &,
                                     void **) noexcept;
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &) noexcept;
void *UnityVulkanRenderEventFunction() noexcept;
const char *UnityVulkanProducerDiagnostic() noexcept;
#if defined(HV_ANDROID_GPU_GATE)
UnityVulkanBridge* UnityVulkanProducerBridge() noexcept;
bool UnityVulkanProducerContext(VulkanDeviceContext&) noexcept;
#endif

inline HV_Result AndroidBridgeResultCode(BridgeResult result) noexcept {
  switch (result) {
  case BridgeResult::Ok:
    return HV_OK;
  case BridgeResult::DroppedNoSlot:
  case BridgeResult::Busy:
    return HV_NO_NEW_RESULT;
  case BridgeResult::Invalid:
    return HV_ERR_INVALID_ARGUMENT;
  case BridgeResult::Closed:
  case BridgeResult::GpuError:
    return HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM;
  }
  return HV_ERR_INTERNAL;
}

} // namespace humanvision::gpu
