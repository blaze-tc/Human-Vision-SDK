#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__ANDROID__)
struct VkInstance_T;
struct VkPhysicalDevice_T;
struct VkDevice_T;
namespace ncnn { class Net; }
#endif

namespace humanvision::gpu {
struct DeviceIdentity {
    bool queried = false;
    std::array<uint8_t, 16> device_uuid{}, driver_uuid{}, pipeline_cache_uuid{};
    uint32_t vendor_id = 0, device_id = 0, driver_version = 0;
    std::string name;
};
struct IndexedDevice { int index = -1; DeviceIdentity identity; };
enum class DeviceMatchStatus { InvalidIdentity, NoMatch, MultipleMatches, Matched };
struct DeviceMatch {
    DeviceMatchStatus status = DeviceMatchStatus::InvalidIdentity;
    int index = -1;
    DeviceIdentity identity;
    std::string diagnostic;
};
std::string DescribeDevice(const DeviceIdentity& identity);
DeviceMatch MatchDevice(const DeviceIdentity& unity, const std::vector<IndexedDevice>& candidates);
#if defined(__ANDROID__)
// B4 supplies enabled instance/device capabilities from device creation, not
// merely advertised extensions. Handles are borrowed for this synchronous probe.
struct VulkanDeviceContext {
    VkInstance_T* instance = nullptr;
    VkPhysicalDevice_T* physical_device = nullptr;
    VkDevice_T* device = nullptr;
    uint32_t instance_api_version = 0;
    bool properties2_extension = false;
    bool external_memory_capabilities_extension = false;
    bool ahb_extension = false;
};
DeviceIdentity QueryDeviceIdentity(const VulkanDeviceContext& device);
// The application owns ncnn's global GPU instance lifetime. Initialize it before
// these calls and keep it alive until all nets/imports are destroyed.
DeviceMatch MatchNcnnDevice(const VulkanDeviceContext& unity);
DeviceMatch ConfigureNcnnNet(const VulkanDeviceContext& unity, ncnn::Net& net);
#endif
}
