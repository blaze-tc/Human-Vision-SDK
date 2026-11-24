#include "android/input_conversion_api.h"
// Defensive host tests execute the production loader with synthetic lookups.
// No Vulkan driver is called; these do not reproduce RK3588 startup or reset.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#endif

using hvinput::InputConversionApi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
namespace {
int instance_calls{}, device_calls{}, physical_device_lookups{};
bool missing_device_loader{};
const char* missing_function = nullptr;
void VKAPI_CALL DeviceFunction() {}
void VKAPI_CALL InstanceFunction() {}
PFN_vkVoidFunction VKAPI_CALL DeviceProc(VkDevice, const char* name) {
  ++device_calls;
  if (!std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") ||
      !std::strcmp(name, "vkGetPhysicalDeviceFormatProperties"))
    ++physical_device_lookups;
  if (missing_function && !std::strcmp(name, missing_function)) return nullptr;
  return &DeviceFunction;
}
PFN_vkVoidFunction VKAPI_CALL InstanceProc(VkInstance, const char* name) {
  ++instance_calls;
  if (!std::strcmp(name, "vkGetDeviceProcAddr"))
    return missing_device_loader ? nullptr
        : reinterpret_cast<PFN_vkVoidFunction>(&DeviceProc);
  if (missing_function && !std::strcmp(name, missing_function)) return nullptr;
  return &InstanceFunction;
}
VkDevice Device() { return reinterpret_cast<VkDevice>(uintptr_t{1}); }
VkInstance Instance() { return reinterpret_cast<VkInstance>(uintptr_t{2}); }
struct Context {
  VkInstance instance = Instance();
  VkPhysicalDevice physicalDevice = reinterpret_cast<VkPhysicalDevice>(uintptr_t{3});
  VkDevice device = Device();
  VkQueue graphicsQueue = reinterpret_cast<VkQueue>(uintptr_t{4});
  PFN_vkGetInstanceProcAddr getInstanceProcAddr = &InstanceProc;
};
void Run(const std::string& name) {
  InputConversionApi api;
  if (name == "NullDevice") {
    CHECK(!api.LoadFunctions(VK_NULL_HANDLE, Instance(), &InstanceProc));
    CHECK(instance_calls == 0 && device_calls == 0);
  } else if (name == "NullInstance") {
    CHECK(!api.LoadFunctions(Device(), VK_NULL_HANDLE, &InstanceProc));
    CHECK(instance_calls == 0 && device_calls == 0);
  } else if (name == "NullInstanceProc") {
    CHECK(!api.LoadFunctions(Device(), Instance(), nullptr));
  } else if (name == "MissingDeviceLoader") {
    missing_device_loader = true;
    CHECK(!api.LoadFunctions(Device(), Instance(), &InstanceProc));
    CHECK(instance_calls == 1 && device_calls == 0);
  } else if (name == "MissingRequiredFunction") {
    missing_function = "vkCreateFence";
    CHECK(!api.LoadFunctions(Device(), Instance(), &InstanceProc));
    CHECK(api.CreateFence == nullptr);
  } else if (name == "ValidDispatchDomains") {
    CHECK(api.LoadFunctions(Device(), Instance(), &InstanceProc));
    CHECK(device_calls > 0 && instance_calls > 0);
    CHECK(physical_device_lookups == 0);
    CHECK(reinterpret_cast<PFN_vkVoidFunction>(api.CreateImage) == &DeviceFunction);
    CHECK(reinterpret_cast<PFN_vkVoidFunction>(api.GetPhysicalDeviceMemoryProperties) == &InstanceFunction);
    CHECK(reinterpret_cast<PFN_vkVoidFunction>(api.GetPhysicalDeviceFormatProperties) == &InstanceFunction);
  } else if (name == "CompleteContext") {
    Context context;
    CHECK(hvinput::InputVulkanContextError(context) == nullptr);
    CHECK(instance_calls == 0 && device_calls == 0);
  } else if (name == "IncompleteContext") {
    Context context;
    context.instance = VK_NULL_HANDLE;
    CHECK(std::strstr(hvinput::InputVulkanContextError(context), "instance"));
    context = {}; context.physicalDevice = VK_NULL_HANDLE;
    CHECK(std::strstr(hvinput::InputVulkanContextError(context), "physical device"));
    context = {}; context.device = VK_NULL_HANDLE;
    CHECK(std::strstr(hvinput::InputVulkanContextError(context), "logical device"));
    context = {}; context.graphicsQueue = VK_NULL_HANDLE;
    CHECK(std::strstr(hvinput::InputVulkanContextError(context), "graphics queue"));
    context = {}; context.getInstanceProcAddr = nullptr;
    CHECK(std::strstr(hvinput::InputVulkanContextError(context), "vkGetInstanceProcAddr"));
    CHECK(instance_calls == 0 && device_calls == 0);
  } else throw std::runtime_error("unknown case");
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
  try {
    CHECK(argc == 2);
    Run(argv[1]);
    std::puts("PASS");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL %s\n", e.what());
    return 1;
  }
}
