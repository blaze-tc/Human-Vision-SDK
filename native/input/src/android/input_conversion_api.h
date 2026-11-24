#pragma once
#include "input_vulkan_proc_loader.h"
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_android.h>
namespace hvinput {
#define INPUT_VK_FUNCTIONS(X)                                                  \
  X(GetAndroidHardwareBufferPropertiesANDROID)                                 \
  X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFormatProperties) X( \
      CreateImage) X(DestroyImage) X(AllocateMemory) X(FreeMemory)             \
      X(BindImageMemory) X(CreateSamplerYcbcrConversion) X(                    \
          DestroySamplerYcbcrConversion) X(CreateSampler) X(DestroySampler)    \
          X(CreateImageView) X(DestroyImageView) X(CreateDescriptorSetLayout)  \
              X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(         \
                  DestroyDescriptorPool) X(AllocateDescriptorSets)             \
                  X(UpdateDescriptorSets) X(CreateShaderModule) X(             \
                      DestroyShaderModule) X(CreatePipelineLayout)             \
                      X(DestroyPipelineLayout) X(CreateComputePipelines) X(    \
                          DestroyPipeline) X(CreateCommandPool)                \
                          X(DestroyCommandPool) X(AllocateCommandBuffers) X(   \
                              ResetCommandBuffer) X(BeginCommandBuffer)        \
                              X(EndCommandBuffer) X(CmdCopyImage) X(           \
                                  CmdPipelineBarrier) X(CmdBindPipeline)       \
                                  X(CmdBindDescriptorSets) X(CmdPushConstants) \
                                      X(CmdDispatch) X(CreateFence)            \
                                          X(DestroyFence) X(GetFenceStatus) X( \
                                              ResetFences) X(CreateSemaphore)  \
                                              X(DestroySemaphore)              \
                                                  X(ImportSemaphoreFdKHR)      \
                                                      X(GetSemaphoreFdKHR)     \
                                                          X(QueueSubmit)
struct InputConversionApi {
#define DECLARE(name) PFN_vk##name name = nullptr;
  INPUT_VK_FUNCTIONS(DECLARE)
#undef DECLARE
  bool LoadFunctions(VkDevice device, VkInstance instance,
                     PFN_vkGetInstanceProcAddr instance_proc) {
    if (device == VK_NULL_HANDLE || instance == VK_NULL_HANDLE ||
        instance_proc == nullptr)
      return false;
    const auto gd = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        instance_proc(instance, "vkGetDeviceProcAddr"));
    if (!gd)
      return false;
#define LOAD(name)                                                             \
  name = reinterpret_cast<PFN_vk##name>(LoadInputVulkanProc(                   \
      "vk" #name, [&](const char *n) { return gd(device, n); },                \
      [&](const char *n) { return instance_proc(instance, n); }));             \
  if (!name)                                                                   \
    return false;
    INPUT_VK_FUNCTIONS(LOAD)
#undef LOAD
    return true;
  }
};
#undef INPUT_VK_FUNCTIONS
// A complete snapshot is necessary, but does not prove device lifetime or
// external-memory qualification. Callers must also enforce their active epoch.
template<class Context>
inline const char* InputVulkanContextError(const Context& context) noexcept {
  if (!context.instance) return "Unity live Vulkan instance is unavailable";
  if (!context.physicalDevice) return "Unity live Vulkan physical device is unavailable";
  if (!context.device) return "Unity live Vulkan logical device is unavailable";
  if (!context.graphicsQueue) return "Unity live Vulkan graphics queue is unavailable";
  if (!context.getInstanceProcAddr) return "Unity live Vulkan vkGetInstanceProcAddr is unavailable";
  return nullptr;
}
} // namespace hvinput
