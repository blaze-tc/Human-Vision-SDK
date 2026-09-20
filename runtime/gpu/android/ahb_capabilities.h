#pragma once
#include "humanvision/humanvision_android_gpu.h"
#include "gpu/vulkan/vulkan_device_identity.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace humanvision::gpu {
struct AhbDescription {
    uint32_t width = 0, height = 0, layers = 0, format = 0;
    uint64_t usage = 0;
    uint32_t stride = 0;
};
struct AhbImageFacts {
    bool properties = false, external_query = false, importable = false;
    bool compatible_handle = false, usage_compatible = false, extent_supported = false;
    bool sampled = false, transfer_dst = false, blit_dst = false, color_attachment = false;
    bool image_created = false, memory_imported = false, memory_bound = false;
    bool view_created = false, framebuffer_created = false;
    uint32_t vk_format = 0, image_usage = 0;
    // format_features belongs to external_format. concrete_format_features
    // belongs to vk_format at the image's selected tiling. They are independent.
    uint64_t external_format = 0, format_features = 0, concrete_format_features = 0;
    // The Vulkan query returns optimal allocation guidance which may include
    // vendor bits. Only required_standard_ahb_usage is an admission gate.
    uint64_t optimal_ahb_usage = 0, required_standard_ahb_usage = 0;
};
struct AhbCandidate {
    HV_AndroidGpuCopyPath path = HV_ANDROID_GPU_COPY_UNAVAILABLE;
    AhbDescription requested, actual;
    bool allocated = false, described = false, source_supported = false;
    bool source_transfer_src = false, source_blit_src = false, source_sampled = false;
    bool requires_scale_or_conversion = false, blit_conversion = false;
    AhbImageFacts producer, consumer;
    std::string detail;
};
struct AhbSelection {
    HV_AndroidGpuCopyPath path = HV_ANDROID_GPU_COPY_UNAVAILABLE;
    AhbDescription contract;
    std::vector<AhbCandidate> candidates;
    std::string diagnostic;
};
AhbSelection SelectAhbCopyPath(const std::vector<AhbCandidate>& candidates);
// The probe callback owns and releases its temporary AHB/import resources before
// returning; the second allocation must never reuse the first candidate's facts.
using AhbProbe = std::function<AhbCandidate(const AhbDescription&, HV_AndroidGpuCopyPath)>;
AhbSelection ProbeAhbContracts(uint32_t width, uint32_t height, const AhbProbe& probe);
#if defined(__ANDROID__)
// Filled from the observed UnityVulkanImage; usage/tiling cannot be inferred from
// the pixel format. No Unity header or object enters the runtime boundary.
struct VulkanSourceImage {
    uint32_t width = 0, height = 0, format = 0, usage = 0, tiling = 0;
    uint32_t samples = 0, layers = 0, image_type = 0;
};
// Initialization/control-thread only. Each candidate owns a temporary allocation
// and both temporary imports, destroyed before the next candidate is allocated.
// The returned description is the contract B3 must re-describe for every slot.
AhbSelection ProbeAndroidAhbCapabilities(const VulkanDeviceContext& unity,
    const VulkanDeviceContext& consumer, const VulkanSourceImage& source,
    uint32_t width, uint32_t height);
#endif
}
