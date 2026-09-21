#pragma once

#include "gpu/android/unity_vulkan_bridge.h"

namespace humanvision::gpu {

// Called by the Android backend control path after B2 has measured the exact
// source/AHB/device contract. This never probes or silently chooses a path.
bool ConfigureUnityVulkanProducer(const AhbSelection &,
                                  const SlotContract &) noexcept;
void ShutdownUnityVulkanProducer() noexcept;
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &,
                                     void **) noexcept;
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &) noexcept;
void *UnityVulkanRenderEventFunction() noexcept;
const char *UnityVulkanProducerDiagnostic() noexcept;

} // namespace humanvision::gpu
