#include "vulkan_device_identity.h"
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <sstream>
#if defined(__ANDROID__)
#include <gpu.h>
#include <net.h>
#endif

namespace humanvision::gpu {
namespace {
bool Valid(const DeviceIdentity& d) {
    const auto nonzero = [](const auto& uuid) {
        return std::any_of(uuid.begin(), uuid.end(), [](uint8_t b) { return b != 0; });
    };
    return d.queried && nonzero(d.device_uuid) && nonzero(d.driver_uuid);
}
std::string Hex(const std::array<uint8_t, 16>& bytes) {
    std::ostringstream out;
    for (const auto b : bytes) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
    return out.str();
}
}
std::string DescribeDevice(const DeviceIdentity& d) {
    std::ostringstream out;
    out << "deviceUUID=" << Hex(d.device_uuid) << " driverUUID=" << Hex(d.driver_uuid)
        << " vendorID=" << d.vendor_id << " deviceID=" << d.device_id
        << " driverVersion=" << d.driver_version << " name=" << d.name
        << " pipelineCacheUUID=" << Hex(d.pipeline_cache_uuid) << " queried=" << d.queried;
    return out.str();
}
DeviceMatch MatchDevice(const DeviceIdentity& unity, const std::vector<IndexedDevice>& candidates) {
    DeviceMatch result;
    std::ostringstream out;
    out << "Unity " << DescribeDevice(unity);
    bool valid = Valid(unity);
    int matches = 0;
    IndexedDevice matched;
    for (const auto& candidate : candidates) {
        out << "\nncnn index=" << candidate.index << ' ' << DescribeDevice(candidate.identity);
        if (candidate.index < 0 || !Valid(candidate.identity)) valid = false;
        if (Valid(candidate.identity) && candidate.identity.device_uuid == unity.device_uuid &&
            candidate.identity.driver_uuid == unity.driver_uuid) {
            ++matches;
            matched = candidate;
        }
    }
    if (!valid) out << "\nInvalidIdentity: require queried, nonzero deviceUUID and driverUUID on every device";
    else if (matches == 0) {
        result.status = DeviceMatchStatus::NoMatch;
        out << "\nNoMatch: no ncnn device has Unity's exact UUID pair";
    } else if (matches != 1) {
        result.status = DeviceMatchStatus::MultipleMatches;
        out << "\nMultipleMatches: ambiguous ncnn UUID pair";
    } else {
        result.status = DeviceMatchStatus::Matched;
        result.index = matched.index;
        result.identity = matched.identity;
        out << "\nMatched index=" << result.index;
    }
    result.diagnostic = out.str();
    return result;
}

#if defined(__ANDROID__)
namespace {
std::mutex gpu_instance_mutex;
uint32_t gpu_instance_users = 0;
bool gpu_instance_owned = false;
}
bool AcquireNcnnGpuInstance() noexcept {
    std::lock_guard<std::mutex> lock(gpu_instance_mutex);
    if (!gpu_instance_users && !ncnn::get_gpu_instance()) {
        if (ncnn::create_gpu_instance() != 0) return false;
        gpu_instance_owned = true;
    }
    ++gpu_instance_users;
    return true;
}
void ReleaseNcnnGpuInstance() noexcept {
    std::lock_guard<std::mutex> lock(gpu_instance_mutex);
    if (!gpu_instance_users) return;
    if (--gpu_instance_users == 0 && gpu_instance_owned) {
        ncnn::destroy_gpu_instance();
        gpu_instance_owned = false;
    }
}
DeviceIdentity QueryDeviceIdentity(const VulkanDeviceContext& context) {
    DeviceIdentity result;
    if (!context.instance || !context.physical_device || !ncnn::vkGetInstanceProcAddr) return result;
    const bool core = context.instance_api_version >= VK_MAKE_VERSION(1, 1, 0);
    if (!core && !(context.properties2_extension && context.external_memory_capabilities_extension)) return result;
    const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
        ncnn::vkGetInstanceProcAddr(context.instance, core ? "vkGetPhysicalDeviceProperties2" : "vkGetPhysicalDeviceProperties2KHR"));
    if (!query) return result;
    VkPhysicalDeviceIDPropertiesKHR ids{};
    ids.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2KHR props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
    props.pNext = &ids;
    query(context.physical_device, &props);
    std::copy_n(ids.deviceUUID, 16, result.device_uuid.begin());
    std::copy_n(ids.driverUUID, 16, result.driver_uuid.begin());
    std::copy_n(props.properties.pipelineCacheUUID, 16, result.pipeline_cache_uuid.begin());
    result.vendor_id = props.properties.vendorID;
    result.device_id = props.properties.deviceID;
    result.driver_version = props.properties.driverVersion;
    result.name = props.properties.deviceName;
    result.queried = true;
    return result;
}

DeviceMatch MatchNcnnDevice(const VulkanDeviceContext& unity) {
    // Never use a default-valued ncnn overload. GpuInfo queries physical devices
    // without constructing a Net or selecting an implicit VulkanDevice.
    const auto unity_identity = QueryDeviceIdentity(unity);
    std::vector<IndexedDevice> candidates;
    if (!ncnn::get_gpu_instance()) return MatchDevice(unity_identity, candidates);
    uint32_t api = VK_MAKE_VERSION(1, 0, 0);
    using EnumerateVersion = VkResult(VKAPI_PTR*)(uint32_t*);
    const auto enumerate_version = reinterpret_cast<EnumerateVersion>(
        ncnn::vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    // Pinned ncnn requests this same loader version when creating its instance.
    if (enumerate_version && enumerate_version(&api) != VK_SUCCESS) api = 0;
    for (int index = 0; index < ncnn::get_gpu_count(); ++index) {
        VulkanDeviceContext context;
        context.instance = ncnn::get_gpu_instance();
        context.physical_device = ncnn::get_gpu_info(index).physicalDevice();
        context.instance_api_version = api;
        context.properties2_extension = ncnn::support_VK_KHR_get_physical_device_properties2 != 0;
        context.external_memory_capabilities_extension = ncnn::support_VK_KHR_external_memory_capabilities != 0;
        candidates.push_back({index, QueryDeviceIdentity(context)});
    }
    return MatchDevice(unity_identity, candidates);
}
DeviceMatch ConfigureNcnnNet(const VulkanDeviceContext& unity, ncnn::Net& net) {
    auto result = MatchNcnnDevice(unity);
    if (result.status == DeviceMatchStatus::Matched) net.set_vulkan_device(result.index);
    return result;
}
bool FindMatchedNcnnContext(const VulkanDeviceContext& unity,
                            VulkanDeviceContext& consumer,
                            std::string& diagnostic) {
    consumer = {};
    if (!AcquireNcnnGpuInstance()) {
        diagnostic = "ncnn Vulkan GPU instance creation failed";
        return false;
    }
    const DeviceMatch match = MatchNcnnDevice(unity);
    diagnostic = match.diagnostic;
    if (match.status != DeviceMatchStatus::Matched) { ReleaseNcnnGpuInstance(); return false; }
    const ncnn::VulkanDevice* device = ncnn::get_gpu_device(match.index);
    if (!device || !device->is_valid()) {
        diagnostic = "Matched ncnn VulkanDevice is invalid: " + diagnostic;
        ReleaseNcnnGpuInstance(); return false;
    }
    consumer.instance = ncnn::get_gpu_instance();
    consumer.physical_device = ncnn::get_gpu_info(match.index).physicalDevice();
    consumer.device = device->vkdevice();
    uint32_t api = VK_MAKE_VERSION(1, 0, 0);
    using EnumerateVersion = VkResult(VKAPI_PTR*)(uint32_t*);
    const auto enumerate_version = reinterpret_cast<EnumerateVersion>(
        ncnn::vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerate_version && enumerate_version(&api) != VK_SUCCESS) {
        diagnostic = "ncnn Vulkan instance-version query failed";
        ReleaseNcnnGpuInstance(); return false;
    }
    consumer.instance_api_version = api;
    consumer.properties2_extension = ncnn::support_VK_KHR_get_physical_device_properties2 != 0;
    consumer.external_memory_capabilities_extension = ncnn::support_VK_KHR_external_memory_capabilities != 0;
    consumer.ahb_extension = true;
    return true;
}
#endif
}
