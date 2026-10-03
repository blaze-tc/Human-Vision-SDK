#pragma once
#include <cstring>
namespace hvinput {
template<class DeviceLookup,class InstanceLookup>
auto LoadInputVulkanProc(const char* name,DeviceLookup device,InstanceLookup instance) {
 // Physical-device commands belong to the instance dispatch domain. Some
 // drivers return a non-null device lookup anyway; that is not a valid loader.
 if(!std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties")||!std::strcmp(name,"vkGetPhysicalDeviceFormatProperties"))return instance(name);
 auto function=device(name);
 return function?function:instance(name);
}
}
