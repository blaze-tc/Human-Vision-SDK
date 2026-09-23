#define __ANDROID__ 1
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define HV_ANDROID_ADAPTER_TEST 1
#define HUMANVISION_BUILDING_DLL 1
#include "gpu/android/unity_vulkan_plugin.cpp"
#include "composition/android_gpu_c.cpp"

#include <gtest/gtest.h>
#include <future>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace {
template <typename T> T Handle(uintptr_t value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
  else return static_cast<T>(value);
}
struct AdapterFacts {
  struct BarrierCall {
    VkPipelineStageFlags source_stage = 0;
    VkPipelineStageFlags destination_stage = 0;
    VkImageMemoryBarrier barrier{};
  };
  AHardwareBuffer_Desc requested{};
  int ahb_live = 0;
  int view_creates = 0;
  int render_view_creates = 0;
  int view_destroys = 0;
  std::unordered_set<uintptr_t> invalid_source_images;
  int invalid_source_view_creates = 0;
  bool dedicated_chain = false;
  bool queried_requirements = false;
  VkImageUsageFlags image_usage = 0;
  VkImageBlit blit{};
  uint32_t pushed_swap = 99;
  VkInstance gipa_instance = VK_NULL_HANDLE;
  std::vector<std::string> enabled_extensions;
  std::vector<std::string> enabled_instance_extensions;
  const void* instance_pnext = nullptr;
  bool omit_instance_semaphore_capabilities = false;
  int unity_access_calls = 0;
  bool inside_queue = false;
  bool access_from_queue = false;
  bool omit_sync_fd_extension = false;
  uint32_t described_width_delta = 0;
  int create_calls = 0;
  int fail_create_at = -1;
  bool export_failure = false;
  bool fence_complete = true;
  int device_idle_calls = 0;
  int fence_wait_calls = 0;
  std::unordered_set<uintptr_t> signaled_semaphores;
  uint32_t identity_query_api = 0;
  bool identity_query_properties2 = false;
  bool identity_query_external_memory = false;
  bool zero_uuid = false;
  UnityVulkanInstance active_instance{};
  PFN_vkGetInstanceProcAddr intercepted_gipa = nullptr;
  IUnityGraphicsDeviceEventCallback device_event = nullptr;
  UnityVulkanPluginEventConfig event_config{};
  int event_config_calls = 0;
  std::vector<BarrierCall> barriers;
  uintptr_t next = 100;
} g;

std::mutex g_queue_mutex;
std::condition_variable g_queue_cv;
bool g_queue_async = false;
bool g_queue_entered = false;
bool g_queue_release = false;
std::thread g_queue_thread;
std::mutex g_create_mutex;
std::condition_variable g_create_cv;
bool g_block_view_create = false;
bool g_view_create_entered = false;
bool g_release_view_create = false;

template <typename T> T NewHandle() { return Handle<T>(++g.next); }
bool FailCreate() { return g.create_calls++ == g.fail_create_at; }
VkResult VKAPI_CALL AhbProperties(VkDevice, const AHardwareBuffer*,
                                  VkAndroidHardwareBufferPropertiesANDROID* p) {
  p->allocationSize = 4096; p->memoryTypeBits = 1;
  auto* f = static_cast<VkAndroidHardwareBufferFormatPropertiesANDROID*>(p->pNext);
  f->format = VK_FORMAT_R8G8B8A8_UNORM;
  f->externalFormat = 0;
  f->formatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  return VK_SUCCESS;
}
VkResult VKAPI_CALL GetSemaphoreFd(VkDevice, const VkSemaphoreGetFdInfoKHR* info, int* fd) {
  if (g.export_failure) return VK_ERROR_OUT_OF_HOST_MEMORY;
  g.signaled_semaphores.erase(reinterpret_cast<uintptr_t>(info->semaphore));
  *fd=-1; return VK_SUCCESS;
}
VkResult VKAPI_CALL Enumerate(VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties* p) {
  static const char* names[] = {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                                VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
  const uint32_t available = g.omit_sync_fd_extension ? 1u : 2u;
  if (!p) { *count=available; return VK_SUCCESS; }
  *count = available;
  for (uint32_t i = 0; i < available; ++i)
    std::memcpy(p[i].extensionName, names[i], std::strlen(names[i]) + 1);
  return VK_SUCCESS;
}
VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo* ci,
                                 const VkAllocationCallbacks*, VkDevice* out) {
  g.enabled_extensions.clear();
  for(uint32_t i=0;i<ci->enabledExtensionCount;++i) g.enabled_extensions.emplace_back(ci->ppEnabledExtensionNames[i]);
  *out=NewHandle<VkDevice>(); return VK_SUCCESS;
}
VkResult VKAPI_CALL EnumerateInstance(const char*, uint32_t* count, VkExtensionProperties* p) {
  static const char* names[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};
  const uint32_t available=g.omit_instance_semaphore_capabilities?2u:3u;
  if (!p) {*count=available;return VK_SUCCESS;}
  *count=available;
  for(uint32_t i=0;i<available;++i) std::strcpy(p[i].extensionName,names[i]);
  return VK_SUCCESS;
}
VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks*, VkInstance* out) {
  g.instance_pnext=ci->pNext;
  g.enabled_instance_extensions.clear();
  for(uint32_t i=0;i<ci->enabledExtensionCount;++i) g.enabled_instance_extensions.emplace_back(ci->ppEnabledExtensionNames[i]);
  *out=Handle<VkInstance>(77); return VK_SUCCESS;
}
void VKAPI_CALL PhysicalProperties(VkPhysicalDevice,VkPhysicalDeviceProperties* out) {out->apiVersion=VK_API_VERSION_1_1;}
void VKAPI_CALL PhysicalProperties2(VkPhysicalDevice,VkPhysicalDeviceProperties2* out) {
  auto* ids=static_cast<VkPhysicalDeviceIDProperties*>(out->pNext);
  if(!g.zero_uuid){ids->deviceUUID[0]=0x11;ids->driverUUID[0]=0x22;}
}
PFN_vkVoidFunction VKAPI_CALL LoaderGipa(VkInstance instance, const char* name) {
  if (std::strcmp(name,"vkCreateInstance")==0) return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
  if (std::strcmp(name,"vkEnumerateInstanceExtensionProperties")==0) return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstance);
  if (std::strcmp(name,"vkGetPhysicalDeviceProperties")==0) return instance ? reinterpret_cast<PFN_vkVoidFunction>(PhysicalProperties) : nullptr;
  if (std::strcmp(name,"vkGetPhysicalDeviceProperties2")==0) {g.identity_query_api=VK_API_VERSION_1_1;return instance ? reinterpret_cast<PFN_vkVoidFunction>(PhysicalProperties2) : nullptr;}
  if (std::strcmp(name,"vkEnumerateDeviceExtensionProperties")==0 || std::strcmp(name,"vkCreateDevice")==0)
    g.gipa_instance=instance;
  if (!instance && (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0 ||
                    std::strcmp(name, "vkCreateDevice") == 0))
    return nullptr;
  if (std::strcmp(name,"vkEnumerateDeviceExtensionProperties")==0) return reinterpret_cast<PFN_vkVoidFunction>(Enumerate);
  if (std::strcmp(name,"vkCreateDevice")==0) return reinterpret_cast<PFN_vkVoidFunction>(CreateDevice);
  return nullptr;
}

IUnityGraphics graphics_api{};
IUnityGraphicsVulkanV2 vulkan_api{};
IUnityInterface* UNITY_INTERFACE_API GetInterface(UnityInterfaceGUID guid) {
  if (guid==GetUnityInterfaceGUID<IUnityGraphics>()) return &graphics_api;
  if (guid==GetUnityInterfaceGUID<IUnityGraphicsVulkanV2>()) return &vulkan_api;
  return nullptr;
}
void InstallAndCreateUnityDevice() {
  using humanvision::gpu::AndroidProducer;
  graphics_api={};vulkan_api={};
  graphics_api.GetRenderer=[]() {return kUnityGfxRendererVulkan;};
  graphics_api.RegisterDeviceEventCallback=[](IUnityGraphicsDeviceEventCallback cb) {g.device_event=cb;};
  graphics_api.UnregisterDeviceEventCallback=[](IUnityGraphicsDeviceEventCallback) {};
  vulkan_api.InterceptInitialization=[](UnityVulkanInitCallback cb,void* p) {g.intercepted_gipa=cb(LoaderGipa,p);return true;};
  vulkan_api.ConfigureEvent=[](int,const UnityVulkanPluginEventConfig* config) {g.event_config=*config;++g.event_config_calls;};
  vulkan_api.Instance=[]() {return g.active_instance;};
  static IUnityInterfaces interfaces{};interfaces.GetInterface=GetInterface;
  UnityPluginLoad(&interfaces);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instance_info.pApplicationInfo=&app;
  auto create_instance=reinterpret_cast<PFN_vkCreateInstance>(g.intercepted_gipa(nullptr,"vkCreateInstance"));
  ASSERT_NE(create_instance,nullptr);
  ASSERT_EQ(create_instance(&instance_info,nullptr,&g.active_instance.instance),VK_SUCCESS);
  g.active_instance.physicalDevice=Handle<VkPhysicalDevice>(88);
  g.active_instance.graphicsQueue=Handle<VkQueue>(99);
  VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  auto create_device=reinterpret_cast<PFN_vkCreateDevice>(g.intercepted_gipa(g.active_instance.instance,"vkCreateDevice"));
  ASSERT_EQ(create_device(g.active_instance.physicalDevice,&device_info,nullptr,&g.active_instance.device),VK_SUCCESS);
  g.device_event(kUnityGfxDeviceEventInitialize);
}
bool UNITY_INTERFACE_API UnityAccessTexture(
    void *texture, const VkImageSubresource *, VkImageLayout,
    VkPipelineStageFlags, VkAccessFlags, UnityVulkanResourceAccessMode,
    UnityVulkanImage *out) {
  ++g.unity_access_calls;
  g.access_from_queue = g.access_from_queue || g.inside_queue;
  out->image = Handle<VkImage>(reinterpret_cast<uintptr_t>(texture) + 1000);
  out->layout = reinterpret_cast<uintptr_t>(texture) == 1
                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  out->usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  out->format = VK_FORMAT_R8G8B8A8_UNORM;
  out->extent = {320, 240, 1};
  out->type = VK_IMAGE_TYPE_2D;
  out->samples = VK_SAMPLE_COUNT_1_BIT;
  return true;
}
void UNITY_INTERFACE_API UnityAccessQueue(UnityRenderingEventAndData callback,
                                          int event_id, void *data, bool) {
  if (g_queue_async) {
    g_queue_thread = std::thread([=] {
      {
        std::unique_lock<std::mutex> lock(g_queue_mutex);
        g_queue_entered = true;
        g_queue_cv.notify_all();
        g_queue_cv.wait(lock, [] { return g_queue_release; });
      }
      g.inside_queue = true;
      callback(event_id, data);
      g.inside_queue = false;
    });
    return;
  }
  g.inside_queue = true;
  callback(event_id, data);
  g.inside_queue = false;
}

humanvision::gpu::AhbSelection Selection(HV_AndroidGpuCopyPath path) {
  using namespace humanvision::gpu;
  AhbCandidate c; c.path=path; c.requested=c.actual={320,240,1,1,path==HV_ANDROID_GPU_COPY_BLIT?256u:768u,320};
  c.allocated=c.described=c.source_supported=true; c.source_transfer_src=c.source_blit_src=c.source_sampled=true; c.blit_conversion=true;
  auto fill=[](AhbImageFacts& f,uint32_t usage){f.properties=f.external_query=f.importable=f.compatible_handle=f.usage_compatible=f.extent_supported=f.sampled=f.image_created=f.memory_imported=f.memory_bound=f.view_created=true;f.vk_format=VK_FORMAT_R8G8B8A8_UNORM;f.image_usage=usage;};
  fill(c.producer,path==HV_ANDROID_GPU_COPY_BLIT?(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT):(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
  fill(c.consumer,VK_IMAGE_USAGE_SAMPLED_BIT); c.producer.transfer_dst=c.producer.blit_dst=c.producer.color_attachment=true;c.producer.framebuffer_created=true;
  AhbSelection selection;
  selection.path = path;
  selection.contract = c.actual;
  selection.candidates = {c};
  selection.source={320,240,37,5,0,1,1,1};
  selection.producer_identity.queried=selection.consumer_identity.queried=true;
  selection.producer_identity.device_uuid[0]=selection.consumer_identity.device_uuid[0]=0x11;
  selection.producer_identity.driver_uuid[0]=selection.consumer_identity.driver_uuid[0]=0x22;
  return selection;
}
humanvision::gpu::SlotContract Contract(HV_AndroidGpuCopyPath path) { humanvision::gpu::SlotContract c;c.width=320;c.height=240;c.actual_format=1;c.actual_usage=path==HV_ANDROID_GPU_COPY_BLIT?256:768;return c; }
humanvision::gpu::UnityVulkanDeviceContext Device() { humanvision::gpu::UnityVulkanDeviceContext c;c.instance=1;c.physical_device=2;c.device=3;c.graphics_queue=4;c.graphics_queue_family=5;return c; }
std::array<humanvision::gpu::BridgeBarrier,4> Barriers(uintptr_t source, uintptr_t dest) {
  using namespace humanvision::gpu; return {{{source,BridgeImageLayout::SourceCurrent,BridgeImageLayout::TransferSource,5,5},{dest,BridgeImageLayout::External,BridgeImageLayout::TransferDestination,UINT32_MAX-1,5},{dest,BridgeImageLayout::TransferDestination,BridgeImageLayout::External,5,UINT32_MAX-1},{source,BridgeImageLayout::TransferSource,BridgeImageLayout::SourceCurrent,5,5}}};
}
}

extern "C" {
int AHardwareBuffer_allocate(const AHardwareBuffer_Desc* d, AHardwareBuffer** out){if(FailCreate())return -1;g.requested=*d;*out=new AHardwareBuffer();++g.ahb_live;return 0;}
void AHardwareBuffer_acquire(AHardwareBuffer* b){++b->references;}
void AHardwareBuffer_describe(const AHardwareBuffer*, AHardwareBuffer_Desc* d){*d=g.requested;d->width+=g.described_width_delta;d->stride=d->width;}
void AHardwareBuffer_release(AHardwareBuffer* b){if(--b->references==0){delete b;--g.ahb_live;}}
PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char* n){if(std::strcmp(n,"vkGetAndroidHardwareBufferPropertiesANDROID")==0)return reinterpret_cast<PFN_vkVoidFunction>(AhbProperties);if(std::strcmp(n,"vkGetSemaphoreFdKHR")==0)return reinterpret_cast<PFN_vkVoidFunction>(GetSemaphoreFd);return nullptr;}
void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* p){p->memoryTypeCount=1;p->memoryTypes[0].propertyFlags=0;}
void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice, VkImage,
                                              VkMemoryRequirements *r) {
  g.queried_requirements = true;
  r->size = 4096;
  r->memoryTypeBits = 1;
}
VkResult VKAPI_CALL vkCreateImage(VkDevice,const VkImageCreateInfo* ci,const VkAllocationCallbacks*,VkImage* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;g.image_usage=ci->usage;*o=NewHandle<VkImage>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyImage(VkDevice,VkImage,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkAllocateMemory(VkDevice,const VkMemoryAllocateInfo* ai,const VkAllocationCallbacks*,VkDeviceMemory* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;auto* d=static_cast<const VkMemoryDedicatedAllocateInfo*>(ai->pNext);auto* i=d?static_cast<const VkImportAndroidHardwareBufferInfoANDROID*>(d->pNext):nullptr;g.dedicated_chain=d&&d->image&&i&&i->buffer;*o=NewHandle<VkDeviceMemory>();return VK_SUCCESS;}
void VKAPI_CALL vkFreeMemory(VkDevice,VkDeviceMemory,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkBindImageMemory(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return FailCreate()?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
VkResult VKAPI_CALL vkCreateImageView(VkDevice,const VkImageViewCreateInfo* info,const VkAllocationCallbacks*,VkImageView* o){
  if (g.invalid_source_images.count(reinterpret_cast<uintptr_t>(info->image))) {
    ++g.invalid_source_view_creates;
    return VK_ERROR_DEVICE_LOST;
  }
  {std::unique_lock<std::mutex> lock(g_create_mutex);if(g_block_view_create){g_view_create_entered=true;g_create_cv.notify_all();g_create_cv.wait(lock,[]{return g_release_view_create;});}}
  if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  ++g.view_creates;
  if(humanvision::gpu::AndroidProducer::TestInsideRenderEvent()) ++g.render_view_creates;
  *o=NewHandle<VkImageView>();return VK_SUCCESS;
}
void VKAPI_CALL vkDestroyImageView(VkDevice,VkImageView,const VkAllocationCallbacks*){++g.view_destroys;}
#define HV_CREATE(name,type) VkResult VKAPI_CALL name(VkDevice,const type*,const VkAllocationCallbacks*,decltype(type{} , (VkSampler*)nullptr) )
VkResult VKAPI_CALL vkCreateCommandPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkCommandPool>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyCommandPool(VkDevice,VkCommandPool,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkCommandBuffer>();return VK_SUCCESS;}
VkResult VKAPI_CALL vkCreateSemaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkSemaphore>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroySemaphore(VkDevice,VkSemaphore,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkFence>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateSampler(VkDevice,const VkSamplerCreateInfo*,const VkAllocationCallbacks*,VkSampler* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkSampler>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroySampler(VkDevice,VkSampler,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice,const VkDescriptorSetLayoutCreateInfo*,const VkAllocationCallbacks*,VkDescriptorSetLayout* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkDescriptorSetLayout>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice,VkDescriptorSetLayout,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice,const VkDescriptorPoolCreateInfo*,const VkAllocationCallbacks*,VkDescriptorPool* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkDescriptorPool>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyDescriptorPool(VkDevice,VkDescriptorPool,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice,const VkDescriptorSetAllocateInfo*,VkDescriptorSet* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkDescriptorSet>();return VK_SUCCESS;}
void VKAPI_CALL vkUpdateDescriptorSets(VkDevice,uint32_t,const VkWriteDescriptorSet*,uint32_t,const VkCopyDescriptorSet*){}
VkResult VKAPI_CALL vkCreateRenderPass(VkDevice,const VkRenderPassCreateInfo*,const VkAllocationCallbacks*,VkRenderPass* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkRenderPass>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyRenderPass(VkDevice,VkRenderPass,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice,const VkFramebufferCreateInfo*,const VkAllocationCallbacks*,VkFramebuffer* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkFramebuffer>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyFramebuffer(VkDevice,VkFramebuffer,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkPipelineLayout>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyPipelineLayout(VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateShaderModule(VkDevice,const VkShaderModuleCreateInfo*,const VkAllocationCallbacks*,VkShaderModule* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkShaderModule>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyShaderModule(VkDevice,VkShaderModule,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* o){if(FailCreate())return VK_ERROR_OUT_OF_DEVICE_MEMORY;*o=NewHandle<VkPipeline>();return VK_SUCCESS;}
void VKAPI_CALL vkDestroyPipeline(VkDevice,VkPipeline,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer,VkCommandBufferResetFlags){return VK_SUCCESS;}
VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer){return VK_SUCCESS;}
void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier* barriers){if(count==1&&barriers)g.barriers.push_back({source,destination,*barriers});}
void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit* r,VkFilter){g.blit=*r;}
void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer,const VkRenderPassBeginInfo*,VkSubpassContents){}
void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer){}
void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*){}
void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,uint32_t,uint32_t,const void* v){g.pushed_swap=*static_cast<const uint32_t*>(v);}
void VKAPI_CALL vkCmdDraw(VkCommandBuffer,uint32_t,uint32_t,uint32_t,uint32_t){}
VkResult VKAPI_CALL vkResetFences(VkDevice,uint32_t,const VkFence*){return VK_SUCCESS;}
VkResult VKAPI_CALL vkQueueSubmit(VkQueue,uint32_t,const VkSubmitInfo* info,VkFence){
  if (!info->signalSemaphoreCount) return VK_SUCCESS;
  const auto semaphore = reinterpret_cast<uintptr_t>(info->pSignalSemaphores[0]);
  EXPECT_EQ(g.signaled_semaphores.count(semaphore), 0u) << "VUID-vkQueueSubmit-pSignalSemaphores-00067";
  g.signaled_semaphores.insert(semaphore);
  return VK_SUCCESS;
}
VkResult VKAPI_CALL vkGetFenceStatus(VkDevice,VkFence){return g.fence_complete ? VK_SUCCESS : VK_NOT_READY;}
VkResult VKAPI_CALL vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++g.fence_wait_calls;return VK_SUCCESS;}
VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice){++g.device_idle_calls;return VK_SUCCESS;}
}
namespace humanvision::gpu { DeviceIdentity QueryDeviceIdentity(const VulkanDeviceContext& c){
  g.identity_query_api=c.instance_api_version;
  g.identity_query_properties2=c.properties2_extension;
  g.identity_query_external_memory=c.external_memory_capabilities_extension;
  DeviceIdentity id;id.queried=true;
  if(!g.zero_uuid){id.device_uuid[0]=0x11;id.driver_uuid[0]=0x22;}
  return id;
} DeviceMatch MatchNcnnDevice(const VulkanDeviceContext&){return {};} DeviceMatch MatchDevice(const DeviceIdentity&,const std::vector<IndexedDevice>&){return {};} }

using humanvision::gpu::AndroidProducer;
TEST(UnityVulkanAndroidAdapter, AndroidAbiDistinguishesUnavailableLifecycleFromMalformedSubmission) {
  humanvision::gpu::ShutdownUnityVulkanProducer();
  humanvision::runtime::RuntimeSession runtime;
  HV_AndroidGpuSubmissionV1 s{sizeof(s),HV_ANDROID_GPU_API_V1,reinterpret_cast<void*>(1),320,240,1,1000,0,0};
  void* event=reinterpret_cast<void*>(1);
  EXPECT_EQ(HV_RuntimePrepareAndroidGpuFrame(&runtime,&s,&event),HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM);
  EXPECT_EQ(event,nullptr);
  EXPECT_FALSE(runtime.error.empty());
  s.frame_id=0;
  EXPECT_EQ(HV_RuntimePrepareAndroidGpuFrame(&runtime,&s,&event),HV_ERR_INVALID_ARGUMENT);
  s.frame_id=1;s.timestamp_us=-1;
  EXPECT_EQ(HV_RuntimePrepareAndroidGpuFrame(&runtime,&s,&event),HV_ERR_INVALID_ARGUMENT);
  HV_AndroidGpuBridgeStatusV1 status{sizeof(status),HV_ANDROID_GPU_API_V1};
  EXPECT_EQ(HV_RuntimeGetAndroidGpuBridgeStatus(&runtime,&status),HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM);
  EXPECT_EQ(status.copy_path,HV_ANDROID_GPU_COPY_UNAVAILABLE);
}
TEST(UnityVulkanAndroidAdapter, UnexpectedDeviceShutdownQuarantinesWithoutWaitOrDestroyedDeviceCalls) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  ASSERT_TRUE(ConfigureUnityVulkanProducer(Selection(HV_ANDROID_GPU_COPY_BLIT),Contract(HV_ANDROID_GPU_COPY_BLIT)));
  const int destroyed=g.view_destroys;
  g.device_event(kUnityGfxDeviceEventShutdown);
  EXPECT_EQ(g.fence_wait_calls,0);
  EXPECT_EQ(g.device_idle_calls,0);
  EXPECT_EQ(g.view_destroys,destroyed);
  EXPECT_NE(std::string(AndroidProducer::TestDiagnostic()).find("control teardown"),std::string::npos);
  HV_AndroidGpuSubmissionV1 submission{sizeof(submission),HV_ANDROID_GPU_API_V1,reinterpret_cast<void*>(1),320,240,1,1000,0,0};
  void* event=nullptr;
  EXPECT_EQ(PrepareUnityVulkanFrame(submission,&event),BridgeResult::Closed);
  ShutdownUnityVulkanProducer();
  EXPECT_EQ(g.view_destroys,destroyed);
  EXPECT_EQ(g.ahb_live,0);
  UnityPluginUnload();
}
TEST(UnityVulkanAndroidAdapter, DeviceShutdownWaitsForBlockedGenerationCreation) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  {std::lock_guard<std::mutex> lock(g_create_mutex);g_block_view_create=true;g_view_create_entered=false;g_release_view_create=false;}
  auto configure=std::async(std::launch::async,[]{return ConfigureUnityVulkanProducer(
      Selection(HV_ANDROID_GPU_COPY_BLIT),Contract(HV_ANDROID_GPU_COPY_BLIT));});
  {std::unique_lock<std::mutex> lock(g_create_mutex);
   ASSERT_TRUE(g_create_cv.wait_for(lock,std::chrono::seconds(2),[]{return g_view_create_entered;}));}
  auto shutdown=std::async(std::launch::async,[]{g.device_event(kUnityGfxDeviceEventShutdown);});
  EXPECT_EQ(shutdown.wait_for(std::chrono::milliseconds(30)),std::future_status::timeout);
  {std::lock_guard<std::mutex> lock(g_create_mutex);g_release_view_create=true;g_block_view_create=false;}
  g_create_cv.notify_all();
  EXPECT_EQ(configure.wait_for(std::chrono::seconds(2)),std::future_status::ready);
  EXPECT_EQ(shutdown.wait_for(std::chrono::seconds(2)),std::future_status::ready);
  ShutdownUnityVulkanProducer();
  EXPECT_EQ(g.ahb_live,0);
  UnityPluginUnload();
}
TEST(UnityVulkanAndroidAdapter, PreloadCapturesEnabledInstanceFactsAndConfiguresPermittedEvent) {
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  EXPECT_EQ(g.identity_query_api,VK_API_VERSION_1_1);
  EXPECT_GT(g.event_config_calls,0);
  EXPECT_EQ(g.event_config.renderPassPrecondition,kUnityVulkanRenderPass_EnsureOutside);
  EXPECT_EQ(g.event_config.graphicsQueueAccess,kUnityVulkanGraphicsQueueAccess_DontCare);
  EXPECT_EQ(std::string(AndroidProducer::TestDiagnostic()),"");
  humanvision::gpu::ShutdownUnityVulkanProducer();
  UnityPluginUnload();
}
TEST(UnityVulkanAndroidAdapter, RejectsDifferentLogicalDeviceAndZeroUuidAfterInterception) {
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  auto intercepted=g.active_instance.device;
  g.active_instance.device=Handle<VkDevice>(901);
  g.device_event(kUnityGfxDeviceEventInitialize);
  EXPECT_NE(std::string(AndroidProducer::TestDiagnostic()).find("intercepted"),std::string::npos);
  g.active_instance.device=intercepted;g.zero_uuid=true;
  g.device_event(kUnityGfxDeviceEventInitialize);
  EXPECT_NE(std::string(AndroidProducer::TestDiagnostic()).find("UUID"),std::string::npos);
  humanvision::gpu::ShutdownUnityVulkanProducer();
  UnityPluginUnload();
}
TEST(UnityVulkanAndroidAdapter, FailedExportRetainsSlotUntilFenceAndSemaphorePayloadRetire) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_BLIT),Selection(HV_ANDROID_GPU_COPY_BLIT),cache));
  auto dispatch=AndroidProducer::Get().MakeDispatch();
  ASSERT_TRUE(dispatch.submit_signal(dispatch.context,Device(),cache));
  g.export_failure=true;
  SyncFd fd;
  EXPECT_FALSE(dispatch.export_sync_fd(dispatch.context,Device(),cache,fd));
  g.fence_complete=false;
  EXPECT_FALSE(dispatch.submission_complete(dispatch.context,Device(),cache));
  g.fence_complete=true;
  EXPECT_FALSE(dispatch.submission_complete(dispatch.context,Device(),cache));
  EXPECT_EQ(g.signaled_semaphores.size(),1u);
  g.export_failure=false;
  EXPECT_TRUE(dispatch.submission_complete(dispatch.context,Device(),cache));
  EXPECT_TRUE(g.signaled_semaphores.empty());
  ASSERT_TRUE(dispatch.submit_signal(dispatch.context,Device(),cache));
  EXPECT_TRUE(dispatch.export_sync_fd(dispatch.context,Device(),cache,fd));
  AndroidProducer::TestDrain(0,cache);
}
TEST(UnityVulkanAndroidAdapter, SlotDrainWaitsOnlyOwnedFenceNotUnityDeviceIdle) {
  g=AdapterFacts{};
  humanvision::gpu::UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_BLIT),Selection(HV_ANDROID_GPU_COPY_BLIT),cache));
  AndroidProducer::TestDrain(0,cache);
  EXPECT_EQ(g.device_idle_calls,0);
}
TEST(UnityVulkanAndroidAdapter, DedicatedAhbImportUsesActualPropertiesWithoutPrebindRequirementsQuery) {
  g=AdapterFacts{}; auto selection=Selection(HV_ANDROID_GPU_COPY_BLIT); humanvision::gpu::UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_BLIT),selection,cache));
  EXPECT_TRUE(g.dedicated_chain); EXPECT_FALSE(g.queried_requirements); EXPECT_EQ(g.ahb_live,1);
  AndroidProducer::TestDrain(0,cache); EXPECT_EQ(g.ahb_live,0); EXPECT_EQ(g.view_creates,g.view_destroys);
}
TEST(UnityVulkanAndroidAdapter, InterceptionUsesNonNullInstanceAndAddsOnlySupportedRequirements) {
  g=AdapterFacts{}; AndroidProducer::TestInstallGipa(LoaderGipa); auto instance=Handle<VkInstance>(77);
  EXPECT_EQ(AndroidProducer::TestInterceptGipa(VK_NULL_HANDLE,
                                               "vkCreateDevice"),
            nullptr);
  EXPECT_NE(AndroidProducer::TestInterceptGipa(instance,"vkCreateDevice"),nullptr);
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; VkDevice device{};
  ASSERT_EQ(AndroidProducer::TestInterceptCreateDevice(Handle<VkPhysicalDevice>(88),&ci,&device),VK_SUCCESS);
  EXPECT_EQ(g.gipa_instance,instance); EXPECT_TRUE(AndroidProducer::TestRequiredExtensionsEnabled());
  EXPECT_EQ(AndroidProducer::TestInterceptedPhysicalDevice(),88u); EXPECT_EQ(g.enabled_extensions.size(),2u);
}
TEST(UnityVulkanAndroidAdapter, Vulkan10InstanceAddsSupportedRequirementsAndPreservesChain) {
  g=AdapterFacts{};
  AndroidProducer::TestInstallGipa(LoaderGipa);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_0;
  const char* existing="VK_existing_extension";
  int chain=42;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo=&app;ci.pNext=&chain;ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=&existing;
  VkInstance instance{};
  ASSERT_EQ(AndroidProducer::TestInterceptCreateInstance(&ci,&instance),VK_SUCCESS);
  EXPECT_EQ(g.instance_pnext,&chain);
  ASSERT_EQ(g.enabled_instance_extensions.size(),4u);
  EXPECT_EQ(g.enabled_instance_extensions[0],existing);
  EXPECT_EQ(g.enabled_instance_extensions[1],VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  EXPECT_EQ(g.enabled_instance_extensions[2],VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
  EXPECT_EQ(g.enabled_instance_extensions[3],VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
  g.omit_instance_semaphore_capabilities=true;
  EXPECT_EQ(AndroidProducer::TestInterceptCreateInstance(&ci,&instance),VK_ERROR_EXTENSION_NOT_PRESENT);
  EXPECT_NE(std::string(AndroidProducer::TestDiagnostic()).find(
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME),std::string::npos);
}
TEST(UnityVulkanAndroidAdapter,
     MissingRequiredExtensionDoesNotCorruptUnityCreateChainAndStaysUnavailable) {
  g = AdapterFacts{};
  g.omit_sync_fd_extension = true;
  AndroidProducer::TestInstallGipa(LoaderGipa);
  auto instance = Handle<VkInstance>(77);
  AndroidProducer::TestInterceptGipa(instance, "vkCreateDevice");
  const char *existing = "VK_existing_extension";
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.enabledExtensionCount = 1;
  ci.ppEnabledExtensionNames = &existing;
  VkDevice device{};
  ASSERT_EQ(AndroidProducer::TestInterceptCreateDevice(
                Handle<VkPhysicalDevice>(88), &ci, &device),
            VK_SUCCESS);
  EXPECT_FALSE(AndroidProducer::TestRequiredExtensionsEnabled());
  ASSERT_EQ(g.enabled_extensions.size(), 2u);
  EXPECT_EQ(g.enabled_extensions[0], existing);
  EXPECT_EQ(g.enabled_extensions[1],
            VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
}
TEST(UnityVulkanAndroidAdapter,
     UnityTextureAccessStaysOutsideAccessQueueAndCarriesDistinctPriorLayouts) {
  g = AdapterFacts{};
  IUnityGraphicsVulkanV2 api{};
  api.AccessTexture = UnityAccessTexture;
  api.AccessQueue = UnityAccessQueue;
  AndroidProducer::TestInstallUnityVulkan(&api);
  humanvision::gpu::UnityTextureAccess first{}, second{};
  ASSERT_TRUE(AndroidProducer::TestAccess(reinterpret_cast<void *>(1), first));
  ASSERT_TRUE(AndroidProducer::TestAccess(reinterpret_cast<void *>(2), second));
  EXPECT_EQ(first.native_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  EXPECT_EQ(second.native_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  EXPECT_NE(first.image, second.image);
  AndroidProducer::TestRelease(reinterpret_cast<void *>(1), first);
  AndroidProducer::TestRelease(reinterpret_cast<void *>(2), second);
  EXPECT_EQ(g.unity_access_calls, 2);
  const int before_queue = g.unity_access_calls;
  ASSERT_TRUE(AndroidProducer::TestQueue());
  EXPECT_EQ(g.unity_access_calls, before_queue);
  EXPECT_FALSE(g.access_from_queue);
}
TEST(UnityVulkanAndroidAdapter, BlitUsesEntireObservedSourceExtentAndScaledDestination) {
  g=AdapterFacts{}; auto selection=Selection(HV_ANDROID_GPU_COPY_BLIT); humanvision::gpu::UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_BLIT),selection,cache));
  humanvision::gpu::UnityTextureAccess a{};a.image=900;a.width=1280;a.height=720;a.native_layout=VK_IMAGE_LAYOUT_GENERAL;a.native_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;a.native_access=VK_ACCESS_MEMORY_READ_BIT;
  auto b=Barriers(a.image,cache.image); ASSERT_TRUE(AndroidProducer::TestBlit(cache,a,b.data()));
  EXPECT_EQ(g.blit.srcOffsets[1].x,1280);EXPECT_EQ(g.blit.srcOffsets[1].y,720);EXPECT_EQ(g.blit.dstOffsets[1].x,320);EXPECT_EQ(g.blit.dstOffsets[1].y,240);
  ASSERT_EQ(g.barriers.size(), 4u);
  EXPECT_EQ(g.barriers[0].source_stage, a.native_stage);
  EXPECT_EQ(g.barriers[0].barrier.oldLayout,
            static_cast<VkImageLayout>(a.native_layout));
  EXPECT_EQ(g.barriers[0].barrier.srcAccessMask, a.native_access);
  EXPECT_EQ(g.barriers[1].barrier.srcQueueFamilyIndex,
            VK_QUEUE_FAMILY_EXTERNAL);
  EXPECT_EQ(g.barriers[1].barrier.dstQueueFamilyIndex, 5u);
  EXPECT_EQ(g.barriers[2].barrier.srcQueueFamilyIndex, 5u);
  EXPECT_EQ(g.barriers[2].barrier.dstQueueFamilyIndex,
            VK_QUEUE_FAMILY_EXTERNAL);
  EXPECT_EQ(g.barriers[3].destination_stage, a.native_stage);
  EXPECT_EQ(g.barriers[3].barrier.newLayout,
            static_cast<VkImageLayout>(a.native_layout));
  EXPECT_EQ(g.barriers[3].barrier.dstAccessMask, a.native_access);
  AndroidProducer::TestDrain(0,cache);
}
TEST(UnityVulkanAndroidAdapter,
     AsyncQueueCancellationClosesAdmissionAndWaitsForLastNotifier) {
  g = AdapterFacts{};
  g_queue_async = true;
  g_queue_entered = false;
  g_queue_release = false;
  IUnityGraphicsVulkanV2 api{};
  api.AccessTexture = UnityAccessTexture;
  api.AccessQueue = UnityAccessQueue;
  AndroidProducer::TestInstallUnityVulkan(&api);
  ASSERT_TRUE(AndroidProducer::TestQueue());
  {
    std::unique_lock<std::mutex> lock(g_queue_mutex);
    ASSERT_TRUE(g_queue_cv.wait_for(
        lock, std::chrono::seconds(2), [] { return g_queue_entered; }));
  }
  auto cancel = std::async(std::launch::async,
                           [] { AndroidProducer::TestCancel(); });
  EXPECT_EQ(cancel.wait_for(std::chrono::milliseconds(30)),
            std::future_status::timeout);
  EXPECT_FALSE(AndroidProducer::TestQueue());
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_queue_release = true;
  }
  g_queue_cv.notify_all();
  EXPECT_EQ(cancel.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  if (g_queue_thread.joinable())
    g_queue_thread.join();
  EXPECT_FALSE(g.access_from_queue);
  g_queue_async = false;
}
TEST(UnityVulkanAndroidAdapter,
     PartialCreateFailureAtEveryObjectStageRollsBackAhbAndViews) {
  for (const auto path : {HV_ANDROID_GPU_COPY_BLIT,
                          HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT}) {
    g = AdapterFacts{};
    auto selection = Selection(path);
    humanvision::gpu::UnityVulkanSlotCache cache{};
    ASSERT_TRUE(AndroidProducer::TestCreate(0, Device(), Contract(path),
                                            selection, cache));
    const int stage_count = g.create_calls;
    AndroidProducer::TestDrain(0, cache);
    ASSERT_GT(stage_count, 0);
    EXPECT_EQ(g.ahb_live, 0);
    EXPECT_EQ(g.view_creates, g.view_destroys);
    for (int failure = 0; failure < stage_count; ++failure) {
      g = AdapterFacts{};
      g.fail_create_at = failure;
      EXPECT_FALSE(AndroidProducer::TestCreate(0, Device(), Contract(path),
                                               selection, cache))
          << "path=" << path << " create-stage=" << failure;
      EXPECT_EQ(g.ahb_live, 0)
          << "path=" << path << " create-stage=" << failure;
      EXPECT_EQ(g.view_creates, g.view_destroys)
          << "path=" << path << " create-stage=" << failure;
      EXPECT_EQ(cache.ahb, 0u);
    }
  }
}
TEST(UnityVulkanAndroidAdapter,
     EveryPermanentAhbIsRedescribedAndMismatchRollsBackAllReferences) {
  g = AdapterFacts{};
  g.described_width_delta = 1;
  auto selection = Selection(HV_ANDROID_GPU_COPY_BLIT);
  humanvision::gpu::UnityVulkanSlotCache cache{};
  EXPECT_FALSE(AndroidProducer::TestCreate(
      0, Device(), Contract(HV_ANDROID_GPU_COPY_BLIT), selection, cache));
  EXPECT_EQ(g.ahb_live, 0);
  EXPECT_EQ(cache.ahb, 0u);
}
TEST(UnityVulkanAndroidAdapter, ColorSourceViewsWarmOnceAndTypedBgraDoesNotSwap) {
  g=AdapterFacts{}; InstallAndCreateUnityDevice(); auto selection=Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
  ASSERT_TRUE(humanvision::gpu::ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  ASSERT_TRUE(humanvision::gpu::BeginUnityVulkanSourceLease(reinterpret_cast<void*>(1)));
  humanvision::gpu::UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT),selection,cache));
  humanvision::gpu::UnityTextureAccess a{};a.texture=reinterpret_cast<void*>(1);a.image=901;a.format=VK_FORMAT_B8G8R8A8_UNORM;a.width=320;a.height=240;a.native_layout=VK_IMAGE_LAYOUT_GENERAL;a.native_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;a.native_access=VK_ACCESS_MEMORY_READ_BIT;
  const int generation_views=g.view_creates;
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache,a),humanvision::gpu::SourcePreparation::Warmed);
  EXPECT_EQ(g.render_view_creates,0);
  AndroidProducer::TestQuiesceSourceWorker();
  EXPECT_EQ(g.view_creates,generation_views+1);
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache,a),humanvision::gpu::SourcePreparation::Ready);
  EXPECT_EQ(g.view_creates,generation_views+1);
  auto b=Barriers(a.image,cache.image);b[0].new_layout=humanvision::gpu::BridgeImageLayout::ShaderRead;b[1].new_layout=humanvision::gpu::BridgeImageLayout::ColorAttachment;b[2].old_layout=humanvision::gpu::BridgeImageLayout::ColorAttachment;b[3].old_layout=humanvision::gpu::BridgeImageLayout::ShaderRead;
  ASSERT_TRUE(AndroidProducer::TestColor(cache,a,b.data())); EXPECT_EQ(g.pushed_swap,0u); EXPECT_EQ(g.view_creates,generation_views+1);
  a.image = 902;
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache, a),
            humanvision::gpu::SourcePreparation::Unsupported);
  EXPECT_EQ(g.render_view_creates,0);
  const int full_cache_views = g.view_creates;
  EXPECT_EQ(g.view_creates, full_cache_views);
  EXPECT_NE(std::string(AndroidProducer::TestDiagnostic()).find(
                "source image identity changed"),
            std::string::npos);
  AndroidProducer::TestDrain(0,cache);
  humanvision::gpu::EndUnityVulkanSourceLease();
  EXPECT_EQ(g.view_creates,g.view_destroys);
  UnityPluginUnload();
}
TEST(UnityVulkanAndroidAdapter, SourceIdentityChangeRequiresControlRebuild) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  auto selection=Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
  ASSERT_TRUE(ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  humanvision::runtime::RuntimeSession runtime;
  ASSERT_EQ(HV_RuntimeBeginAndroidGpuSourceLease(&runtime,reinterpret_cast<void*>(1)),HV_OK);
  UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT),selection,cache));
  UnityTextureAccess access{};access.texture=reinterpret_cast<void*>(1);access.format=VK_FORMAT_R8G8B8A8_UNORM;
  const uint64_t before=AndroidProducer::TestGeneration();
  access.image=901;
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache,access),SourcePreparation::Warmed);
  AndroidProducer::TestQuiesceSourceWorker();
  access.image=902;
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache,access),SourcePreparation::Unsupported);
  EXPECT_EQ(AndroidProducer::TestGeneration(),before);
  EXPECT_EQ(g.render_view_creates,0);
  AndroidProducer::TestDrain(0,cache);
  ShutdownUnityVulkanProducer();
  EXPECT_EQ(g.ahb_live,0);
  UnityPluginUnload();
}
TEST(UnityVulkanAndroidAdapter, SourceViewCreationFailureClosesAdmissionWithoutRebuild) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  auto selection=Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
  ASSERT_TRUE(ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT),selection,cache));
  const uint64_t generation=AndroidProducer::TestGeneration();
  g.fail_create_at=g.create_calls;
  ASSERT_TRUE(BeginUnityVulkanSourceLease(reinterpret_cast<void*>(1)));
  UnityTextureAccess access{};access.texture=reinterpret_cast<void*>(1);access.image=910;access.format=VK_FORMAT_R8G8B8A8_UNORM;
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache,access),SourcePreparation::Warmed);
  AndroidProducer::TestQuiesceSourceWorker();
  EXPECT_EQ(AndroidProducer::TestGeneration(),generation);
  EXPECT_NE(std::string(AndroidProducer::TestDiagnostic()).find("image-view creation failed"),std::string::npos);
  HV_AndroidGpuSubmissionV1 frame{sizeof(frame),HV_ANDROID_GPU_API_V1,reinterpret_cast<void*>(1),320,240,1,1000,0,0};
  void* event=nullptr;
  EXPECT_EQ(PrepareUnityVulkanFrame(frame,&event),BridgeResult::Closed);
  EXPECT_EQ(g.render_view_creates,0);
  g.fail_create_at=-1;
  AndroidProducer::TestDrain(0,cache);
  ShutdownUnityVulkanProducer();
  UnityPluginUnload();
}

TEST(UnityVulkanAndroidAdapter, ReconfigureDiscardsQueuedSourceFromPriorGeneration) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  auto selection=Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
  ASSERT_TRUE(ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  ASSERT_TRUE(BeginUnityVulkanSourceLease(reinterpret_cast<void*>(1)));
  UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT),selection,cache));
  UnityTextureAccess source{}; source.texture=reinterpret_cast<void*>(1);
  source.image=901; source.format=VK_FORMAT_R8G8B8A8_UNORM;
  AndroidProducer::TestPauseSourceWorker();
  ASSERT_EQ(AndroidProducer::TestPrepareSource(cache,source),SourcePreparation::Warmed);
  ASSERT_TRUE(ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  g.invalid_source_images.insert(901);
  AndroidProducer::TestResumeSourceWorker();
  AndroidProducer::TestQuiesceSourceWorker();
  EXPECT_EQ(g.invalid_source_view_creates,0);
  AndroidProducer::TestDrain(0,cache);
  ShutdownUnityVulkanProducer();
  UnityPluginUnload();
}

TEST(UnityVulkanAndroidAdapter, EndingSourceLeaseCancelsPendingViewBeforeImageDestruction) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  auto selection=Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
  ASSERT_TRUE(ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  humanvision::runtime::RuntimeSession runtime;
  ASSERT_TRUE(BeginUnityVulkanSourceLease(reinterpret_cast<void*>(1)));
  UnityVulkanSlotCache cache{};
  ASSERT_TRUE(AndroidProducer::TestCreate(0,Device(),Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT),selection,cache));
  UnityTextureAccess source{}; source.texture=reinterpret_cast<void*>(1);
  source.image=902; source.format=VK_FORMAT_R8G8B8A8_UNORM;
  AndroidProducer::TestPauseSourceWorker();
  ASSERT_EQ(AndroidProducer::TestPrepareSource(cache,source),SourcePreparation::Warmed);
  EXPECT_EQ(HV_RuntimeEndAndroidGpuSourceLease(&runtime),HV_OK);
  g.invalid_source_images.insert(902);
  AndroidProducer::TestResumeSourceWorker();
  AndroidProducer::TestQuiesceSourceWorker();
  EXPECT_EQ(g.invalid_source_view_creates,0);
  EXPECT_EQ(AndroidProducer::TestPrepareSource(cache,source),SourcePreparation::Unsupported);
  AndroidProducer::TestDrain(0,cache);
  UnityPluginUnload();
}

TEST(UnityVulkanAndroidAdapter, ColorFrameAdmissionRequiresMatchingSourceLease) {
  using namespace humanvision::gpu;
  g=AdapterFacts{};
  InstallAndCreateUnityDevice();
  auto selection=Selection(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
  ASSERT_TRUE(ConfigureUnityVulkanProducer(selection,Contract(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT)));
  humanvision::runtime::RuntimeSession runtime;
  HV_AndroidGpuSubmissionV1 frame{sizeof(frame),HV_ANDROID_GPU_API_V1,reinterpret_cast<void*>(1),320,240,1,1000,0,0};
  void* event=reinterpret_cast<void*>(123);
  EXPECT_EQ(PrepareUnityVulkanFrame(frame,&event),BridgeResult::Closed);
  EXPECT_EQ(event,nullptr);
  ASSERT_EQ(HV_RuntimeBeginAndroidGpuSourceLease(&runtime,reinterpret_cast<void*>(1)),HV_OK);
  frame.unity_texture=reinterpret_cast<void*>(2);
  EXPECT_EQ(PrepareUnityVulkanFrame(frame,&event),BridgeResult::Closed);
  frame.unity_texture=reinterpret_cast<void*>(1);
  EXPECT_EQ(PrepareUnityVulkanFrame(frame,&event),BridgeResult::Ok);
  EXPECT_EQ(HV_RuntimeEndAndroidGpuSourceLease(&runtime),HV_OK);
  EXPECT_EQ(PrepareUnityVulkanFrame(frame,&event),BridgeResult::Closed);
  EXPECT_EQ(event,nullptr);
  UnityPluginUnload();
}
