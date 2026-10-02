#include "android_input_gpu.h"
#include "input_ycbcr_declarations.h"
#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include "input_vulkan_private.h"
#include <android/log.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
namespace hvinput {
static IUnityGraphicsVulkan* unity_vulkan = nullptr;
static IUnityGraphics* graphics=nullptr;
static void UNITY_INTERFACE_API GraphicsEvent(UnityGfxDeviceEventType event){if(event==kUnityGfxDeviceEventInitialize&&graphics&&graphics->GetRenderer()==kUnityGfxRendererVulkan&&InputForeignEnabled())ConfigureInputRenderEvent();}
static PFN_vkGetInstanceProcAddr loader = nullptr;
static PFN_vkCreateDevice create_device = nullptr;
static PFN_vkCreateInstance create_instance = nullptr;
static uint32_t instance_api = VK_API_VERSION_1_0;
static VkInstance captured_instance=VK_NULL_HANDLE;
static std::mutex device_mutex;
static VkDevice proven_device = VK_NULL_HANDLE;
static VkPhysicalDevice proven_physical = VK_NULL_HANDLE;
static bool enabled_ycbcr = false, enabled_sync = false, enabled_ahb = false;
static bool enabled_foreign=false;
IUnityGraphicsVulkan* InputUnityVulkan(){return unity_vulkan;}
bool InputForeignEnabled(){std::lock_guard<std::mutex> lock(device_mutex);return proven_device&&unity_vulkan&&proven_device==unity_vulkan->Instance().device&&enabled_foreign;}
static bool HasExtension(const VkDeviceCreateInfo* p, const char* name) {
  for (uint32_t i=0; i<p->enabledExtensionCount; ++i)
    if (!std::strcmp(p->ppEnabledExtensionNames[i], name)) return true;
  return false;
}
static VKAPI_ATTR VkResult VKAPI_CALL CaptureCreateInstance(const VkInstanceCreateInfo* info,
    const VkAllocationCallbacks* allocation,VkInstance* instance) {
  const auto result=create_instance(info,allocation,instance);
  if(result==VK_SUCCESS) {
    captured_instance=*instance;
    instance_api=info->pApplicationInfo && info->pApplicationInfo->apiVersion ? info->pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
    __android_log_print(ANDROID_LOG_INFO,"HVInputGate","instance_creation success instance=%p requested_api=%u",*instance,instance_api);
  }
  return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL CaptureCreateDevice(VkPhysicalDevice physical,
    const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocation, VkDevice* device) {
  try {
  const auto declarations=ValidateYcbcrDeclarations(info->pNext);
  if(!declarations.Valid()) {
    __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL invalid_ycbcr_declarations=%d",static_cast<int>(declarations.failure));
  }
  return CreateIfYcbcrValid(declarations,[&]() -> VkResult {
  bool ycbcr=declarations.enabled;
  const bool ycbcr_declared=declarations.declared;
  // Loader implementations need the live instance for physical-device entrypoints.
  // The interception captures it before Unity requests vkCreateDevice.
  auto get_properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(loader(captured_instance,"vkGetPhysicalDeviceProperties"));
  auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(loader(captured_instance,"vkEnumerateDeviceExtensionProperties"));
  auto get_features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(loader(captured_instance,"vkGetPhysicalDeviceFeatures2"));
  if(!get_features) get_features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(loader(captured_instance,"vkGetPhysicalDeviceFeatures2KHR"));
  if(!get_properties || !enumerate || !get_features) return VK_ERROR_FEATURE_NOT_PRESENT;
  VkPhysicalDeviceProperties properties{};get_properties(physical,&properties);
  const bool core11=instance_api>=VK_API_VERSION_1_1 && properties.apiVersion>=VK_API_VERSION_1_1;
  VkPhysicalDeviceSamplerYcbcrConversionFeatures supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&supported};
  get_features(physical,&features);
  uint32_t count=0;
  if(enumerate(physical,nullptr,&count,nullptr)!=VK_SUCCESS) return VK_ERROR_EXTENSION_NOT_PRESENT;
  std::vector<VkExtensionProperties> available(count);
  if(enumerate(physical,nullptr,&count,available.data())!=VK_SUCCESS) return VK_ERROR_EXTENSION_NOT_PRESENT;
  std::vector<const char*> requested;
  for(uint32_t i=0;i<info->enabledExtensionCount;++i) requested.push_back(info->ppEnabledExtensionNames[i]);
  const auto require=[&](const char* name) {
    for(auto* existing:requested) if(!std::strcmp(existing,name)) return true;
    for(const auto& extension:available) if(!std::strcmp(extension.extensionName,name)) {requested.push_back(name);return true;}
    __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL missing_physical_extension=%s",name);return false;
  };
  bool complete=require(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) && require(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) && require(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
  if(!core11) {
    for(auto* dependency:{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,VK_KHR_MAINTENANCE1_EXTENSION_NAME}) complete=require(dependency) && complete;
  }
  if(!complete || !supported.samplerYcbcrConversion || (ycbcr_declared && !ycbcr)) {
    __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL initialization_prerequisite physical_ycbcr=%d existing_disabled_ycbcr=%d extensions_complete=%d",supported.samplerYcbcrConversion,ycbcr_declared&&!ycbcr,complete);
    return VK_ERROR_FEATURE_NOT_PRESENT;
  }
  VkDeviceCreateInfo augmented=*info;
  augmented.enabledExtensionCount=static_cast<uint32_t>(requested.size()); augmented.ppEnabledExtensionNames=requested.data();
  VkPhysicalDeviceSamplerYcbcrConversionFeatures enable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,const_cast<void*>(info->pNext),VK_TRUE};
  if(!ycbcr_declared) augmented.pNext=&enable;
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","explicit_input_request physical=%p name=%s physical_api=%u instance_api=%u core11=%d physical_ycbcr=%d original_ycbcr=%d original_extensions=%u requested_extensions=%u",physical,properties.deviceName,properties.apiVersion,instance_api,core11,supported.samplerYcbcrConversion,ycbcr,info->enabledExtensionCount,augmented.enabledExtensionCount);
  info=&augmented; ycbcr=true;
  const VkResult result = create_device(physical, info, allocation, device);
  if (result == VK_SUCCESS) {
    std::lock_guard<std::mutex> lock(device_mutex);
    proven_device = *device; proven_physical=physical; enabled_ycbcr = ycbcr;
    enabled_sync = HasExtension(info, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    enabled_ahb = HasExtension(info, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
    enabled_foreign=HasExtension(info,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    __android_log_print(ANDROID_LOG_INFO,"HVInputGate","foreign_extension_successful_device=%d device=%p",enabled_foreign,*device);
    __android_log_print(ANDROID_LOG_INFO,"HVInputGate",
      "device_creation success device=%p ycbcr_enabled=%d sync_fd_extension_enabled=%d ahb_extension_enabled=%d extension_count=%u",
      *device,ycbcr,enabled_sync,enabled_ahb,info->enabledExtensionCount);
    for(uint32_t i=0;i<info->enabledExtensionCount;++i)
      __android_log_print(ANDROID_LOG_INFO,"HVInputGate","enabled_extension=%s",info->ppEnabledExtensionNames[i]);
  } else __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL device_creation_result=%d",result);
  return result;
  });
  } catch (...) {
    __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL device_creation_exception");
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL CaptureGetProc(VkInstance instance, const char* name) {
  auto result = loader(instance,name);
  if (!std::strcmp(name,"vkCreateDevice") && result) {
    create_device = reinterpret_cast<PFN_vkCreateDevice>(result);
    return reinterpret_cast<PFN_vkVoidFunction>(CaptureCreateDevice);
  }
  if (!std::strcmp(name,"vkCreateInstance") && result) {
    create_instance=reinterpret_cast<PFN_vkCreateInstance>(result);
    return reinterpret_cast<PFN_vkVoidFunction>(CaptureCreateInstance);
  }
  return result;
}
static PFN_vkGetInstanceProcAddr UNITY_INTERFACE_API Initialize(PFN_vkGetInstanceProcAddr original,void*) {
  loader=original; return CaptureGetProc;
}
bool ProbeDecodedBuffer(AHardwareBuffer* buffer, VkPhysicalDevice physical, InputGpuCapabilities* out) {
  if(!buffer || !out || !unity_vulkan) return false;
  const auto instance = unity_vulkan->Instance();
  if(instance.physicalDevice != physical || !instance.device) return false;
  AHardwareBuffer_Desc desc{}; AHardwareBuffer_describe(buffer,&desc);
  out->usage=desc.usage; out->ahb_format=desc.format;
  {
    std::lock_guard<std::mutex> lock(device_mutex);
    out->logical_proven=proven_device==instance.device && proven_physical==physical;
    out->logical_ycbcr=enabled_ycbcr; out->logical_sync_fd=enabled_sync; out->logical_ahb=enabled_ahb;
  }
  auto get_device = reinterpret_cast<PFN_vkGetDeviceProcAddr>(instance.getInstanceProcAddr(instance.instance,"vkGetDeviceProcAddr"));
  auto get_ahb = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(get_device(instance.device,"vkGetAndroidHardwareBufferPropertiesANDROID"));
  auto get_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(instance.getInstanceProcAddr(instance.instance,"vkGetPhysicalDeviceFeatures2"));
  if(!get_features) get_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(instance.getInstanceProcAddr(instance.instance,"vkGetPhysicalDeviceFeatures2KHR"));
  auto get_external = reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(instance.getInstanceProcAddr(instance.instance,"vkGetPhysicalDeviceExternalSemaphoreProperties"));
  if(!get_external) get_external = reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(instance.getInstanceProcAddr(instance.instance,"vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
  if(!get_features || !get_external) return false;
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&ycbcr};
  get_features(physical,&features); out->physical_ycbcr=ycbcr.samplerYcbcrConversion;
  VkPhysicalDeviceExternalSemaphoreInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,nullptr,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
  VkExternalSemaphoreProperties semaphore{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
  get_external(physical,&external,&semaphore);
  out->sync_fd_features=semaphore.externalSemaphoreFeatures;
  out->sync_fd_compatible_types=semaphore.compatibleHandleTypes;
  const auto required=VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT|VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
  out->physical_sync_fd=(semaphore.externalSemaphoreFeatures & required)==required;
  if(!get_ahb) return false;
  VkAndroidHardwareBufferFormatPropertiesANDROID format{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
  VkAndroidHardwareBufferPropertiesANDROID properties{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,&format};
  if(get_ahb(instance.device,buffer,&properties)!=VK_SUCCESS) return false;
  out->format=format.format; out->external_format=format.externalFormat;
  out->format_features=format.formatFeatures;
  out->sampled=(format.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)!=0 && (desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)!=0;
  out->model=format.suggestedYcbcrModel; out->range=format.suggestedYcbcrRange;
  out->x_chroma=format.suggestedXChromaOffset; out->y_chroma=format.suggestedYChromaOffset;
  out->components[0]=format.samplerYcbcrConversionComponents.r;
  out->components[1]=format.samplerYcbcrConversionComponents.g;
  out->components[2]=format.samplerYcbcrConversionComponents.b;
  out->components[3]=format.samplerYcbcrConversionComponents.a;
  return true;
}
void RecordDecodedCapability(AndroidDecodedImage& image,const char* codec) {
  InputGpuCapabilities c{};
  bool queried=unity_vulkan && ProbeDecodedBuffer(image.buffer,unity_vulkan->Instance().physicalDevice,&c);
  int32_t width=0,height=0; AImage_getWidth(image.image,&width); AImage_getHeight(image.image,&height);
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate",
    "decoded codec=%s protocol=RTSP_TCP codec_type=H264 generation=%llu pts_us=%lld received_us=%lld decoded_us=%lld width=%d height=%d acquire_fd=%d acquire_state=%s ahb_format=%u usage=%llu vk_format=%u external_format=%llu format_features=%u sampled=%d physical_ycbcr=%d physical_sync_fd=%d logical_proven=%d logical_ycbcr=%d logical_sync_fd=%d logical_ahb=%d model=%u range=%u x_chroma=%u y_chroma=%u components=%u,%u,%u,%u query=%d failure=%d",
    codec,(unsigned long long)image.generation,(long long)image.pts_us,(long long)image.received_us,(long long)image.decoded_us,width,height,image.acquire_fd,image.acquire_fd<0?"already_complete":"fence_fd",
    c.ahb_format,(unsigned long long)c.usage,c.format,(unsigned long long)c.external_format,c.format_features,c.sampled,c.physical_ycbcr,c.physical_sync_fd,c.logical_proven,c.logical_ycbcr,c.logical_sync_fd,c.logical_ahb,c.model,c.range,c.x_chroma,c.y_chroma,c.components[0],c.components[1],c.components[2],c.components[3],queried,(int)AdmitDecodedBuffer(c));
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","capability_result=%s",queried && AdmitDecodedBuffer(c)==CapabilityFailure::None?"PASS":"FAIL");
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","sync_fd_properties importable=%d exportable=%d features=%u compatible_handle_types=%u logical_fd_extension=%d",(c.sync_fd_features&VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)!=0,(c.sync_fd_features&VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT)!=0,c.sync_fd_features,c.sync_fd_compatible_types,c.logical_sync_fd);
}
}
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* interfaces) {
  hvinput::unity_vulkan=interfaces->Get<IUnityGraphicsVulkan>();
  bool hooked=hvinput::unity_vulkan && hvinput::unity_vulkan->InterceptInitialization(hvinput::Initialize,nullptr);
  hvinput::graphics=interfaces->Get<IUnityGraphics>();
  if(hvinput::graphics)hvinput::graphics->RegisterDeviceEventCallback(hvinput::GraphicsEvent);
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","input_only_preinit_hook=%d",hooked);
}
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() { hvinput::StopCapabilityProbe(); hvinput::ShutdownInputColor(); if(hvinput::graphics)hvinput::graphics->UnregisterDeviceEventCallback(hvinput::GraphicsEvent); }
extern "C" __attribute__((visibility("default"))) void HV_Input_StartCapabilityProbe(const char* url) {
  try { hvinput::StartCapabilityProbe(url); }
  catch(...) { __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","capability_result=FAIL probe_initialization_exception"); }
}
extern "C" __attribute__((visibility("default"))) void HV_Input_StopCapabilityProbe() { hvinput::StopCapabilityProbe(); }
