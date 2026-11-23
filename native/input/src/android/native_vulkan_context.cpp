#include "native_vulkan_context.h"
#include "decoded_ahb_properties.h"
#include "native_vulkan_readback.h"
#include <algorithm>
#include <cstdio>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
namespace hvinput {
namespace {
constexpr VkFormat rgba = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkImageUsageFlags output_usage =
    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
constexpr auto ahb_handle =
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
constexpr VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                              0, 1};
VkImageCreateInfo Image(uint32_t width, uint32_t height,
                        VkImageUsageFlags usage) {
  VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image.imageType = VK_IMAGE_TYPE_2D;
  image.format = rgba;
  image.extent = {width, height, 1};
  image.mipLevels = image.arrayLayers = 1;
  image.samples = VK_SAMPLE_COUNT_1_BIT;
  image.tiling = VK_IMAGE_TILING_OPTIMAL;
  image.usage = usage;
  return image;
}
VkImageMemoryBarrier Barrier(VkImage image, VkImageLayout from,
                             VkImageLayout to, VkAccessFlags src,
                             VkAccessFlags dst,
                             uint32_t src_family = VK_QUEUE_FAMILY_IGNORED,
                             uint32_t dst_family = VK_QUEUE_FAMILY_IGNORED) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.image = image;
  b.subresourceRange = color_range;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcAccessMask = src;
  b.dstAccessMask = dst;
  b.srcQueueFamilyIndex = src_family;
  b.dstQueueFamilyIndex = dst_family;
  return b;
}
} // namespace
NativeVulkanContext::NativeVulkanContext()
    : importer(*this, registry, stats, completed,
               [this](const char *stage, VkResult r) { Check(r, stage); }),
      cache(importer) {}
bool NativeVulkanContext::Fail(const char *why) {
  error = why;
  return false;
}
bool NativeVulkanContext::Check(VkResult r, const char *stage) {
  if (r == VK_SUCCESS)
    return true;
  char msg[192]{};
  std::snprintf(msg, sizeof(msg), "%s (VkResult %d)", stage, int(r));
  return Fail(msg);
}
uint32_t NativeVulkanContext::MemoryType(uint32_t bits,
                                         VkMemoryPropertyFlags required) const {
  for (uint32_t i = 0; i < memory_.memoryTypeCount; ++i)
    if ((bits & (1u << i)) &&
        (memory_.memoryTypes[i].propertyFlags & required) == required)
      return i;
  return UINT32_MAX;
}
bool NativeVulkanContext::Open(const HV_InputNativeOptionsV1 &config) {
  width_ = config.width;
  height_ = config.height;
  library_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  if (!library_)
    return Fail("Android Vulkan loader unavailable");
  proc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      dlsym(library_, "vkGetInstanceProcAddr"));
  if (!proc_)
    return Fail("vkGetInstanceProcAddr unavailable");
  auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
      proc_(nullptr, "vkCreateInstance"));
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "HumanVision Input";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ic.pApplicationInfo = &app;
  if (!create_instance)
    return Fail("vkCreateInstance unavailable");
  if (!Check(create_instance(&ic, nullptr, &instance_),
             "create independent Vulkan 1.1 instance"))
    return false;
#define INSTANCE(name)                                                         \
  auto name = reinterpret_cast<PFN_vk##name>(proc_(instance_, "vk" #name));    \
  if (!name)                                                                   \
  return Fail("missing vk" #name)
  INSTANCE(EnumeratePhysicalDevices);
  INSTANCE(GetPhysicalDeviceQueueFamilyProperties);
  INSTANCE(GetPhysicalDeviceProperties2);
  INSTANCE(GetPhysicalDeviceFeatures2);
  INSTANCE(GetPhysicalDeviceExternalSemaphoreProperties);
  INSTANCE(GetPhysicalDeviceImageFormatProperties2);
  INSTANCE(CreateDevice);
  INSTANCE(GetDeviceProcAddr);
  INSTANCE(EnumerateDeviceExtensionProperties);
#undef INSTANCE
  uint32_t count = 0;
  if (!Check(EnumeratePhysicalDevices(instance_, &count, nullptr),
             "enumerate Vulkan devices") ||
      !count)
    return Fail("no Vulkan physical device");
  std::vector<VkPhysicalDevice> devices(count);
  if (!Check(EnumeratePhysicalDevices(instance_, &count, devices.data()),
             "list Vulkan devices"))
    return false;
  const char *extensions[] = {
      VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
      VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
  uint64_t allocation_usage = 0;
  for (auto candidate : devices) {
    VkPhysicalDeviceIDProperties ids{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &ids};
    GetPhysicalDeviceProperties2(candidate, &props);
    auto nonzero = [](const uint8_t *id) {
      return std::any_of(id, id + 16, [](uint8_t b) { return b != 0; });
    };
    if (props.properties.apiVersion < VK_API_VERSION_1_1 ||
        !nonzero(ids.deviceUUID) || !nonzero(ids.driverUUID))
      continue;
    uint32_t n = 0;
    if (EnumerateDeviceExtensionProperties(candidate, nullptr, &n, nullptr) !=
        VK_SUCCESS)
      continue;
    std::vector<VkExtensionProperties> ext(n);
    if (EnumerateDeviceExtensionProperties(candidate, nullptr, &n,
                                           ext.data()) != VK_SUCCESS)
      continue;
    bool supported = true;
    for (auto required : extensions)
      supported &= std::any_of(ext.begin(), ext.end(), [&](const auto &e) {
        return !std::strcmp(e.extensionName, required);
      });
    if (!supported)
      continue;
    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
    VkPhysicalDeviceFeatures2 features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &ycbcr};
    GetPhysicalDeviceFeatures2(candidate, &features);
    if (!ycbcr.samplerYcbcrConversion)
      continue;
    VkPhysicalDeviceExternalSemaphoreInfo si{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
    si.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkExternalSemaphoreProperties sp{
        VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    GetPhysicalDeviceExternalSemaphoreProperties(candidate, &si, &sp);
    const auto both = VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT |
                      VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
    if ((sp.externalSemaphoreFeatures & both) != both ||
        !(sp.compatibleHandleTypes & si.handleType))
      continue;
    VkPhysicalDeviceExternalImageFormatInfo ei{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    ei.handleType = ahb_handle;
    VkPhysicalDeviceImageFormatInfo2 fi{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, &ei};
    fi.format = rgba;
    fi.type = VK_IMAGE_TYPE_2D;
    fi.tiling = VK_IMAGE_TILING_OPTIMAL;
    fi.usage = output_usage;
    VkAndroidHardwareBufferUsageANDROID usage{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID};
    VkExternalImageFormatProperties external{
        VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES, &usage};
    VkImageFormatProperties2 fp{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
                                &external};
    if (GetPhysicalDeviceImageFormatProperties2(candidate, &fi, &fp) !=
            VK_SUCCESS ||
        !(external.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
        width_ > fp.imageFormatProperties.maxExtent.width ||
        height_ > fp.imageFormatProperties.maxExtent.height)
      continue;
    GetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
    std::vector<VkQueueFamilyProperties> queues(n);
    GetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
    uint32_t family = 0;
    while (family < n && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
      ++family;
    if (family == n)
      continue;
    float priority = 0.5f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &ycbcr};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 3;
    di.ppEnabledExtensionNames = extensions;
    if (CreateDevice(candidate, &di, nullptr, &device) != VK_SUCCESS)
      continue;
    physical = candidate;
    identity.size = sizeof(identity);
    identity.version = 1;
    std::memcpy(identity.device_uuid, ids.deviceUUID, 16);
    std::memcpy(identity.driver_uuid, ids.driverUUID, 16);
    identity.api_version = props.properties.apiVersion;
    identity.vendor_id = props.properties.vendorID;
    identity.device_id = props.properties.deviceID;
    identity.queue_family = family;
    identity.ycbcr_enabled = identity.ahb_enabled = identity.foreign_enabled =
        identity.sync_fd_enabled = 1;
    allocation_usage = usage.androidHardwareBufferUsage |
                       AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    break;
  }
  if (!device)
    return Fail("no Vulkan 1.1 compute device supporting UUID, YCbCr, RGBA AHB "
                "import, FOREIGN and bidirectional sync fd");
#define LOAD(name)                                                             \
  name =                                                                       \
      reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name));   \
  if (!name)                                                                   \
    return Fail("missing vk" #name);
  NATIVE_VK_EXTRA(LOAD)
#undef LOAD
  if (!LoadFunctions(device, instance_, proc_))
    return Fail("missing native Vulkan conversion entry point");
  importer.device = device;
  importer.physical_device = physical;
  GetDeviceQueue(device, identity.queue_family, 0, &queue_);
  GetPhysicalDeviceMemoryProperties(physical, &memory_);
  VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pc.queueFamilyIndex = identity.queue_family;
  pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (!Check(CreateCommandPool(device, &pc, nullptr, &pool_),
             "create native command pool"))
    return false;
  VkCommandBufferAllocateInfo ca{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = pool_;
  ca.commandBufferCount = 1;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  if (!Check(AllocateCommandBuffers(device, &ca, &command_),
             "allocate native command buffer"))
    return false;
  VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (!Check(CreateFence(device, &fc, nullptr, &fence_),
             "create conversion fence"))
    return false;
  VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  if (!Check(CreateSemaphore(device, &sc, nullptr, &acquire_),
             "create decoder semaphore"))
    return false;
  VkExportSemaphoreCreateInfo ex{
      VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
  ex.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  sc.pNext = &ex;
  if (!Check(CreateSemaphore(device, &sc, nullptr, &ready_),
             "create external producer semaphore"))
    return false;
  if (!MakeScratch(width_, height_, scratch_, scratch_memory_, scratch_view_))
    return false;
  for (auto &slot : slots) {
    slot.usage = allocation_usage;
    if (!MakeOutput(slot, config.enable_readback != 0))
      return false;
  }
  return true;
}
bool NativeVulkanContext::MakeOutput(NativeRgbaSlot &slot, bool readback) {
  AHardwareBuffer_Desc desc{};
  desc.width = width_;
  desc.height = height_;
  desc.layers = 1;
  desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  desc.usage = slot.usage;
  if (AHardwareBuffer_allocate(&desc, &slot.ahb) != 0)
    return Fail("allocate RGBA AHardwareBuffer");
  AHardwareBuffer_describe(slot.ahb, &desc);
  if (desc.width != width_ || desc.height != height_ || desc.layers != 1 ||
      desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
      (desc.usage & slot.usage) != slot.usage)
    return Fail("allocated RGBA AHB geometry or usage mismatch");
  slot.usage = desc.usage;
  VkExternalMemoryImageCreateInfo ext{
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, nullptr, ahb_handle};
  auto image = Image(width_, height_, output_usage);
  image.pNext = &ext;
  if (!Check(CreateImage(device, &image, nullptr, &slot.image),
             "create external RGBA image"))
    return false;
  VkAndroidHardwareBufferFormatPropertiesANDROID fmt{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
  VkAndroidHardwareBufferPropertiesANDROID props{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &fmt};
  if (!Check(
          GetAndroidHardwareBufferPropertiesANDROID(device, slot.ahb, &props),
          "query output AHB"))
    return false;
  auto type = MemoryType(props.memoryTypeBits, 0);
  if (fmt.format != rgba || type == UINT32_MAX || !props.allocationSize)
    return Fail("invalid RGBA external memory contract");
  VkImportAndroidHardwareBufferInfoANDROID imp{
      VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, nullptr,
      slot.ahb};
  VkMemoryDedicatedAllocateInfo dedicated{
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, &imp, slot.image};
  VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &dedicated,
                           props.allocationSize, type};
  if (!Check(AllocateMemory(device, &mem, nullptr, &slot.memory),
             "import RGBA AHB") ||
      !Check(BindImageMemory(device, slot.image, slot.memory, 0),
             "bind RGBA AHB"))
    return false;
  return !readback || MakeStaging(slot, width_, height_);
}
bool NativeVulkanContext::MakeScratch(uint32_t width, uint32_t height,
                                      VkImage &target, VkDeviceMemory &memory,
                                      VkImageView &view) {
  auto image =
      Image(width, height,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  if (!Check(CreateImage(device, &image, nullptr, &target),
             "create RGBA conversion target"))
    return false;
  VkMemoryRequirements req{};
  GetImageMemoryRequirements(device, target, &req);
  auto type = MemoryType(req.memoryTypeBits, 0);
  if (type == UINT32_MAX)
    return Fail("no conversion target memory type");
  VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
                           req.size, type};
  if (!Check(AllocateMemory(device, &mem, nullptr, &memory),
             "allocate conversion target") ||
      !Check(BindImageMemory(device, target, memory, 0),
             "bind conversion target"))
    return false;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = target;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = rgba;
  vi.subresourceRange = color_range;
  if (!Check(CreateImageView(device, &vi, nullptr, &view),
             "create RGBA target view"))
    return false;
  return true;
}
bool NativeVulkanContext::MakeStaging(NativeRgbaSlot &slot, uint32_t width,
                                      uint32_t height) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = VkDeviceSize(width) * height * 4;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (!Check(CreateBuffer(device, &bi, nullptr, &slot.staging),
             "create readback staging"))
    return false;
  VkMemoryRequirements req{};
  GetBufferMemoryRequirements(device, slot.staging, &req);
  auto type =
      MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (type == UINT32_MAX)
    type = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  if (type == UINT32_MAX)
    return Fail("no host-visible staging memory type");
  slot.coherent = (memory_.memoryTypes[type].propertyFlags &
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
  VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
                           req.size, type};
  return Check(AllocateMemory(device, &mem, nullptr, &slot.staging_memory),
               "allocate staging memory") &&
         Check(BindBufferMemory(device, slot.staging, slot.staging_memory, 0),
               "bind staging memory") &&
         Check(MapMemory(device, slot.staging_memory, 0, VK_WHOLE_SIZE, 0,
                         &slot.mapped),
               "map staging buffer");
}
void NativeVulkanContext::DestroyStaging(NativeRgbaSlot &slot) {
  if (slot.mapped)
    UnmapMemory(device, slot.staging_memory);
  if (slot.staging)
    DestroyBuffer(device, slot.staging, nullptr);
  if (slot.staging_memory)
    FreeMemory(device, slot.staging_memory, nullptr);
  slot.mapped = nullptr;
  slot.staging = {};
  slot.staging_memory = {};
}
void NativeVulkanContext::DestroyCapture() {
  DestroyStaging(capture_.staging);
  if (capture_.view)
    DestroyImageView(device, capture_.view, nullptr);
  if (capture_.image)
    DestroyImage(device, capture_.image, nullptr);
  if (capture_.memory)
    FreeMemory(device, capture_.memory, nullptr);
  capture_ = {};
}
bool NativeVulkanContext::PrepareCapture(uint32_t width, uint32_t height) {
  if (width == capture_.width && height == capture_.height &&
      capture_.staging.mapped)
    return true;
  DestroyCapture();
  if (!MakeScratch(width, height, capture_.image, capture_.memory,
                   capture_.view) ||
      !MakeStaging(capture_.staging, width, height)) {
    DestroyCapture();
    return false;
  }
  capture_.width = width;
  capture_.height = height;
  return true;
}
NativeVulkanContext::~NativeVulkanContext() {
  // Session release is gated on true GPU retirement.
  cache.RemoveAll();
  cache.Collect();
  for (auto &slot : slots) {
    if (slot.producer_fd >= 0)
      close(slot.producer_fd);
    DestroyStaging(slot);
    if (slot.image)
      DestroyImage(device, slot.image, nullptr);
    if (slot.memory)
      FreeMemory(device, slot.memory, nullptr);
    if (slot.ahb)
      AHardwareBuffer_release(slot.ahb);
  }
  DestroyCapture();
  if (scratch_view_)
    DestroyImageView(device, scratch_view_, nullptr);
  if (scratch_)
    DestroyImage(device, scratch_, nullptr);
  if (scratch_memory_)
    FreeMemory(device, scratch_memory_, nullptr);
  if (pool_)
    DestroyCommandPool(device, pool_, nullptr);
  if (acquire_)
    DestroySemaphore(device, acquire_, nullptr);
  if (ready_)
    DestroySemaphore(device, ready_, nullptr);
  if (fence_)
    DestroyFence(device, fence_, nullptr);
  if (device && DestroyDevice)
    DestroyDevice(device, nullptr);
  if (instance_)
    reinterpret_cast<PFN_vkDestroyInstance>(
        proc_(instance_, "vkDestroyInstance"))(instance_, nullptr);
  if (library_)
    dlclose(library_);
}

bool NativeVulkanContext::ReadContract(AndroidDecodedImage &image,
                                       ImageContract &contract) {
  AHardwareBuffer_Desc desc{};
  AHardwareBuffer_describe(image.buffer, &desc);
  InputGpuCapabilities capabilities{};
  if (!QueryDecodedAhbProperties(GetAndroidHardwareBufferPropertiesANDROID,
                                 device, image.buffer, desc, &capabilities) ||
      !capabilities.external_format || !capabilities.sampled)
    return Fail(
        "unsupported PRIVATE decoder AHB external format or sampled usage");
  contract = DecodedImageContract(image, capabilities);
  return true;
}
void NativeVulkanContext::Record(AndroidDecodedImage &source, CacheEntry &entry,
                                 NativeRgbaSlot &slot, int rotation,
                                 bool mirror) {
  auto *resource = static_cast<Imported *>(entry.resource);
  const auto family = identity.queue_family;
  VkImageMemoryBarrier prepare[] = {
      Barrier(resource->image, VK_IMAGE_LAYOUT_GENERAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
              VK_ACCESS_SHADER_READ_BIT, VK_QUEUE_FAMILY_FOREIGN_EXT, family),
      Barrier(scratch_,
              scratch_used_ ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_GENERAL,
              scratch_used_ ? VK_ACCESS_TRANSFER_READ_BIT : 0,
              VK_ACCESS_SHADER_WRITE_BIT),
      Barrier(
          slot.image,
          slot.written ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          slot.written ? VK_QUEUE_FAMILY_EXTERNAL_KHR : VK_QUEUE_FAMILY_IGNORED,
          slot.written ? family : VK_QUEUE_FAMILY_IGNORED)};
  CmdPipelineBarrier(command_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                     0, 0, nullptr, 0, nullptr, 3, prepare);
  if (capturing_) {
    auto prepare_capture =
        Barrier(capture_.image,
                capture_.initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                     : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                capture_.initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
                VK_ACCESS_SHADER_WRITE_BIT);
    CmdPipelineBarrier(command_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &prepare_capture);
  }
  CmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, resource->pipeline);
  RecordInputConversion(*this, device, command_, resource,
                        resource->descriptors[0], scratch_view_, width_,
                        height_, source, entry.contract, rotation, mirror,
                        true);
  if (capturing_) {
    RecordInputConversion(*this, device, command_, resource,
                          resource->descriptors[1], capture_.view,
                          capture_.width, capture_.height, source,
                          entry.contract, rotation, mirror, true);
    auto readable =
        Barrier(capture_.image, VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    CmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &readable);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {capture_.width, capture_.height, 1};
    CmdCopyImageToBuffer(command_, capture_.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         capture_.staging.staging, 1, &region);
    VkBufferMemoryBarrier visible{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    visible.srcQueueFamilyIndex = visible.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    visible.buffer = capture_.staging.staging;
    visible.size = VK_WHOLE_SIZE;
    CmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &visible,
                       0, nullptr);
  }
  VkImageMemoryBarrier converted[] = {
      Barrier(scratch_, VK_IMAGE_LAYOUT_GENERAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
              VK_ACCESS_TRANSFER_READ_BIT),
      Barrier(resource->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, 0, family,
              VK_QUEUE_FAMILY_FOREIGN_EXT)};
  CmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                     2, converted);
  VkImageCopy copy{};
  copy.srcSubresource =
      copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.extent = {width_, height_, 1};
  CmdCopyImage(command_, scratch_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               slot.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  if (slot.staging) {
    VkBufferImageCopy region{};
    region.imageSubresource = copy.srcSubresource;
    region.imageExtent = copy.extent;
    CmdCopyImageToBuffer(command_, scratch_,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.staging, 1,
                         &region);
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    host.buffer = slot.staging;
    host.size = VK_WHOLE_SIZE;
  }
  auto release = Barrier(slot.image, VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         0, family, VK_QUEUE_FAMILY_EXTERNAL_KHR);
  CmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_HOST_BIT |
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                     0, 0, nullptr, slot.staging ? 1 : 0,
                     slot.staging ? &host : nullptr, 1, &release);
}
bool NativeVulkanContext::RunFence(NativeRgbaSlot &slot,
                                   AndroidDecodedImage &source,
                                   CacheEntry &entry) {
  const bool wait = source.acquire_fd >= 0;
  if (wait) {
    int duplicate = fcntl(source.acquire_fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0)
      return Fail("duplicate decoded acquire sync fd");
    VkImportSemaphoreFdInfoKHR info{
        VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
    info.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
    info.semaphore = acquire_;
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    info.fd = duplicate;
    if (!Check(ImportSemaphoreFdKHR(device, &info),
               "import decoded acquire sync fd")) {
      close(duplicate);
      return false;
    }
  }
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = wait ? 1 : 0;
  submit.pWaitSemaphores = &acquire_;
  submit.pWaitDstStageMask = &stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &ready_;
  VkResult submitted = QueueSubmit(queue_, 1, &submit, fence_);
  if (!Check(submitted, "submit native conversion")) {
    unfenced = submitted == VK_ERROR_DEVICE_LOST;
    return false;
  }
  unfenced = true;
  cache.Used(entry, completed + 1);
  VkSemaphoreGetFdInfoKHR proof{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
  proof.semaphore = ready_;
  proof.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  if (slot.producer_fd >= 0)
    close(slot.producer_fd);
  slot.producer_fd = -1;
  int exported_fd = -1;
  const VkResult exported = GetSemaphoreFdKHR(device, &proof, &exported_fd);
  if (exported == VK_SUCCESS)
    slot.producer_fd = exported_fd;
  // Only the dedicated conversion worker waits. Decoder, caller and Unity never
  // wait on this fence.
  auto proof_state = CompleteNativeReadback(
      !slot.staging || slot.coherent,
      [&] {
        return Check(WaitForFences(device, 1, &fence_, VK_TRUE, UINT64_MAX),
                     "wait actual conversion fence");
      },
      [&] {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = slot.staging_memory;
        range.size = VK_WHOLE_SIZE;
        return Check(InvalidateMappedMemoryRanges(device, 1, &range),
                     "invalidate readback after copy fence");
      });
  if (proof_state == NativeReadbackProof::WaitFailed)
    return false;
  ++completed;
  unfenced = false;
  scratch_used_ = true;
  slot.written = true;
  source.gpu_submitted = true;
  if (proof_state != NativeReadbackProof::Visible)
    return false;
  if (capturing_) {
    capture_.initialized = true;
    capture_visible_ = capture_.staging.coherent;
    if (!capture_visible_) {
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = capture_.staging.staging_memory;
      range.size = VK_WHOLE_SIZE;
      capture_visible_ =
          InvalidateMappedMemoryRanges(device, 1, &range) == VK_SUCCESS;
    }
  }
  if (!Check(exported, "export producer completion fd"))
    return false;
  return true;
}
bool NativeVulkanContext::Convert(AndroidDecodedImage &source, int index,
                                  int rotation, bool mirror, bool capture) {
  capture_visible_ = false;
  capturing_ = false;
  if (capture) {
    uint32_t width = source.crop_right - source.crop_left,
             height = source.crop_bottom - source.crop_top;
    if (rotation == 90 || rotation == 270)
      std::swap(width, height);
    capturing_ = PrepareCapture(width, height);
  }
  BufferIdentity removed{};
  while (registry.TakeRemoved(removed))
    cache.Remove(removed.buffer);
  cache.Collect();
  ImageContract contract{};
  if (!ReadContract(source, contract))
    return false;
  importer.source_reader = source.reader;
  auto *entry = cache.Acquire(source.buffer, source.generation, contract);
  if (!entry) {
    if (error.empty())
      error = CacheFailureName(cache.LastFailure().reason);
    return false;
  }
  if (!Check(ResetCommandBuffer(command_, 0), "reset conversion command") ||
      !Check(ResetFences(device, 1, &fence_), "reset conversion fence"))
    return false;
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!Check(BeginCommandBuffer(command_, &bi), "begin conversion command"))
    return false;
  Record(source, *entry, slots[index], rotation, mirror);
  return Check(EndCommandBuffer(command_), "end conversion command") &&
         RunFence(slots[index], source, *entry);
}
} // namespace hvinput
