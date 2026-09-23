#define __ANDROID__ 1
#include "gpu/android/ahb_capabilities.cpp"
#include <gtest/gtest.h>

using namespace humanvision::gpu;
namespace {
VkFormatFeatureFlags concrete_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
    VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
VkFormatFeatureFlags external_features = concrete_features;
uint64_t optimal_usage_extra = 0;
int fail_stage = 0;
int live_resources = 0;
int identity_queries = 0;
AHardwareBuffer_Desc allocated_description{};
AHardwareBuffer allocated_buffer{};

VkResult VKAPI_CALL BufferProperties(VkDevice, const AHardwareBuffer*, VkAndroidHardwareBufferPropertiesANDROID* properties) {
    properties->allocationSize = 4096;
    properties->memoryTypeBits = 1;
    auto* format = static_cast<VkAndroidHardwareBufferFormatPropertiesANDROID*>(properties->pNext);
    format->format = VK_FORMAT_R8G8B8A8_UNORM;
    format->externalFormat = 42;
    format->formatFeatures = external_features;
    return VK_SUCCESS;
}

VkResult VKAPI_CALL ImageProperties(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2* info,
                                    VkImageFormatProperties2* properties) {
    auto* external = static_cast<VkExternalImageFormatProperties*>(properties->pNext);
    external->externalMemoryProperties.externalMemoryFeatures =
        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;
    external->externalMemoryProperties.compatibleHandleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    auto* usage = static_cast<VkAndroidHardwareBufferUsageANDROID*>(external->pNext);
    usage->androidHardwareBufferUsage = 256 |
        ((info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ? 512 : 0) | optimal_usage_extra;
    properties->imageFormatProperties.maxExtent = {4096, 4096, 1};
    properties->imageFormatProperties.maxArrayLayers = 1;
    properties->imageFormatProperties.maxMipLevels = 1;
    properties->imageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    return VK_SUCCESS;
}

void Reset() {
    concrete_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    external_features = concrete_features;
    optimal_usage_extra = 0;
    fail_stage = 0;
    identity_queries = 0;
    ASSERT_EQ(live_resources, 0);
}

VulkanDeviceContext Context() {
    VulkanDeviceContext context;
    context.instance = reinterpret_cast<VkInstance>(1);
    context.physical_device = reinterpret_cast<VkPhysicalDevice>(2);
    context.device = reinterpret_cast<VkDevice>(3);
    context.instance_api_version = VK_API_VERSION_1_1;
    context.ahb_extension = true;
    return context;
}

AhbImageFacts Probe(uint64_t actual_usage, VkImageUsageFlags usage) {
    AHardwareBuffer buffer;
    AhbDescription description{640, 480, 1, 1, actual_usage, 640};
    std::ostringstream diagnostic;
    return ProbeImport(Context(), &buffer, description, usage, diagnostic);
}
}

extern "C" {
PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*) {
    return reinterpret_cast<PFN_vkVoidFunction>(BufferProperties);
}
PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*) {
    return reinterpret_cast<PFN_vkVoidFunction>(ImageProperties);
}
void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
    properties->linearTilingFeatures = properties->optimalTilingFeatures = concrete_features;
}
void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties) {
    properties->memoryTypeCount = 1;
}
VkResult VKAPI_CALL vkCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* image) {
    if (fail_stage == 1) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *image = reinterpret_cast<VkImage>(1); ++live_resources; return VK_SUCCESS;
}
VkResult VKAPI_CALL vkAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* memory) {
    if (fail_stage == 2) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *memory = reinterpret_cast<VkDeviceMemory>(2); ++live_resources; return VK_SUCCESS;
}
VkResult VKAPI_CALL vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) {
    return fail_stage == 3 ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
VkResult VKAPI_CALL vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* view) {
    if (fail_stage == 4) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *view = reinterpret_cast<VkImageView>(3); ++live_resources; return VK_SUCCESS;
}
VkResult VKAPI_CALL vkCreateRenderPass(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass* render_pass) {
    if (fail_stage == 5) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *render_pass = reinterpret_cast<VkRenderPass>(4); ++live_resources; return VK_SUCCESS;
}
VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer* framebuffer) {
    if (fail_stage == 6) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *framebuffer = reinterpret_cast<VkFramebuffer>(5); ++live_resources; return VK_SUCCESS;
}
void VKAPI_CALL vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) { --live_resources; }
void VKAPI_CALL vkFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) { --live_resources; }
void VKAPI_CALL vkDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) { --live_resources; }
void VKAPI_CALL vkDestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*) { --live_resources; }
void VKAPI_CALL vkDestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*) { --live_resources; }
}

int AHardwareBuffer_allocate(const AHardwareBuffer_Desc* description, AHardwareBuffer** buffer) {
    allocated_description = *description;
    allocated_description.stride = description->width;
    *buffer = &allocated_buffer;
    return 0;
}
void AHardwareBuffer_describe(const AHardwareBuffer*, AHardwareBuffer_Desc* description) {
    *description = allocated_description;
}
void AHardwareBuffer_release(AHardwareBuffer*) {}
namespace humanvision::gpu {
DeviceIdentity QueryDeviceIdentity(const VulkanDeviceContext& context) {
    ++identity_queries;
    DeviceIdentity identity;
    identity.queried = true;
    identity.device_uuid.fill(11);
    identity.driver_uuid.fill(12);
    identity.pipeline_cache_uuid.fill(13);
    identity.vendor_id = 14;
    identity.device_id = 15;
    identity.driver_version = 16;
    identity.name = context.device == reinterpret_cast<VkDevice>(3) ? "producer" : "consumer";
    return identity;
}
DeviceMatch MatchNcnnDevice(const VulkanDeviceContext&) {
    DeviceMatch match;
    match.status = DeviceMatchStatus::Matched;
    match.index = 7;
    return match;
}
DeviceMatch MatchDevice(const DeviceIdentity&, const std::vector<IndexedDevice>& candidates) {
    DeviceMatch match;
    match.status = DeviceMatchStatus::Matched;
    match.index = candidates.front().index;
    match.identity = candidates.front().identity;
    return match;
}
}

TEST(AhbAndroidProbe, SelectionRetainsMeasuredSourceAndBothDeviceQueryResults) {
    for (const auto path : {HV_ANDROID_GPU_COPY_BLIT, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT}) {
        Reset();
        if (path == HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)
            concrete_features &= ~VK_FORMAT_FEATURE_BLIT_DST_BIT;
        auto consumer = Context();
        consumer.device = reinterpret_cast<VkDevice>(4);
        const VulkanSourceImage source{1280, 720, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_TILING_LINEAR,
            VK_SAMPLE_COUNT_1_BIT, 1, VK_IMAGE_TYPE_2D};
        const auto result = ProbeAndroidAhbCapabilities(Context(), consumer, source, 640, 480);
        ASSERT_EQ(result.path, path);
        EXPECT_EQ(result.source.width, 1280u);
        EXPECT_EQ(result.source.height, 720u);
        EXPECT_EQ(result.source.format, VK_FORMAT_B8G8R8A8_UNORM);
        EXPECT_EQ(result.source.usage, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        EXPECT_EQ(result.source.tiling, VK_IMAGE_TILING_LINEAR);
        EXPECT_EQ(result.source.samples, VK_SAMPLE_COUNT_1_BIT);
        EXPECT_EQ(result.source.layers, 1u);
        EXPECT_EQ(result.source.image_type, VK_IMAGE_TYPE_2D);
        EXPECT_EQ(result.producer_identity.name, "producer");
        EXPECT_EQ(result.consumer_identity.name, "consumer");
        for (const auto* identity : {&result.producer_identity, &result.consumer_identity}) {
            EXPECT_TRUE(identity->queried);
            for (const auto byte : identity->device_uuid) EXPECT_EQ(byte, 11);
            for (const auto byte : identity->driver_uuid) EXPECT_EQ(byte, 12);
            for (const auto byte : identity->pipeline_cache_uuid) EXPECT_EQ(byte, 13);
            EXPECT_EQ(identity->vendor_id, 14u);
            EXPECT_EQ(identity->device_id, 15u);
            EXPECT_EQ(identity->driver_version, 16u);
        }
        EXPECT_EQ(identity_queries, 2);
        EXPECT_EQ(live_resources, 0);

        const auto generic = SelectAhbCopyPath(result.candidates);
        EXPECT_EQ(generic.path, path);
        EXPECT_EQ(generic.source.width, 0u);
        EXPECT_FALSE(generic.producer_identity.queried);
        EXPECT_FALSE(generic.consumer_identity.queried);
    }
}

TEST(AhbAndroidProbe, ConcreteFormatFeaturesAreIndependentFromExternalFormatFeatures) {
    Reset();
    external_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;
    const auto facts = Probe(768, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    EXPECT_TRUE(facts.color_attachment);
    EXPECT_TRUE(facts.image_created);
    EXPECT_EQ(facts.format_features, external_features);
    EXPECT_EQ(live_resources, 0);
}

TEST(AhbAndroidProbe, OptionalVendorUsageIsDiagnosticButStandardUsageIsMandatory) {
    Reset();
    optimal_usage_extra = 1ull << 48;
    auto facts = Probe(768, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    EXPECT_TRUE(facts.usage_compatible);
    EXPECT_TRUE(facts.image_created);
    EXPECT_EQ(facts.optimal_ahb_usage, 768u | optimal_usage_extra);
    EXPECT_EQ(facts.required_standard_ahb_usage, 768u);
    facts = Probe(512, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    EXPECT_FALSE(facts.usage_compatible);
    EXPECT_FALSE(facts.image_created);
    EXPECT_EQ(live_resources, 0);
}

TEST(AhbAndroidProbe, ConsumerViewFailureRejectsSelectionWithNamedReason) {
    Reset();
    AhbCandidate candidate;
    candidate.path = HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT;
    candidate.requested = candidate.actual = {640, 480, 1, 1, 768, 640};
    candidate.allocated = candidate.described = candidate.source_supported = candidate.source_sampled = true;
    candidate.producer = Probe(768, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    fail_stage = 4;
    candidate.consumer = Probe(768, VK_IMAGE_USAGE_SAMPLED_BIT);
    const auto result = SelectAhbCopyPath({candidate});
    EXPECT_EQ(result.path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
    EXPECT_NE(result.diagnostic.find("consumer.view_created"), std::string::npos);
    EXPECT_EQ(live_resources, 0);
}

TEST(AhbAndroidProbe, EveryCreationFailureReleasesAllEarlierResources) {
    Reset();
    for (fail_stage = 1; fail_stage <= 6; ++fail_stage) {
        Probe(768, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        EXPECT_EQ(live_resources, 0) << "failure stage " << fail_stage;
    }
    fail_stage = 0;
}
