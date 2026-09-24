#pragma once

namespace humanvision::runtime::ncnn_backend {
// The fourth lane is explicit zero padding for a three-channel RGB model.
int NormalizedChannelCount(int output_elempack) noexcept;
bool NormalizeRgbPixel(const float rgb[3], const float mean[3],
                       const float norm[3], int channels, float output[4]) noexcept;
}

#if defined(__ANDROID__)
#include "humanvision_plugin_v2.h"
#include <command.h>
#include <gpu.h>
#include <mat.h>
#include <option.h>
#include <pipeline.h>
#include <memory>
#include <string>
#include <vector>

namespace humanvision::runtime::ncnn_backend {

// A single persistent shader handles planar RGB crop, bilinear resize,
// optional channel swap and per-channel normalization. Input and output VkMats
// are owned by the slot cache; this class only records GPU work.
class GpuPreprocess {
public:
    bool Initialize(const ncnn::VulkanDevice*, const ncnn::Option&, std::string& error);
    bool Record(const ncnn::VkMat& rgb, const HV_GpuImageTransformV1& transform,
                ncnn::VkMat& normalized, ncnn::VkCompute& compute,
                std::string& error);
private:
    std::unique_ptr<ncnn::Pipeline> pipeline_;
    std::vector<ncnn::VkMat> bindings_;
    std::vector<ncnn::vk_constant_type> constants_;
};
}
#endif
