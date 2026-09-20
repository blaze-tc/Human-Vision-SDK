#pragma once
#include <cstddef>
#include <cstdint>

#define VKAPI_ATTR
#define VKAPI_CALL
#define VKAPI_PTR
#define VK_NULL_HANDLE nullptr
#define VK_API_VERSION_1_1 0x00401000u

using VkFlags = uint32_t;
using VkBool32 = uint32_t;
using VkDeviceSize = uint64_t;
using VkFormat = uint32_t;
using VkFormatFeatureFlags = uint32_t;
using VkImageUsageFlags = uint32_t;
using VkExternalMemoryFeatureFlags = uint32_t;
using VkExternalMemoryHandleTypeFlags = uint32_t;
using VkSampleCountFlags = uint32_t;
using VkStructureType = uint32_t;
using VkResult = int32_t;
using VkImageType = uint32_t;
using VkImageTiling = uint32_t;
using VkImageLayout = uint32_t;
using VkSharingMode = uint32_t;
using VkImageViewType = uint32_t;
using VkImageAspectFlags = uint32_t;
using VkSampleCountFlagBits = uint32_t;
using VkAttachmentLoadOp = uint32_t;
using VkAttachmentStoreOp = uint32_t;
using VkPipelineBindPoint = uint32_t;

struct VkInstance_T {}; struct VkPhysicalDevice_T {}; struct VkDevice_T {};
struct VkImage_T {}; struct VkDeviceMemory_T {}; struct VkImageView_T {};
struct VkRenderPass_T {}; struct VkFramebuffer_T {};
using VkInstance = VkInstance_T*; using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkDevice = VkDevice_T*; using VkImage = VkImage_T*;
using VkDeviceMemory = VkDeviceMemory_T*; using VkImageView = VkImageView_T*;
using VkRenderPass = VkRenderPass_T*; using VkFramebuffer = VkFramebuffer_T*;
using PFN_vkVoidFunction = void(*)();

constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_ERROR_OUT_OF_DEVICE_MEMORY = -2;
constexpr VkFormat VK_FORMAT_R8G8B8A8_UNORM = 37;
constexpr VkFormat VK_FORMAT_B8G8R8A8_UNORM = 44;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT = 0x1;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_BLIT_SRC_BIT = 0x400;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_BLIT_DST_BIT = 0x800;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT = 0x1000;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_TRANSFER_SRC_BIT = 0x4000;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_TRANSFER_DST_BIT = 0x8000;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT = 0x20000;
constexpr VkFormatFeatureFlags VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT = 0x80;
constexpr VkImageUsageFlags VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x1;
constexpr VkImageUsageFlags VK_IMAGE_USAGE_TRANSFER_DST_BIT = 0x2;
constexpr VkImageUsageFlags VK_IMAGE_USAGE_SAMPLED_BIT = 0x4;
constexpr VkImageUsageFlags VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x10;
constexpr VkExternalMemoryFeatureFlags VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT = 0x1;
constexpr VkExternalMemoryFeatureFlags VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT = 0x4;
constexpr VkExternalMemoryHandleTypeFlags VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID = 0x400;
constexpr VkImageType VK_IMAGE_TYPE_2D = 1;
constexpr VkImageTiling VK_IMAGE_TILING_OPTIMAL = 0;
constexpr VkImageTiling VK_IMAGE_TILING_LINEAR = 1;
constexpr VkSampleCountFlagBits VK_SAMPLE_COUNT_1_BIT = 1;
constexpr VkSharingMode VK_SHARING_MODE_EXCLUSIVE = 0;
constexpr VkImageLayout VK_IMAGE_LAYOUT_UNDEFINED = 0;
constexpr VkImageLayout VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2;
constexpr VkImageViewType VK_IMAGE_VIEW_TYPE_2D = 1;
constexpr VkImageAspectFlags VK_IMAGE_ASPECT_COLOR_BIT = 1;
constexpr VkAttachmentLoadOp VK_ATTACHMENT_LOAD_OP_DONT_CARE = 2;
constexpr VkAttachmentStoreOp VK_ATTACHMENT_STORE_OP_STORE = 0;
constexpr VkAttachmentStoreOp VK_ATTACHMENT_STORE_OP_DONT_CARE = 1;
constexpr VkPipelineBindPoint VK_PIPELINE_BIND_POINT_GRAPHICS = 0;

constexpr VkStructureType VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID = 1000129002;
constexpr VkStructureType VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID = 1000129001;
constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO = 1000071000;
constexpr VkStructureType VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 = 1000059004;
constexpr VkStructureType VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID = 1000129000;
constexpr VkStructureType VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES = 1000071001;
constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 = 1000059003;
constexpr VkStructureType VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO = 1000072000;
constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14;
constexpr VkStructureType VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID = 1000129003;
constexpr VkStructureType VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO = 1000127001;
constexpr VkStructureType VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5;
constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15;
constexpr VkStructureType VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO = 38;
constexpr VkStructureType VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO = 37;

struct VkAllocationCallbacks {};
struct VkExtent3D { uint32_t width, height, depth; };
struct VkFormatProperties { VkFormatFeatureFlags linearTilingFeatures, optimalTilingFeatures, bufferFeatures; };
struct VkAndroidHardwareBufferFormatPropertiesANDROID {
    VkStructureType sType; void* pNext; VkFormat format; uint64_t externalFormat;
    VkFormatFeatureFlags formatFeatures; uint64_t samplerYcbcrConversionComponents[2];
    uint32_t suggestedYcbcrModel, suggestedYcbcrRange, suggestedXChromaOffset, suggestedYChromaOffset;
};
struct VkAndroidHardwareBufferPropertiesANDROID {
    VkStructureType sType; void* pNext; VkDeviceSize allocationSize; uint32_t memoryTypeBits;
};
struct VkPhysicalDeviceExternalImageFormatInfo {
    VkStructureType sType; const void* pNext; VkExternalMemoryHandleTypeFlags handleType;
};
struct VkPhysicalDeviceImageFormatInfo2 {
    VkStructureType sType; const void* pNext; VkFormat format; VkImageType type;
    VkImageTiling tiling; VkImageUsageFlags usage; VkFlags flags;
};
struct VkExternalMemoryProperties {
    VkExternalMemoryFeatureFlags externalMemoryFeatures;
    VkExternalMemoryHandleTypeFlags exportFromImportedHandleTypes, compatibleHandleTypes;
};
struct VkAndroidHardwareBufferUsageANDROID { VkStructureType sType; void* pNext; uint64_t androidHardwareBufferUsage; };
struct VkExternalImageFormatProperties { VkStructureType sType; void* pNext; VkExternalMemoryProperties externalMemoryProperties; };
struct VkImageFormatProperties {
    VkExtent3D maxExtent; uint32_t maxMipLevels, maxArrayLayers; VkSampleCountFlags sampleCounts; VkDeviceSize maxResourceSize;
};
struct VkImageFormatProperties2 { VkStructureType sType; void* pNext; VkImageFormatProperties imageFormatProperties; };
struct VkExternalMemoryImageCreateInfo { VkStructureType sType; const void* pNext; VkExternalMemoryHandleTypeFlags handleTypes; };
struct VkImageCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags; VkImageType imageType; VkFormat format;
    VkExtent3D extent; uint32_t mipLevels, arrayLayers; VkSampleCountFlagBits samples; VkImageTiling tiling;
    VkImageUsageFlags usage; VkSharingMode sharingMode; uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices; VkImageLayout initialLayout;
};
struct VkImportAndroidHardwareBufferInfoANDROID { VkStructureType sType; const void* pNext; struct AHardwareBuffer* buffer; };
struct VkMemoryDedicatedAllocateInfo { VkStructureType sType; const void* pNext; VkImage image; void* buffer; };
struct VkMemoryAllocateInfo { VkStructureType sType; const void* pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; };
struct VkMemoryType { VkFlags propertyFlags; uint32_t heapIndex; };
struct VkPhysicalDeviceMemoryProperties { uint32_t memoryTypeCount; VkMemoryType memoryTypes[32]; };
struct VkImageSubresourceRange { VkImageAspectFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount; };
struct VkComponentMapping { uint32_t r, g, b, a; };
struct VkImageViewCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags; VkImage image; VkImageViewType viewType;
    VkFormat format; VkComponentMapping components; VkImageSubresourceRange subresourceRange;
};
struct VkAttachmentDescription {
    VkFlags flags; VkFormat format; VkSampleCountFlagBits samples; VkAttachmentLoadOp loadOp;
    VkAttachmentStoreOp storeOp; VkAttachmentLoadOp stencilLoadOp; VkAttachmentStoreOp stencilStoreOp;
    VkImageLayout initialLayout, finalLayout;
};
struct VkAttachmentReference { uint32_t attachment; VkImageLayout layout; };
struct VkSubpassDescription {
    VkFlags flags; VkPipelineBindPoint pipelineBindPoint; uint32_t inputAttachmentCount; const VkAttachmentReference* pInputAttachments;
    uint32_t colorAttachmentCount; const VkAttachmentReference* pColorAttachments; const VkAttachmentReference* pResolveAttachments;
    const VkAttachmentReference* pDepthStencilAttachment; uint32_t preserveAttachmentCount; const uint32_t* pPreserveAttachments;
};
struct VkRenderPassCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags; uint32_t attachmentCount; const VkAttachmentDescription* pAttachments;
    uint32_t subpassCount; const VkSubpassDescription* pSubpasses; uint32_t dependencyCount; const void* pDependencies;
};
struct VkFramebufferCreateInfo {
    VkStructureType sType; const void* pNext; VkFlags flags; VkRenderPass renderPass; uint32_t attachmentCount;
    const VkImageView* pAttachments; uint32_t width, height, layers;
};

using PFN_vkGetAndroidHardwareBufferPropertiesANDROID = VkResult(VKAPI_PTR*)(VkDevice, const AHardwareBuffer*, VkAndroidHardwareBufferPropertiesANDROID*);
using PFN_vkGetPhysicalDeviceImageFormatProperties2 = VkResult(VKAPI_PTR*)(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2*, VkImageFormatProperties2*);

extern "C" {
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties*);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*);
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*);
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*);
}
