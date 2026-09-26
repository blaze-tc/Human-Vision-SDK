#pragma once

#include "gpu/android/unity_vulkan_bridge.h"

namespace humanvision::gpu {
struct VulkanDeviceContext;

// Called by the Android backend control path after B2 has measured the exact
// source/AHB/device contract. This never probes or silently chooses a path.
bool ConfigureUnityVulkanProducer(const AhbSelection &,
                                  const SlotContract &) noexcept;
// The caller retains this Unity-owned texture until End returns and the
// retention query is false. Query both before and after End (the worker may
// quarantine while joining). On true, keep a process-lifetime strong texture
// reference, never Release/Destroy it, and stop GPU use until process restart.
// Native code does not own Unity's VkDevice and cannot make device recreation
// safe after unknown GPU completion. End still joins CPU callbacks, without
// attempting an unbounded GPU wait on the quarantined generation.
bool BeginUnityVulkanSourceLease(void *unity_texture) noexcept;
void EndUnityVulkanSourceLease() noexcept;
bool UnityVulkanSourceRequiresRetention() noexcept;
void ShutdownUnityVulkanProducer() noexcept;
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &,
                                     void **) noexcept;
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &) noexcept;
void *UnityVulkanRenderEventFunction() noexcept;
const char *UnityVulkanProducerDiagnostic() noexcept;
// Borrowed only while the Unity source lease and its bridge generation live.
// The runtime GPU worker uses the bridge's claim/retire protocol; the gate
// helpers below remain development-only diagnostics.
UnityVulkanBridge* UnityVulkanProducerBridge() noexcept;
bool UnityVulkanProducerContext(VulkanDeviceContext&) noexcept;
#if defined(HV_ANDROID_GPU_GATE)
const char* UnityVulkanProducerGateError() noexcept;
const char* UnityVulkanProducerGateProbe() noexcept;
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
