#pragma once

#include "humanvision_plugin_v2.h"
#include "humanvision_plugin_v3.h"
#include "json/json.hpp"
#include <array>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include "gpu/vulkan/vulkan_device_identity.h"
namespace humanvision::gpu { class UnityVulkanBridge; }
#endif

namespace humanvision::runtime::ncnn_backend {
struct InputContract {
    uint32_t image_format = 0;
    uint32_t channel_order = 0;
    uint32_t output_type = 0;
    int output_elempack = 0;
    int cast_type_to = 0;
    int width = 0, height = 0;
    std::array<float, 3> mean{}, norm{};
    std::string input_blob;
    std::vector<std::string> output_blobs;
};
bool ParseInputContract(const nlohmann::json& value, InputContract& result, std::string& error);
#if defined(__ANDROID__)
// Internal control-thread context behind HV_GpuDeviceContextV1::host_context.
// The public V2 C ABI remains opaque and unchanged.
struct HostContext {
    uint32_t struct_size = sizeof(HostContext);
    uint32_t api_version = HV_GPU_FRAME_API_V1;
    gpu::VulkanDeviceContext unity_device{};
    gpu::UnityVulkanBridge* bridge = nullptr;
};
#endif
}
extern "C" HV_Result HV_CALL HV_QueryNcnnVulkanPluginV2(
    uint32_t requested_api_version, HV_PluginApiV2* out_api);
extern "C" HV_Result HV_CALL HV_QueryNcnnVulkanPluginV3(
    uint32_t requested_api_version, HV_PluginApiV3* out_api);
