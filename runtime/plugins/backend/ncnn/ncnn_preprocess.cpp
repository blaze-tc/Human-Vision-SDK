#include "plugins/backend/ncnn/ncnn_preprocess.h"

#if defined(__ANDROID__)
#include <cmath>

namespace humanvision::runtime::ncnn_backend {
namespace {
constexpr char kShader[] = R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(binding = 0) readonly buffer Source { float source_data[]; };
layout(binding = 1) writeonly buffer Target { float target_data[]; };
layout(push_constant) uniform parameter {
    int source_width;
    int source_height;
    int source_cstep;
    int target_width;
    int target_height;
    int target_cstep;
    float rect_x;
    float rect_y;
    float rect_width;
    float rect_height;
    float mean_r;
    float mean_g;
    float mean_b;
    float norm_r;
    float norm_g;
    float norm_b;
    int channel_order;
} p;

float pixel(int channel, int x, int y) {
    x = clamp(x, 0, p.source_width - 1);
    y = clamp(y, 0, p.source_height - 1);
    return source_data[channel * p.source_cstep + y * p.source_width + x];
}

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    int output_channel = int(gl_GlobalInvocationID.z);
    if (x >= p.target_width || y >= p.target_height || output_channel >= 3) return;
    int source_channel = p.channel_order == 2 ? 2 - output_channel : output_channel;
    float fx = p.rect_x + ((float(x) + 0.5) * p.rect_width / float(p.target_width)) - 0.5;
    float fy = p.rect_y + ((float(y) + 0.5) * p.rect_height / float(p.target_height)) - 0.5;
    int x0 = int(floor(fx));
    int y0 = int(floor(fy));
    float wx = fx - float(x0);
    float wy = fy - float(y0);
    float a = mix(pixel(source_channel, x0, y0), pixel(source_channel, x0 + 1, y0), wx);
    float b = mix(pixel(source_channel, x0, y0 + 1), pixel(source_channel, x0 + 1, y0 + 1), wx);
    float mean_value = output_channel == 0 ? p.mean_r : output_channel == 1 ? p.mean_g : p.mean_b;
    float norm_value = output_channel == 0 ? p.norm_r : output_channel == 1 ? p.norm_g : p.norm_b;
    target_data[output_channel * p.target_cstep + y * p.target_width + x] =
        (mix(a, b, wy) - mean_value) * norm_value;
}
)glsl";
}

bool GpuPreprocess::Initialize(const ncnn::VulkanDevice* device,
                               const ncnn::Option& option, std::string& error) {
    if (!device || !device->is_valid()) { error = "ncnn Vulkan device is invalid"; return false; }
    std::vector<uint32_t> spirv;
    if (ncnn::compile_spirv_module(kShader, static_cast<int>(sizeof(kShader) - 1), option, spirv) != 0 ||
        spirv.empty()) { error = "ncnn GPU preprocessing shader compilation failed"; return false; }
    auto pipeline = std::make_unique<ncnn::Pipeline>(device);
    pipeline->set_local_size_xyz(8, 8, 1);
    if (pipeline->create(spirv.data(), spirv.size() * sizeof(uint32_t), {}) != 0 ||
        pipeline->shader_info().binding_count != 2 ||
        pipeline->shader_info().push_constant_count != 17) {
        error = "ncnn GPU preprocessing pipeline contract is invalid"; return false;
    }
    pipeline_ = std::move(pipeline);
    bindings_.resize(2);
    constants_.resize(17);
    error.clear();
    return true;
}

bool GpuPreprocess::Record(const ncnn::VkMat& rgb, const HV_GpuImageTransformV1& transform,
                           ncnn::VkMat& normalized, ncnn::VkCompute& compute,
                           std::string& error) {
    const auto& r = transform.source_rect_px;
    if (!pipeline_ || rgb.empty() || normalized.empty() || rgb.dims != 3 || rgb.c != 3 ||
        rgb.elempack != 1 || rgb.elemsize != sizeof(float) || normalized.dims != 3 ||
        normalized.c != 3 || normalized.elempack != 1 || normalized.elemsize != sizeof(float) ||
        normalized.w != transform.output_width || normalized.h != transform.output_height ||
        transform.channel_order != 1 && transform.channel_order != 2 ||
        !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height) ||
        r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
        r.x + r.width > rgb.w || r.y + r.height > rgb.h) {
        error = "GPU preprocessing source rectangle or tensor geometry is invalid"; return false;
    }
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(transform.mean[i]) || !std::isfinite(transform.norm[i])) {
            error = "GPU preprocessing normalization contains a non-finite value"; return false;
        }
    constants_[0].i = rgb.w; constants_[1].i = rgb.h;
    constants_[2].i = static_cast<int>(rgb.cstep);
    constants_[3].i = normalized.w; constants_[4].i = normalized.h;
    constants_[5].i = static_cast<int>(normalized.cstep);
    constants_[6].f = r.x; constants_[7].f = r.y;
    constants_[8].f = r.width; constants_[9].f = r.height;
    for (int i = 0; i < 3; ++i) {
        constants_[10 + i].f = transform.mean[i];
        constants_[13 + i].f = transform.norm[i];
    }
    constants_[16].i = static_cast<int>(transform.channel_order);
    bindings_[0] = rgb;
    bindings_[1] = normalized;
    compute.record_pipeline(pipeline_.get(), bindings_, constants_, normalized);
    error.clear();
    return true;
}
}
#endif
