#pragma once
#include "decoded_sampling_contract.h"
#include "android_input_gpu.h"
#include "input_buffer_registry.h"
#include "input_vulkan_proc_loader.h"
#include "input_yuv_shader.h"
#include <android/log.h>
#include <cstring>
#include <functional>
#include <memory>
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
struct Imported {
  const void *reader = nullptr;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, 2> descriptors{};
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  bool reference = false;
};
struct Counters {
  uint64_t creates = 0, destroys = 0, submits = 0, completes = 0,
           positive_waits = 0, already_complete = 0, exports = 0,
           ownership_acquires = 0, ownership_returns = 0, queue_callbacks = 0,
           target_views_created = 0, target_views_destroyed = 0,
           source_views_created = 0, source_views_destroyed = 0,
           pipelines_created = 0, pipelines_destroyed = 0,
           descriptor_pools_created = 0, descriptor_pools_destroyed = 0,
           cache_ref_acquires = 0, cache_ref_releases = 0, errors = 0;
};

class InputConversionImporter : public ImageImporter {
  InputConversionApi &api;
  InputBufferRegistry &buffer_registry;
  Counters &counters;
  uint64_t &completed_token;
  std::function<void(const char *, VkResult)> report;
  void Error(const char *stage, VkResult code = VK_ERROR_UNKNOWN) {
    report(stage, code);
  }

public:
  VkDevice device{};
  VkPhysicalDevice physical_device{};
  VkPipelineCache pipeline_cache{};
  InputConversionImporter(InputConversionApi &functions,
                          InputBufferRegistry &registry, Counters &stats,
                          uint64_t &completed,
                          std::function<void(const char *, VkResult)> error)
      : api(functions), buffer_registry(registry), counters(stats),
        completed_token(completed), report(std::move(error)) {}
  void DestroyResource(CacheEntry &e) {
    auto *r = static_cast<Imported *>(e.resource);
    if (!r)
      return;
    auto d = device;
    // Registry mutex is released before any Vulkan/AHB destruction.
    buffer_registry.Unregister(r->reader, e.buffer);
    if (r->pipeline) {
      api.DestroyPipeline(d, r->pipeline, nullptr);
      ++counters.pipelines_destroyed;
    }
    if (r->layout)
      api.DestroyPipelineLayout(d, r->layout, nullptr);
    if (r->descriptor_pool) {
      api.DestroyDescriptorPool(d, r->descriptor_pool, nullptr);
      ++counters.descriptor_pools_destroyed;
    }
    if (r->set_layout)
      api.DestroyDescriptorSetLayout(d, r->set_layout, nullptr);
    if (r->view) {
      api.DestroyImageView(d, r->view, nullptr);
      ++counters.source_views_destroyed;
    }
    if (r->sampler)
      api.DestroySampler(d, r->sampler, nullptr);
    if (r->conversion)
      api.DestroySamplerYcbcrConversion(d, r->conversion, nullptr);
    if (r->image)
      api.DestroyImage(d, r->image, nullptr);
    if (r->memory)
      api.FreeMemory(d, r->memory, nullptr);
    if (r->reference) {
      AHardwareBuffer_release(static_cast<AHardwareBuffer *>(e.buffer));
      ++counters.cache_ref_releases;
    }
    delete r;
    e.resource = nullptr;
  }

  const void *source_reader = nullptr;
  bool Complete(uint64_t token) override { return token <= completed_token; }
  void Destroy(CacheEntry &e) override {
    DestroyResource(e);
    ++counters.destroys;
  }
  bool Create(CacheEntry &e) override {
    InputBufferReservation reservation(buffer_registry, source_reader,
                                       e.buffer);
    if (!reservation) {
      Error("known AHB registry domain or identity");
      return false;
    }
    auto resource = std::make_unique<Imported>();
    resource->reader = source_reader;
    e.resource = resource.get();
    auto d = device;
    const auto check = [&](VkResult v, const char *stage) {
      if (v == VK_SUCCESS)
        return true;
      Error(stage, v);
      return false;
    };
    bool success = false;
    const auto work = [&]() -> bool {
      const auto &c = e.contract;
      VkExternalFormatANDROID external{
          VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID, nullptr,
          c.external_format};
      VkExternalMemoryImageCreateInfo external_image{
          VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, &external,
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
      VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               &external_image};
      create.imageType = VK_IMAGE_TYPE_2D;
      create.format = static_cast<VkFormat>(c.format);
      create.extent = {c.width, c.height, 1};
      create.mipLevels = 1;
      create.arrayLayers = 1;
      create.samples = VK_SAMPLE_COUNT_1_BIT;
      create.tiling = VK_IMAGE_TILING_OPTIMAL;
      create.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      if (!check(api.CreateImage(d, &create, nullptr, &resource->image),
                 "create readonly AHB image"))
        return false;
      VkAndroidHardwareBufferPropertiesANDROID properties{
          VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
      if (!check(api.GetAndroidHardwareBufferPropertiesANDROID(
                     d, static_cast<AHardwareBuffer *>(e.buffer), &properties),
                 "query import allocation"))
        return false;
      VkPhysicalDeviceMemoryProperties memory{};
      api.GetPhysicalDeviceMemoryProperties(physical_device, &memory);
      uint32_t index = UINT32_MAX;
      for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if (properties.memoryTypeBits & (1u << i)) {
          index = i;
          break;
        }
      if (index == UINT32_MAX) {
        Error("AHB import memory type");
        return false;
      }
      VkImportAndroidHardwareBufferInfoANDROID import{
          VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
          nullptr, static_cast<AHardwareBuffer *>(e.buffer)};
      VkMemoryDedicatedAllocateInfo dedicated{
          VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, &import,
          resource->image, VK_NULL_HANDLE};
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                      &dedicated, properties.allocationSize,
                                      index};
      if (!check(api.AllocateMemory(d, &allocation, nullptr, &resource->memory),
                 "import AHB memory") ||
          !check(api.BindImageMemory(d, resource->image, resource->memory, 0),
                 "bind imported AHB"))
        return false;
      VkSamplerYcbcrConversionCreateInfo conversion{
          VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO, &external};
      conversion.format = static_cast<VkFormat>(c.format);
      conversion.ycbcrModel =
          static_cast<VkSamplerYcbcrModelConversion>(c.model);
      conversion.ycbcrRange = static_cast<VkSamplerYcbcrRange>(c.range);
      conversion.components = {
          static_cast<VkComponentSwizzle>(c.components[0]),
          static_cast<VkComponentSwizzle>(c.components[1]),
          static_cast<VkComponentSwizzle>(c.components[2]),
          static_cast<VkComponentSwizzle>(c.components[3])};
      conversion.xChromaOffset = static_cast<VkChromaLocation>(c.x_chroma);
      conversion.yChromaOffset = static_cast<VkChromaLocation>(c.y_chroma);
      conversion.chromaFilter =
          (c.features &
           VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
              ? VK_FILTER_LINEAR
              : VK_FILTER_NEAREST;
      if (!check(api.CreateSamplerYcbcrConversion(d, &conversion, nullptr,
                                                  &resource->conversion),
                 "create actual YCbCr conversion"))
        return false;
      VkSamplerYcbcrConversionInfo converted{
          VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO, nullptr,
          resource->conversion};
      VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                  &converted};
      sampler.magFilter = sampler.minFilter = conversion.chromaFilter;
      sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampler.maxLod = 0;
      if (!check(api.CreateSampler(d, &sampler, nullptr, &resource->sampler),
                 "create immutable converted sampler"))
        return false;
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 &converted};
      view.image = resource->image;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = static_cast<VkFormat>(c.format);
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      if (!check(api.CreateImageView(d, &view, nullptr, &resource->view),
                 "create converted source view"))
        return false;
      ++counters.source_views_created;
      VkDescriptorSetLayoutBinding bindings[] = {
          {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
           VK_SHADER_STAGE_COMPUTE_BIT, &resource->sampler},
          {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
           nullptr}};
      VkDescriptorSetLayoutCreateInfo set{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2,
          bindings};
      if (!check(api.CreateDescriptorSetLayout(d, &set, nullptr,
                                               &resource->set_layout),
                 "create conversion descriptor layout"))
        return false;
      // External opaque formats have no valid VkFormat query. The pool is
      // deliberately overprovisioned; successful allocation qualifies this
      // device. This is resource capacity, not an assertion about the external
      // plane count.
      constexpr uint32_t descriptor_budget = 16;
      VkDescriptorPoolSize sizes[] = {
          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * descriptor_budget},
          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
      VkDescriptorPoolCreateInfo pool{
          VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          nullptr,
          0,
          2,
          2,
          sizes};
      if (!check(api.CreateDescriptorPool(d, &pool, nullptr,
                                          &resource->descriptor_pool),
                 "create descriptor pool"))
        return false;
      ++counters.descriptor_pools_created;
      const VkDescriptorSetLayout layouts[] = {resource->set_layout,
                                               resource->set_layout};
      VkDescriptorSetAllocateInfo descriptors{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
          resource->descriptor_pool, 2, layouts};
      if (!check(api.AllocateDescriptorSets(d, &descriptors,
                                            resource->descriptors.data()),
                 "allocate converted descriptors"))
        return false;
      VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
      VkPipelineLayoutCreateInfo layout{
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          nullptr,
          0,
          1,
          &resource->set_layout,
          1,
          &push};
      if (!check(
              api.CreatePipelineLayout(d, &layout, nullptr, &resource->layout),
              "create conversion pipeline layout"))
        return false;
      VkShaderModule module = VK_NULL_HANDLE;
      VkShaderModuleCreateInfo shader{
          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
          sizeof(input_yuv_shader), input_yuv_shader};
      if (!check(api.CreateShaderModule(d, &shader, nullptr, &module),
                 "create qualified input shader"))
        return false;
      VkComputePipelineCreateInfo pipeline{
          VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        nullptr,
                        0,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        module,
                        "main",
                        nullptr};
      pipeline.layout = resource->layout;
      auto result = api.CreateComputePipelines(d, pipeline_cache, 1, &pipeline,
                                               nullptr, &resource->pipeline);
      api.DestroyShaderModule(d, module, nullptr);
      if (!check(result, "create cached YCbCr pipeline"))
        return false;
      ++counters.pipelines_created;
      AHardwareBuffer_acquire(static_cast<AHardwareBuffer *>(e.buffer));
      resource->reference = true;
      ++counters.cache_ref_acquires;
      __android_log_print(
          ANDROID_LOG_INFO, "HVInputGate",
          "ahb_import_created buffer=%p reader=%p external_format=%llu "
          "width=%u height=%u model=%u range=%u descriptors=%u",
          e.buffer, source_reader, (unsigned long long)c.external_format,
          c.width, c.height, c.model, c.range, descriptor_budget);
      return true;
    };
    success = work();
    resource.release();
    if (!success) {
      DestroyResource(e);
      return false;
    }
    if (!reservation.Commit()) {
      Error("known AHB registry reservation lost");
      DestroyResource(e);
      return false;
    }
    ++counters.creates;
    return true;
  }
};
inline void RecordInputConversion(InputConversionApi &api, VkDevice d,
                                  VkCommandBuffer command, Imported *r,
                                  VkDescriptorSet descriptor, VkImageView view,
                                  uint32_t width, uint32_t height,
                                  const AndroidDecodedImage &image,
                                  const ImageContract &contract, int rotation,
                                  bool mirror, bool top_left) {
  VkDescriptorImageInfo images[] = {
      {r->sampler, r->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[2]{};
  for (int i = 0; i < 2; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = descriptor;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                 : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = &images[i];
  }
  api.UpdateDescriptorSets(d, 2, writes, 0, nullptr);
  struct Push {
    float crop[4];
    int32_t info[4];
  } push{{float(image.crop_left) / contract.width,
          float(image.crop_top) / contract.height,
          float(image.crop_right) / contract.width,
          float(image.crop_bottom) / contract.height},
         {int32_t(width), int32_t(height), rotation,
          int32_t(mirror) | (top_left ? 2 : 0)}};
  api.CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, r->layout,
                            0, 1, &descriptor, 0, nullptr);
  api.CmdPushConstants(command, r->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(push), &push);
  api.CmdDispatch(command, (width + 7) / 8, (height + 7) / 8, 1);
}
inline ImageContract DecodedImageContract(const AndroidDecodedImage &image,
                                          const InputGpuCapabilities &c) {
  auto contract = DecodedSamplingContract(c);
  AHardwareBuffer_Desc desc{};
  AHardwareBuffer_describe(image.buffer, &desc);
  contract.width = desc.width;
  contract.height = desc.height;
  if (image.matrix == 1)
    contract.model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
  else if (image.matrix == 2)
    contract.model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
  if (image.color_range == 1)
    contract.range = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
  else if (image.color_range == 2)
    contract.range = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
  return contract;
}
} // namespace hvinput
