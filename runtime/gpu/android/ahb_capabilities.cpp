#include "ahb_capabilities.h"
#include <sstream>
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#endif

namespace humanvision::gpu {
namespace {
// Android/Vulkan constants kept private so host tests do not need platform SDKs.
constexpr uint64_t sampled_usage = 256, color_usage = 512;
constexpr uint32_t rgba8 = 1, vk_rgba8 = 37, sampled_image = 4;
bool Evaluate(const AhbCandidate& c, std::ostringstream& out) {
    bool valid = true;
    const auto require = [&](bool pass, const std::string& name) {
        if (!pass) { valid = false; out << "\nfailed: " << name; }
    };
    const bool blit = c.path == HV_ANDROID_GPU_COPY_BLIT;
    out << "\ncandidate=" << (blit ? "blit" : "color_attachment")
        << " width=" << c.actual.width << " height=" << c.actual.height
        << " layers=" << c.actual.layers << " format=" << c.actual.format
        << " usage=" << c.actual.usage << " stride=" << c.actual.stride;
    require(blit || c.path == HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT, "candidate.path");
    require(c.allocated, "AHardwareBuffer_allocate");
    require(c.described, "AHardwareBuffer_describe");
    require(c.requested.width > 0 && c.actual.width == c.requested.width, "actual.width");
    require(c.requested.height > 0 && c.actual.height == c.requested.height, "actual.height");
    require(c.actual.layers == 1 && c.requested.layers == 1, "actual.layers");
    require(c.actual.format == rgba8 && c.requested.format == rgba8, "actual.format");
    require(c.actual.usage == c.requested.usage &&
            c.requested.usage == (blit ? sampled_usage : sampled_usage | color_usage), "actual.usage");
    require(c.actual.stride >= c.actual.width && c.actual.stride > 0, "actual.stride");
    require(c.source_supported, "source.single_sample_rgba_or_bgra_2d");
    const auto image = [&](const AhbImageFacts& facts, const std::string& prefix) {
        out << "\n" << prefix << " vk_format=" << facts.vk_format << " external_format=" << facts.external_format
            << " external_features=" << facts.format_features
            << " concrete_features=" << facts.concrete_format_features
            << " image_usage=" << facts.image_usage
            << " optimal_ahb_usage=" << facts.optimal_ahb_usage
            << " required_standard_ahb_usage=" << facts.required_standard_ahb_usage;
        require(facts.properties, prefix + ".properties");
        require(facts.vk_format == vk_rgba8, prefix + ".concrete_rgba8_format");
        require(facts.external_query, prefix + ".external_query");
        require(facts.importable, prefix + ".importable");
        require(facts.compatible_handle, prefix + ".compatible_handle");
        require(facts.usage_compatible, prefix + ".usage_compatible");
        require(facts.extent_supported, prefix + ".extent_supported");
        require(facts.sampled, prefix + ".sampled");
        require(facts.image_created, prefix + ".image_created");
        require(facts.memory_imported, prefix + ".memory_imported");
        require(facts.memory_bound, prefix + ".memory_bound");
    };
    image(c.producer, "producer");
    image(c.consumer, "consumer");
    require(c.consumer.image_usage == sampled_image, "consumer.sampled_read_only_usage");
    require(c.consumer.view_created, "consumer.view_created");
    require(c.producer.image_usage == (blit ? 6u : 20u), "producer.image_usage");
    if (blit) {
        require(c.source_transfer_src, "source.transfer_src");
        require(c.source_blit_src, "source.blit_src");
        require(c.producer.transfer_dst, "producer.transfer_dst");
        require(c.producer.blit_dst, "producer.blit_dst");
        require(!c.requires_scale_or_conversion || c.blit_conversion, "formats.blit_conversion");
    } else {
        require(c.source_sampled, "source.sampled");
        require(c.producer.color_attachment, "producer.color_attachment");
        require(c.producer.view_created, "producer.view_created");
        require(c.producer.framebuffer_created, "producer.framebuffer_created");
    }
    out << '\n' << c.detail;
    return valid;
}
}
AhbSelection SelectAhbCopyPath(const std::vector<AhbCandidate>& candidates) {
    AhbSelection result;
    result.candidates = candidates;
    std::ostringstream out;
    for (const auto path : {HV_ANDROID_GPU_COPY_BLIT, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT}) {
        for (const auto& candidate : candidates) {
            if (candidate.path != path) continue;
            const bool supported = Evaluate(candidate, out);
            if (supported && result.path == HV_ANDROID_GPU_COPY_UNAVAILABLE) {
                result.path = path;
                result.contract = candidate.actual;
            }
        }
    }
    if (result.path == HV_ANDROID_GPU_COPY_UNAVAILABLE) out << "\nHV_ANDROID_GPU_COPY_UNAVAILABLE";
    result.diagnostic = out.str();
    return result;
}
AhbSelection ProbeAhbContracts(uint32_t width, uint32_t height, const AhbProbe& probe) {
    if (width == 0 || height == 0 || !probe) return SelectAhbCopyPath({});
    std::vector<AhbCandidate> candidates;
    for (const auto path : {HV_ANDROID_GPU_COPY_BLIT, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT}) {
        const AhbDescription request{width, height, 1, rgba8,
            path == HV_ANDROID_GPU_COPY_BLIT ? sampled_usage : sampled_usage | color_usage, 0};
        auto candidate = probe(request, path);
        // The orchestrator owns request identity; a backend cannot reinterpret a
        // sampled-only probe as a color-output allocation after measuring it.
        candidate.requested = request;
        candidate.path = path;
        candidates.push_back(std::move(candidate));
        auto result = SelectAhbCopyPath(candidates);
        if (result.path != HV_ANDROID_GPU_COPY_UNAVAILABLE) return result;
    }
    return SelectAhbCopyPath(candidates);
}

#if defined(__ANDROID__)
namespace {
// Probe objects are never submitted. Destruction needs no GPU wait and precedes
// the AHB allocation-reference release. B3 owns persistent generation resources.
struct ProbeImage {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    explicit ProbeImage(VkDevice d) : device(d) {}
    ProbeImage(const ProbeImage&) = delete;
    ProbeImage& operator=(const ProbeImage&) = delete;
    ~ProbeImage() {
        if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
    }
};
struct ProbeBuffer {
    AHardwareBuffer* value = nullptr;
    ~ProbeBuffer() { if (value) AHardwareBuffer_release(value); }
};
bool Record(VkResult result, const char* operation, std::ostringstream& out) {
    out << ' ' << operation << '=' << result;
    return result == VK_SUCCESS;
}

AhbImageFacts ProbeImport(const VulkanDeviceContext& context, AHardwareBuffer* buffer,
    const AhbDescription& desc, VkImageUsageFlags usage, std::ostringstream& out) {
    AhbImageFacts facts;
    facts.image_usage = usage;
    const bool core = context.instance_api_version >= VK_API_VERSION_1_1;
    if (!context.instance || !context.physical_device || !context.device || !context.ahb_extension ||
        (!core && !(context.properties2_extension && context.external_memory_capabilities_extension))) {
        out << " enabled AHB/properties2/external-memory capabilities or device handles missing";
        return facts;
    }
    const auto get_buffer_properties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(context.device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    const auto get_image_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
        vkGetInstanceProcAddr(context.instance, core ? "vkGetPhysicalDeviceImageFormatProperties2" : "vkGetPhysicalDeviceImageFormatProperties2KHR"));
    if (!get_buffer_properties || !get_image_properties) {
        out << " missing Vulkan AHB/image-format query entrypoints";
        return facts;
    }
    VkAndroidHardwareBufferFormatPropertiesANDROID format{};
    format.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferPropertiesANDROID properties{};
    properties.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    properties.pNext = &format;
    facts.properties = Record(get_buffer_properties(context.device, buffer, &properties),
                              "vkGetAndroidHardwareBufferPropertiesANDROID", out);
    facts.vk_format = format.format;
    facts.external_format = format.externalFormat;
    facts.format_features = format.formatFeatures;
    out << " allocationSize=" << properties.allocationSize << " memoryTypeBits=" << properties.memoryTypeBits;
    // This generation contract is RGBA8. External-only formats require a
    // separately approved conversion contract and cannot be color attachments.
    if (!facts.properties || format.format != VK_FORMAT_R8G8B8A8_UNORM) return facts;
    VkFormatProperties device_format{};
    vkGetPhysicalDeviceFormatProperties(context.physical_device, format.format, &device_format);
    // format.formatFeatures describes the external-format image named by
    // externalFormat. This generation creates a concrete RGBA image, so its
    // capabilities come only from the concrete-format/tiling query. The two
    // masks are deliberately retained separately in diagnostics.
    const auto features = device_format.optimalTilingFeatures;
    facts.concrete_format_features = features;
    out << " optimalTilingFeatures=" << device_format.optimalTilingFeatures;
    facts.sampled = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    facts.transfer_dst = (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
    facts.blit_dst = (features & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
    facts.color_attachment = (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;

    VkPhysicalDeviceExternalImageFormatInfo external{};
    external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkPhysicalDeviceImageFormatInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.pNext = &external;
    info.format = format.format;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    VkAndroidHardwareBufferUsageANDROID ahb_usage{};
    ahb_usage.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID;
    VkExternalImageFormatProperties external_properties{};
    external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    external_properties.pNext = &ahb_usage;
    VkImageFormatProperties2 image_properties{};
    image_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    image_properties.pNext = &external_properties;
    facts.external_query = Record(get_image_properties(context.physical_device, &info, &image_properties),
                                 "vkGetPhysicalDeviceImageFormatProperties2", out);
    const auto& memory = external_properties.externalMemoryProperties;
    facts.importable = (memory.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
    facts.compatible_handle = (memory.compatibleHandleTypes & external.handleType) != 0;
    facts.optimal_ahb_usage = ahb_usage.androidHardwareBufferUsage;
    facts.required_standard_ahb_usage =
        ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? sampled_usage : 0) |
        ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ? color_usage : 0);
    // androidHardwareBufferUsage is optimal allocation guidance and can contain
    // vendor-specific bits. The externally allocated contract is required to
    // carry the standard usage equivalents only; exact Vulkan image support is
    // still enforced by the image-format query and create/import/bind sequence.
    facts.usage_compatible =
        (desc.usage & facts.required_standard_ahb_usage) == facts.required_standard_ahb_usage;
    const auto& limits = image_properties.imageFormatProperties;
    facts.extent_supported = desc.width <= limits.maxExtent.width && desc.height <= limits.maxExtent.height &&
        limits.maxExtent.depth >= 1 && limits.maxArrayLayers >= desc.layers && limits.maxMipLevels >= 1 &&
        (limits.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0;
    out << " externalMemoryFeatures=" << memory.externalMemoryFeatures
        << " compatibleHandleTypes=" << memory.compatibleHandleTypes
        << " maxExtent=" << limits.maxExtent.width << 'x' << limits.maxExtent.height
        << " maxResourceSize=" << limits.maxResourceSize;
    if (!facts.external_query || !facts.importable || !facts.compatible_handle || !facts.usage_compatible ||
        !facts.extent_supported || !facts.sampled ||
        ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) && !facts.transfer_dst) ||
        ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) && !facts.color_attachment)) return facts;

    ProbeImage resources(context.device);
    VkExternalMemoryImageCreateInfo external_create{};
    external_create.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_create.handleTypes = external.handleType;
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.pNext = &external_create;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = format.format;
    create.extent = {desc.width, desc.height, 1};
    create.mipLevels = 1;
    create.arrayLayers = desc.layers;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    facts.image_created = Record(vkCreateImage(context.device, &create, nullptr, &resources.image), "vkCreateImage", out);
    if (!facts.image_created) return facts;
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(context.physical_device, &memory_properties);
    uint32_t memory_type = 0;
    while (memory_type < memory_properties.memoryTypeCount && !(properties.memoryTypeBits & (1u << memory_type))) ++memory_type;
    if (memory_type == memory_properties.memoryTypeCount || properties.allocationSize == 0) {
        out << " no AHB memory type/allocation size";
        return facts;
    }
    VkImportAndroidHardwareBufferInfoANDROID import{};
    import.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import.buffer = buffer;
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.pNext = &import;
    dedicated.image = resources.image;
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.pNext = &dedicated;
    allocate.allocationSize = properties.allocationSize;
    allocate.memoryTypeIndex = memory_type;
    facts.memory_imported = Record(vkAllocateMemory(context.device, &allocate, nullptr, &resources.memory), "vkAllocateMemory", out);
    if (!facts.memory_imported) return facts;
    facts.memory_bound = Record(vkBindImageMemory(context.device, resources.image, resources.memory, 0), "vkBindImageMemory", out);
    if (!facts.memory_bound) return facts;
    // Consumer probe always stays sampled-only; no transfer, render target,
    // submission, or transition is performed on the ncnn logical device.
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = resources.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    facts.view_created = Record(vkCreateImageView(context.device, &view, nullptr, &resources.view), "vkCreateImageView", out);
    if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) || !facts.view_created) return facts;
    VkAttachmentDescription attachment{};
    attachment.format = format.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkRenderPassCreateInfo render_pass{};
    render_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass.attachmentCount = 1;
    render_pass.pAttachments = &attachment;
    render_pass.subpassCount = 1;
    render_pass.pSubpasses = &subpass;
    if (!Record(vkCreateRenderPass(context.device, &render_pass, nullptr, &resources.render_pass), "vkCreateRenderPass", out)) return facts;
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = resources.render_pass;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &resources.view;
    framebuffer.width = desc.width;
    framebuffer.height = desc.height;
    framebuffer.layers = 1;
    facts.framebuffer_created = Record(vkCreateFramebuffer(context.device, &framebuffer, nullptr, &resources.framebuffer), "vkCreateFramebuffer", out);
    return facts;
}
}

AhbSelection ProbeAndroidAhbCapabilities(const VulkanDeviceContext& unity,
    const VulkanDeviceContext& consumer, const VulkanSourceImage& source, uint32_t width, uint32_t height) {
    const auto match = MatchNcnnDevice(unity);
    const auto producer_identity = QueryDeviceIdentity(unity);
    const auto consumer_identity = QueryDeviceIdentity(consumer);
    const auto consumer_match = MatchDevice(producer_identity, {{match.index, consumer_identity}});
    const auto bind_measurement = [&](AhbSelection& result) {
        result.source = source;
        result.producer_identity = producer_identity;
        result.consumer_identity = consumer_identity;
        result.diagnostic += "\n" + match.diagnostic + "\nconsumer " + consumer_match.diagnostic;
    };
    if (match.status != DeviceMatchStatus::Matched || consumer_match.status != DeviceMatchStatus::Matched) {
        auto result = SelectAhbCopyPath({});
        bind_measurement(result);
        return result;
    }
    auto result = ProbeAhbContracts(width, height, [&](const AhbDescription& request, HV_AndroidGpuCopyPath path) {
        AhbCandidate c;
        c.path = path;
        c.requested = request;
        c.source_supported = source.width > 0 && source.height > 0 && source.samples == VK_SAMPLE_COUNT_1_BIT &&
            source.layers == 1 && source.image_type == VK_IMAGE_TYPE_2D &&
            (source.tiling == VK_IMAGE_TILING_OPTIMAL || source.tiling == VK_IMAGE_TILING_LINEAR) &&
            (source.format == VK_FORMAT_R8G8B8A8_UNORM || source.format == VK_FORMAT_B8G8R8A8_UNORM);
        VkFormatProperties source_properties{};
        if (c.source_supported) vkGetPhysicalDeviceFormatProperties(unity.physical_device,
            static_cast<VkFormat>(source.format), &source_properties);
        const auto source_features = source.tiling == VK_IMAGE_TILING_LINEAR ?
            source_properties.linearTilingFeatures : source_properties.optimalTilingFeatures;
        c.source_transfer_src = (source.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
            (source_features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
        c.source_blit_src = (source_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
        c.source_sampled = (source.usage & VK_IMAGE_USAGE_SAMPLED_BIT) && (source_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
        c.requires_scale_or_conversion = source.width != width || source.height != height || source.format != VK_FORMAT_R8G8B8A8_UNORM;
        // The admitted formats are compatible four-component UNORM float
        // formats; scaling/conversion uses a linear blit in B4.
        c.blit_conversion = c.source_supported && (source_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        std::ostringstream detail;
        detail << "source format=" << source.format << " usage=" << source.usage << " features=" << source_features;
        AHardwareBuffer_Desc allocate{};
        allocate.width = request.width;
        allocate.height = request.height;
        allocate.layers = request.layers;
        allocate.format = request.format;
        allocate.usage = request.usage;
        ProbeBuffer buffer;
        const int allocation_result = AHardwareBuffer_allocate(&allocate, &buffer.value);
        detail << " AHardwareBuffer_allocate=" << allocation_result;
        c.allocated = allocation_result == 0 && buffer.value;
        if (c.allocated) {
            AHardwareBuffer_Desc actual{};
            AHardwareBuffer_describe(buffer.value, &actual);
            c.described = true;
            c.actual = {actual.width, actual.height, actual.layers, actual.format, actual.usage, actual.stride};
            // Never issue import/create operations with an incompatible actual
            // descriptor, even if the requested descriptor was correct.
            if (actual.width == request.width && actual.height == request.height && actual.layers == 1 &&
                actual.format == request.format && actual.usage == request.usage && actual.stride >= actual.width) {
                detail << "\nproducer";
                c.producer = ProbeImport(unity, buffer.value, c.actual,
                    VK_IMAGE_USAGE_SAMPLED_BIT | (path == HV_ANDROID_GPU_COPY_BLIT ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT), detail);
                detail << "\nconsumer";
                c.consumer = ProbeImport(consumer, buffer.value, c.actual, VK_IMAGE_USAGE_SAMPLED_BIT, detail);
            }
        }
        c.detail = detail.str();
        return c;
    });
    bind_measurement(result);
    return result;
}
#endif
}
