#include "input_vulkan_proc_loader.h"
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
using namespace hvinput;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main(){try{
 for(const char* name:{"vkGetPhysicalDeviceMemoryProperties","vkGetPhysicalDeviceFormatProperties"}){
  int device_calls=0,instance_calls=0;
  auto value=LoadInputVulkanProc(name,[&](const char*){++device_calls;return 42;},[&](const char*){++instance_calls;return 17;});
  CHECK(value==17);CHECK(device_calls==0);CHECK(instance_calls==1);
  value=LoadInputVulkanProc(name,[&](const char*){++device_calls;return 42;},[&](const char*){++instance_calls;return 0;});CHECK(value==0);CHECK(device_calls==0);
 }
 int device_calls=0,instance_calls=0;
 auto value=LoadInputVulkanProc("vkCreateImage",[&](const char*){++device_calls;return 42;},[&](const char*){++instance_calls;return 17;});CHECK(value==42);CHECK(device_calls==1);CHECK(instance_calls==0);
 value=LoadInputVulkanProc("vkCreateImage",[&](const char*){++device_calls;return 0;},[&](const char*){++instance_calls;return 17;});CHECK(value==17);CHECK(device_calls==2);CHECK(instance_calls==1);
 std::puts("PASS");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
