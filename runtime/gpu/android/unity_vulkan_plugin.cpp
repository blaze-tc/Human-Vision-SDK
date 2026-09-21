#include "gpu/android/unity_vulkan_plugin.h"

#if defined(__ANDROID__)

#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include "IUnityInterface.h"
#include "gpu/android/shaders/embedded_shaders.h"
#include "gpu/vulkan/vulkan_device_identity.h"

#include <android/hardware_buffer.h>
#include <unistd.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace humanvision::gpu {
namespace {

struct AndroidSlot {
  AHardwareBuffer *ahb = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkSemaphore semaphore = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkShaderModule vertex = VK_NULL_HANDLE;
  VkShaderModule fragment = VK_NULL_HANDLE;
  VkImageView source_view = VK_NULL_HANDLE;
  VkImage source_image = VK_NULL_HANDLE;
  VkFormat source_format = VK_FORMAT_UNDEFINED;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0;
  bool destination_initialized = false;
  ~AndroidSlot() {
    if (!device)
      return;
    if (source_view)
      vkDestroyImageView(device, source_view, nullptr);
    if (pipeline)
      vkDestroyPipeline(device, pipeline, nullptr);
    if (vertex)
      vkDestroyShaderModule(device, vertex, nullptr);
    if (fragment)
      vkDestroyShaderModule(device, fragment, nullptr);
    if (pipeline_layout)
      vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    if (framebuffer)
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    if (render_pass)
      vkDestroyRenderPass(device, render_pass, nullptr);
    if (descriptor_pool)
      vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    if (descriptor_layout)
      vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    if (sampler)
      vkDestroySampler(device, sampler, nullptr);
    if (semaphore)
      vkDestroySemaphore(device, semaphore, nullptr);
    if (command_pool)
      vkDestroyCommandPool(device, command_pool, nullptr);
    if (view)
      vkDestroyImageView(device, view, nullptr);
    if (image)
      vkDestroyImage(device, image, nullptr);
    if (memory)
      vkFreeMemory(device, memory, nullptr);
    // Future ncnn owner, Unity importer owner, allocation owner (last).
    if (ahb) {
      AHardwareBuffer_release(ahb);
      AHardwareBuffer_release(ahb);
      AHardwareBuffer_release(ahb);
    }
  }
};

class AndroidProducer {
public:
  AndroidProducer() : bridge_(MakeDispatch()) {}

  UnityVulkanBridgeDispatch MakeDispatch() noexcept {
    return {this, Create, Drain,  Access, Release, QueueAccess,
            Blit, Color,  Submit, Export, Cancel};
  }

  bool Configure(const AhbSelection &selection,
                 const SlotContract &contract) noexcept {
    std::lock_guard<std::mutex> lock(control_);
    if (!vulkan_ || !have_device_) {
      diagnostic_ = "Unity Vulkan device is not initialized";
      return false;
    }
    return bridge_.Initialize(device_context_, selection, contract);
  }
  void Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(control_);
    bridge_.Shutdown();
  }
  BridgeResult Prepare(const HV_AndroidGpuSubmissionV1 &s,
                       void **out) noexcept {
    return bridge_.Prepare(s, out);
  }
  void Status(HV_AndroidGpuBridgeStatusV1 &s) noexcept { bridge_.GetStatus(s); }
  const char *Diagnostic() const noexcept { return diagnostic_.c_str(); }

  void Load(IUnityInterfaces *interfaces) noexcept {
    interfaces_ = interfaces;
    graphics_ = interfaces ? interfaces->Get<IUnityGraphics>() : nullptr;
    vulkan_ = interfaces ? interfaces->Get<IUnityGraphicsVulkanV2>() : nullptr;
    if (graphics_) {
      graphics_->RegisterDeviceEventCallback(DeviceEvent);
      DeviceEvent(kUnityGfxDeviceEventInitialize);
    }
  }
  void Unload() noexcept {
    if (graphics_)
      graphics_->UnregisterDeviceEventCallback(DeviceEvent);
    OnDevice(kUnityGfxDeviceEventShutdown);
    graphics_ = nullptr;
    vulkan_ = nullptr;
    interfaces_ = nullptr;
  }
  void OnDevice(UnityGfxDeviceEventType event) noexcept {
    std::lock_guard<std::mutex> lock(control_);
    if (event == kUnityGfxDeviceEventShutdown ||
        event == kUnityGfxDeviceEventBeforeReset) {
      bridge_.Shutdown();
      have_device_ = false;
      device_context_ = {};
      return;
    }
    if (event != kUnityGfxDeviceEventInitialize &&
        event != kUnityGfxDeviceEventAfterReset)
      return;
    if (!graphics_ || graphics_->GetRenderer() != kUnityGfxRendererVulkan ||
        !vulkan_) {
      diagnostic_ =
          "Android GPU bridge requires Unity Vulkan as the active renderer";
      return;
    }
    const UnityVulkanInstance instance = vulkan_->Instance();
    if (!instance.instance || !instance.physicalDevice || !instance.device ||
        !instance.graphicsQueue) {
      diagnostic_ = "Unity returned an incomplete Vulkan device context";
      return;
    }
    device_context_.instance = reinterpret_cast<uintptr_t>(instance.instance);
    device_context_.physical_device =
        reinterpret_cast<uintptr_t>(instance.physicalDevice);
    device_context_.device = reinterpret_cast<uintptr_t>(instance.device);
    device_context_.graphics_queue =
        reinterpret_cast<uintptr_t>(instance.graphicsQueue);
    device_context_.graphics_queue_family = instance.queueFamilyIndex;
    VulkanDeviceContext query{};
    query.instance = instance.instance;
    query.physical_device = instance.physicalDevice;
    query.device = instance.device;
    const DeviceIdentity id = QueryDeviceIdentity(query);
    device_context_.device_uuid = id.device_uuid;
    device_context_.driver_uuid = id.driver_uuid;
    have_device_ = true;
    diagnostic_.clear();
  }

  static AndroidProducer &Get() {
    static AndroidProducer producer;
    return producer;
  }

private:
  static void UNITY_INTERFACE_API DeviceEvent(UnityGfxDeviceEventType event) {
    Get().OnDevice(event);
  }
  static VkDevice D(const UnityVulkanDeviceContext &c) {
    return reinterpret_cast<VkDevice>(c.device);
  }
  static VkPhysicalDevice P(const UnityVulkanDeviceContext &c) {
    return reinterpret_cast<VkPhysicalDevice>(c.physical_device);
  }
  static VkQueue Q(const UnityVulkanDeviceContext &c) {
    return reinterpret_cast<VkQueue>(c.graphics_queue);
  }
  static AndroidSlot *S(const UnityVulkanSlotCache &c) {
    return reinterpret_cast<AndroidSlot *>(c.ahb);
  }

  static uint32_t MemoryType(VkPhysicalDevice physical,
                             uint32_t bits) noexcept {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
      if (bits & (1u << i))
        return i;
    return UINT32_MAX;
  }
  static VkImageLayout Layout(BridgeImageLayout l) noexcept {
    switch (l) {
    case BridgeImageLayout::TransferSource:
      return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case BridgeImageLayout::TransferDestination:
      return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case BridgeImageLayout::ShaderRead:
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case BridgeImageLayout::ColorAttachment:
      return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case BridgeImageLayout::External:
      return VK_IMAGE_LAYOUT_GENERAL;
    default:
      return VK_IMAGE_LAYOUT_GENERAL;
    }
  }
  static VkAccessFlags SrcAccess(BridgeImageLayout l) noexcept {
    if (l == BridgeImageLayout::TransferSource)
      return VK_ACCESS_TRANSFER_READ_BIT;
    if (l == BridgeImageLayout::TransferDestination)
      return VK_ACCESS_TRANSFER_WRITE_BIT;
    if (l == BridgeImageLayout::ShaderRead)
      return VK_ACCESS_SHADER_READ_BIT;
    if (l == BridgeImageLayout::ColorAttachment)
      return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    return 0;
  }
  static VkAccessFlags DstAccess(BridgeImageLayout l) noexcept {
    return SrcAccess(l);
  }
  static VkPipelineStageFlags Stage(BridgeImageLayout l) noexcept {
    if (l == BridgeImageLayout::TransferSource ||
        l == BridgeImageLayout::TransferDestination)
      return VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (l == BridgeImageLayout::ShaderRead)
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (l == BridgeImageLayout::ColorAttachment)
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  }
  static VkImageMemoryBarrier Barrier(const BridgeBarrier &b) noexcept {
    VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    v.srcAccessMask = SrcAccess(b.old_layout);
    v.dstAccessMask = DstAccess(b.new_layout);
    v.oldLayout = Layout(b.old_layout);
    v.newLayout = Layout(b.new_layout);
    v.srcQueueFamilyIndex = b.source_queue_family;
    v.dstQueueFamilyIndex = b.destination_queue_family;
    v.image = reinterpret_cast<VkImage>(b.image);
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return v;
  }
  static bool Module(VkDevice d, const uint32_t *words, size_t bytes,
                     VkShaderModule &out) noexcept {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = words;
    return vkCreateShaderModule(d, &ci, nullptr, &out) == VK_SUCCESS;
  }
  static bool CreateColor(AndroidSlot &s) noexcept {
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1;
    if (vkCreateSampler(s.device, &sci, nullptr, &s.sampler) != VK_SUCCESS)
      return false;
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(s.device, &dl, nullptr,
                                    &s.descriptor_layout) != VK_SUCCESS)
      return false;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dp{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(s.device, &dp, nullptr, &s.descriptor_pool) !=
        VK_SUCCESS)
      return false;
    VkDescriptorSetAllocateInfo da{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = s.descriptor_pool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &s.descriptor_layout;
    if (vkAllocateDescriptorSets(s.device, &da, &s.descriptor) != VK_SUCCESS)
      return false;
    VkAttachmentDescription attachment{};
    attachment.format = s.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &attachment;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    if (vkCreateRenderPass(s.device, &rp, nullptr, &s.render_pass) !=
        VK_SUCCESS)
      return false;
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = s.render_pass;
    fb.attachmentCount = 1;
    fb.pAttachments = &s.view;
    fb.width = s.width;
    fb.height = s.height;
    fb.layers = 1;
    if (vkCreateFramebuffer(s.device, &fb, nullptr, &s.framebuffer) !=
        VK_SUCCESS)
      return false;
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pl{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &s.descriptor_layout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(s.device, &pl, nullptr, &s.pipeline_layout) !=
            VK_SUCCESS ||
        !Module(s.device, shaders::kFullscreenVertexSpv,
                sizeof(shaders::kFullscreenVertexSpv), s.vertex) ||
        !Module(s.device, shaders::kCopyRgbaFragmentSpv,
                sizeof(shaders::kCopyRgbaFragmentSpv), s.fragment))
      return false;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr,
                 0,
                 VK_SHADER_STAGE_VERTEX_BIT,
                 s.vertex,
                 "main",
                 nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr,
                 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT,
                 s.fragment,
                 "main",
                 nullptr};
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, float(s.width), float(s.height), 0, 1};
    VkRect2D scissor{{0, 0}, {s.width, s.height}};
    VkPipelineViewportStateCreateInfo vp{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gp{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.layout = s.pipeline_layout;
    gp.renderPass = s.render_pass;
    return vkCreateGraphicsPipelines(s.device, VK_NULL_HANDLE, 1, &gp, nullptr,
                                     &s.pipeline) == VK_SUCCESS;
  }
  static bool Create(void *, uint32_t, const UnityVulkanDeviceContext &c,
                     const SlotContract &contract,
                     const AhbSelection &selection,
                     UnityVulkanSlotCache &out) noexcept {
    auto slot = std::make_unique<AndroidSlot>();
    slot->device = D(c);
    slot->width = contract.width;
    slot->height = contract.height;
    const auto it = std::find_if(
        selection.candidates.begin(), selection.candidates.end(),
        [&](const AhbCandidate &x) { return x.path == selection.path; });
    if (it == selection.candidates.end() || !it->producer.vk_format)
      return false;
    slot->format = static_cast<VkFormat>(it->producer.vk_format);
    AHardwareBuffer_Desc desc{};
    desc.width = contract.width;
    desc.height = contract.height;
    desc.layers = 1;
    desc.format = contract.actual_format;
    desc.usage = contract.actual_usage;
    if (AHardwareBuffer_allocate(&desc, &slot->ahb) != 0)
      return false;
    // Allocation, Unity importer and future ncnn importer each retain one
    // generation-long owner reference.
    AHardwareBuffer_acquire(slot->ahb);
    AHardwareBuffer_acquire(slot->ahb);
    VkExternalMemoryImageCreateInfo ext{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &ext};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = slot->format;
    image.extent = {contract.width, contract.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = it->producer.image_usage;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(slot->device, &image, nullptr, &slot->image) !=
        VK_SUCCESS)
      return false;
    const auto get_ahb_properties =
        reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            vkGetDeviceProcAddr(slot->device,
                                "vkGetAndroidHardwareBufferPropertiesANDROID"));
    VkAndroidHardwareBufferPropertiesANDROID props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    if (!get_ahb_properties ||
        get_ahb_properties(slot->device, slot->ahb, &props) != VK_SUCCESS)
      return false;
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(slot->device, slot->image, &req);
    uint32_t type = MemoryType(P(c), req.memoryTypeBits & props.memoryTypeBits);
    if (type == UINT32_MAX)
      return false;
    VkImportAndroidHardwareBufferInfoANDROID imp{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, nullptr,
        slot->ahb};
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp,
                               props.allocationSize, type};
    if (vkAllocateMemory(slot->device, &alloc, nullptr, &slot->memory) !=
            VK_SUCCESS ||
        vkBindImageMemory(slot->device, slot->image, slot->memory, 0) !=
            VK_SUCCESS)
      return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = slot->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = slot->format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(slot->device, &view, nullptr, &slot->view) !=
        VK_SUCCESS)
      return false;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = c.graphics_queue_family;
    if (vkCreateCommandPool(slot->device, &pool, nullptr,
                            &slot->command_pool) != VK_SUCCESS)
      return false;
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = slot->command_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(slot->device, &ca, &slot->command) !=
        VK_SUCCESS)
      return false;
    VkExportSemaphoreCreateInfo export_info{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                              &export_info};
    if (vkCreateSemaphore(slot->device, &sem, nullptr, &slot->semaphore) !=
        VK_SUCCESS)
      return false;
    if (selection.path == HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT &&
        !CreateColor(*slot))
      return false;
    out.ahb = reinterpret_cast<uintptr_t>(slot.get());
    out.image = reinterpret_cast<uintptr_t>(slot->image);
    out.memory = reinterpret_cast<uintptr_t>(slot->memory);
    out.image_view = reinterpret_cast<uintptr_t>(slot->view);
    out.command_buffer = reinterpret_cast<uintptr_t>(slot->command);
    out.export_semaphore = reinterpret_cast<uintptr_t>(slot->semaphore);
    out.sampler = reinterpret_cast<uintptr_t>(slot->sampler);
    out.descriptor_set_layout =
        reinterpret_cast<uintptr_t>(slot->descriptor_layout);
    out.descriptor_pool = reinterpret_cast<uintptr_t>(slot->descriptor_pool);
    out.descriptor_set = reinterpret_cast<uintptr_t>(slot->descriptor);
    out.render_pass = reinterpret_cast<uintptr_t>(slot->render_pass);
    out.framebuffer = reinterpret_cast<uintptr_t>(slot->framebuffer);
    out.pipeline_layout = reinterpret_cast<uintptr_t>(slot->pipeline_layout);
    out.pipeline = reinterpret_cast<uintptr_t>(slot->pipeline);
    slot.release();
    return true;
  }
  static void Drain(void *, uint32_t, AhbSlotState, UnityVulkanSlotCache &cache,
                    SyncFd &fd) noexcept {
    fd.Reset();
    std::unique_ptr<AndroidSlot> s(S(cache));
    if (!s)
      return;
    vkDeviceWaitIdle(s->device);
    cache = {};
  }
  static bool Access(void *p, void *texture, UnityTextureAccess &out) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    if (!self.vulkan_)
      return false;
    UnityVulkanImage observed{};
    if (!self.vulkan_->AccessTexture(
            texture, UnityVulkanWholeImage, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            kUnityVulkanResourceAccess_ObserveOnly, &observed))
      return false;
    const VkImageLayout prior = observed.layout;
    const VkImageLayout requested = VK_IMAGE_LAYOUT_GENERAL;
    if (!self.vulkan_->AccessTexture(
            texture, UnityVulkanWholeImage, requested,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT,
            kUnityVulkanResourceAccess_PipelineBarrier, &observed))
      return false;
    out.image = reinterpret_cast<uintptr_t>(observed.image);
    out.layout = BridgeImageLayout::SourceCurrent;
    {
      std::lock_guard<std::mutex> lock(self.access_);
      self.prior_layout_ = prior;
      self.source_format_ = observed.format;
      self.source_extent_ = observed.extent;
    }
    return true;
  }
  static void Release(void *p, void *texture,
                      const UnityTextureAccess &) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    if (!self.vulkan_)
      return;
    UnityVulkanImage ignored{};
    VkImageLayout prior;
    {
      std::lock_guard<std::mutex> lock(self.access_);
      prior = self.prior_layout_;
    }
    self.vulkan_->AccessTexture(
        texture, UnityVulkanWholeImage, prior,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        kUnityVulkanResourceAccess_PipelineBarrier, &ignored);
  }
  static void UNITY_INTERFACE_API QueueCallback(int, void *data) noexcept {
    UnityVulkanBridge::QueueEvent(data);
    auto &self = Get();
    self.queued_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    self.queue_cv_.notify_all();
  }
  static bool QueueAccess(void *p, void (*callback)(void *) noexcept,
                          void *data) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    if (!self.vulkan_ || callback != &UnityVulkanBridge::QueueEvent)
      return false;
    self.queued_callbacks_.fetch_add(1, std::memory_order_acq_rel);
    self.vulkan_->AccessQueue(&QueueCallback, 0, data, true);
    return true;
  }
  static bool Begin(AndroidSlot &s, const BridgeBarrier *b,
                    uint32_t n) noexcept {
    if (n != 4 || vkResetCommandBuffer(s.command, 0) != VK_SUCCESS)
      return false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(s.command, &bi) != VK_SUCCESS)
      return false;
    for (uint32_t i = 0; i < 2; ++i) {
      auto v = Barrier(b[i]);
      if (i == 1 && !s.destination_initialized)
        v.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vkCmdPipelineBarrier(s.command, Stage(b[i].old_layout),
                           Stage(b[i].new_layout), 0, 0, nullptr, 0, nullptr, 1,
                           &v);
    }
    return true;
  }
  static void EndBarriers(AndroidSlot &s, const BridgeBarrier *b) noexcept {
    for (uint32_t i = 2; i < 4; ++i) {
      auto v = Barrier(b[i]);
      vkCmdPipelineBarrier(s.command, Stage(b[i].old_layout),
                           Stage(b[i].new_layout), 0, 0, nullptr, 0, nullptr, 1,
                           &v);
    }
  }
  static bool Blit(void *, const UnityVulkanSlotCache &c,
                   const UnityTextureAccess &, const BridgeBarrier *b,
                   uint32_t n) noexcept {
    auto *s = S(c);
    if (!s || !Begin(*s, b, n))
      return false;
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {int32_t(s->width), int32_t(s->height), 1};
    region.dstSubresource = region.srcSubresource;
    region.dstOffsets[1] = region.srcOffsets[1];
    vkCmdBlitImage(s->command, reinterpret_cast<VkImage>(b[0].image),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_LINEAR);
    EndBarriers(*s, b);
    return vkEndCommandBuffer(s->command) == VK_SUCCESS;
  }
  static bool Color(void *p, const UnityVulkanSlotCache &c,
                    const UnityTextureAccess &, const BridgeBarrier *b,
                    uint32_t n, bool gpu_shader_conversion) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    auto *s = S(c);
    if (!s || !gpu_shader_conversion || !Begin(*s, b, n))
      return false;
    VkFormat format;
    VkExtent3D extent;
    {
      std::lock_guard<std::mutex> lock(self.access_);
      format = self.source_format_;
      extent = self.source_extent_;
    }
    if (!s->source_view ||
        s->source_image != reinterpret_cast<VkImage>(b[0].image)) {
      if (s->source_view)
        vkDestroyImageView(s->device, s->source_view, nullptr);
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = reinterpret_cast<VkImage>(b[0].image);
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = format;
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      if (vkCreateImageView(s->device, &vi, nullptr, &s->source_view) !=
          VK_SUCCESS)
        return false;
      s->source_image = vi.image;
      s->source_format = format;
    }
    VkDescriptorImageInfo di{s->sampler, s->source_view,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = s->descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &di;
    vkUpdateDescriptorSets(s->device, 1, &write, 0, nullptr);
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = s->render_pass;
    rp.framebuffer = s->framebuffer;
    rp.renderArea.extent = {s->width, s->height};
    vkCmdBeginRenderPass(s->command, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(s->command, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline);
    vkCmdBindDescriptorSets(s->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            s->pipeline_layout, 0, 1, &s->descriptor, 0,
                            nullptr);
    uint32_t swap = (format == VK_FORMAT_B8G8R8A8_UNORM ||
                     format == VK_FORMAT_B8G8R8A8_SRGB)
                        ? 1u
                        : 0u;
    vkCmdPushConstants(s->command, s->pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &swap);
    vkCmdDraw(s->command, 3, 1, 0, 0);
    vkCmdEndRenderPass(s->command);
    EndBarriers(*s, b);
    (void)extent;
    return vkEndCommandBuffer(s->command) == VK_SUCCESS;
  }
  static bool Submit(void *, const UnityVulkanDeviceContext &dc,
                     const UnityVulkanSlotCache &c) noexcept {
    auto *s = S(c);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s->command;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &s->semaphore;
    const bool ok = vkQueueSubmit(Q(dc), 1, &si, VK_NULL_HANDLE) == VK_SUCCESS;
    if (ok)
      s->destination_initialized = true;
    return ok;
  }
  static bool Export(void *, const UnityVulkanDeviceContext &dc,
                     const UnityVulkanSlotCache &c, SyncFd &out) noexcept {
    auto *s = S(c);
    auto fn = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(D(dc), "vkGetSemaphoreFdKHR"));
    if (!fn)
      return false;
    VkSemaphoreGetFdInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    info.semaphore = s->semaphore;
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -2;
    if (fn(D(dc), &info, &fd) != VK_SUCCESS)
      return false;
    out = SyncFd(
        fd,
        [](void *, int owned) noexcept {
          if (owned >= 0)
            ::close(owned);
        },
        nullptr);
    return out.HasPayload();
  }
  static void Cancel(void *p) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    std::unique_lock<std::mutex> lock(self.queue_mutex_);
    self.queue_cv_.wait(lock, [&] {
      return self.queued_callbacks_.load(std::memory_order_acquire) == 0;
    });
    if (self.have_device_)
      vkDeviceWaitIdle(reinterpret_cast<VkDevice>(self.device_context_.device));
  }

  IUnityInterfaces *interfaces_ = nullptr;
  IUnityGraphics *graphics_ = nullptr;
  IUnityGraphicsVulkanV2 *vulkan_ = nullptr;
  UnityVulkanDeviceContext device_context_{};
  bool have_device_ = false;
  std::string diagnostic_ = "Unity Vulkan device is not initialized";
  std::mutex control_, access_;
  VkImageLayout prior_layout_ = VK_IMAGE_LAYOUT_GENERAL;
  VkFormat source_format_ = VK_FORMAT_UNDEFINED;
  VkExtent3D source_extent_{};
  std::atomic<uint32_t> queued_callbacks_{0};
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  UnityVulkanBridge bridge_;
};
} // namespace

bool ConfigureUnityVulkanProducer(const AhbSelection &s,
                                  const SlotContract &c) noexcept {
  return AndroidProducer::Get().Configure(s, c);
}
void ShutdownUnityVulkanProducer() noexcept {
  AndroidProducer::Get().Shutdown();
}
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &s,
                                     void **out) noexcept {
  return AndroidProducer::Get().Prepare(s, out);
}
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &s) noexcept {
  AndroidProducer::Get().Status(s);
}
void *UnityVulkanRenderEventFunction() noexcept {
  return reinterpret_cast<void *>(&UnityVulkanBridge::RenderEvent);
}
const char *UnityVulkanProducerDiagnostic() noexcept {
  return AndroidProducer::Get().Diagnostic();
}
} // namespace humanvision::gpu

extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces *interfaces) {
  humanvision::gpu::AndroidProducer::Get().Load(interfaces);
}
extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload() {
  humanvision::gpu::AndroidProducer::Get().Unload();
}

#else
namespace humanvision::gpu {
bool ConfigureUnityVulkanProducer(const AhbSelection &,
                                  const SlotContract &) noexcept {
  return false;
}
void ShutdownUnityVulkanProducer() noexcept {}
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &,
                                     void **out) noexcept {
  if (out)
    *out = nullptr;
  return BridgeResult::Closed;
}
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &) noexcept {}
void *UnityVulkanRenderEventFunction() noexcept { return nullptr; }
const char *UnityVulkanProducerDiagnostic() noexcept {
  return "Android Vulkan producer bridge is unavailable in this build";
}
} // namespace humanvision::gpu
#endif
