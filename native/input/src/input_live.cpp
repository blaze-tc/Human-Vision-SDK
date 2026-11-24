#include "humanvision_input_live.h"
#include "input_internal.h"
#include "live_bridge_state.h"
#include "native_live_contract.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#ifdef __ANDROID__
#include "android/input_vulkan_private.h"
#include "android/input_conversion.h"
#include "android/native_vulkan_image_service.h"
#include <android/log.h>
#include <unistd.h>
#endif

namespace {
template<class T> bool Valid(const T* p){return p&&p->size==sizeof(T)&&p->version==1;}
bool ConfigValid(const HV_InputLiveConfigV1* p){return Valid(p)&&hvinput::ValidNativeLiveOptions(
 p->max_width,p->max_height,p->rotation_degrees,p->mirror_x,p->enabled,p->reserved);}
}
#ifdef __ANDROID__
struct HV_InputLiveConsumerOpaque {
 HV_InputHandle owner{};
 std::shared_ptr<hvinput::NativeVulkanImageService> source;
 std::mutex mutex;
 hvinput::LiveBridgeState state;
 HV_InputLiveConfigV1 config{};
 HV_InputLiveTargetsV1 targets{};
 std::array<HV_InputLiveFrameV1,3> frames{};
 uint64_t after{};
 std::string error;
 struct Api : hvinput::InputConversionApi {UnityVulkanInstance unity{};} api;
 struct Imported {AHardwareBuffer* ahb{};VkImage image{};VkDeviceMemory memory{};};
 std::array<Imported,3> imported{};
 VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
 UnityVulkanImage target_image{};
 HV_InputNativeFrameV1 active{};
 uint64_t device_epoch{};
 int target_slot=-1;
 bool inflight{},unfenced{},resources_retired=true,failed{};
};
namespace {
constexpr int kLiveEvent=HV_INPUT_LIVE_RENDER_EVENT_ID;
constexpr VkImageSubresourceRange kColor{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
bool Error(HV_InputLiveHandle h,const char* reason){
 if(!h->failed){h->error=reason;__android_log_print(ANDROID_LOG_ERROR,"HVInputLive","%s",reason);}
 h->failed=true;h->state.RetireTargets();return false;
}
bool VkCheck(HV_InputLiveHandle h,VkResult result,const char* stage){
 if(result==VK_SUCCESS)return true;char text[192]{};std::snprintf(text,sizeof(text),"%s: Vulkan result %d",stage,int(result));return Error(h,text);
}
VkImageMemoryBarrier Barrier(VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst,uint32_t a=VK_QUEUE_FAMILY_IGNORED,uint32_t b=VK_QUEUE_FAMILY_IGNORED){
 VkImageMemoryBarrier out{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};out.image=image;out.oldLayout=from;out.newLayout=to;
 out.srcAccessMask=src;out.dstAccessMask=dst;out.srcQueueFamilyIndex=a;out.dstQueueFamilyIndex=b;out.subresourceRange=kColor;return out;
}
bool DeviceIsCurrent(HV_InputLiveHandle h,const hvinput::LiveDeviceLifetime::Lease& device){
 if(device.Matches(h->device_epoch))return true;
 // An old VkDevice can no longer be used even to poll or destroy a fence.
 // Retain everything whose GPU completion cannot be observed.
 if(h->pool)h->unfenced=true;
 return Error(h,"Unity Vulkan device changed before live preview targets retired");
}
bool Initialize(HV_InputLiveHandle h,IUnityGraphicsVulkan& unity){
 const auto u=unity.Instance();
 if(const char* error=hvinput::InputVulkanContextError(u))return Error(h,error);
 if(!hvinput::InputForeignEnabled(u))return Error(h,"Live preview requires the qualified Unity Vulkan external-memory device");
 if(h->pool){
  const auto& previous=h->api.unity;
  if(previous.instance==u.instance&&previous.physicalDevice==u.physicalDevice&&previous.device==u.device&&
     previous.graphicsQueue==u.graphicsQueue&&previous.queueFamilyIndex==u.queueFamilyIndex&&previous.getInstanceProcAddr==u.getInstanceProcAddr)return true;
  h->unfenced=true;
  return Error(h,"Unity Vulkan context changed while live preview resources were owned");
 }
 h->api.unity=u;
 if(!h->api.LoadFunctions(u.device,u.instance,u.getInstanceProcAddr))return Error(h,"Unity live Vulkan functions are unavailable");
 HV_InputNativeDeviceV1 producer{sizeof(producer),1};
 if(h->source->Device(producer)!=HV_INPUT_OK)return Error(h,"Native producer device identity unavailable");
 auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(u.getInstanceProcAddr(u.instance,"vkGetPhysicalDeviceProperties2"));
 if(!properties)return Error(h,"Unity physical-device identity function unavailable");
 VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&id};
 properties(u.physicalDevice,&prop);
 if(std::memcmp(id.deviceUUID,producer.device_uuid,16)||std::memcmp(id.driverUUID,producer.driver_uuid,16))return Error(h,"Native and Unity live devices do not share physical/driver identity");
 VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=u.queueFamilyIndex;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
 if(!VkCheck(h,h->api.CreateCommandPool(u.device,&pool,nullptr,&h->pool),"create live command pool"))return false;
 h->resources_retired=false;
 VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=h->pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
 if(!VkCheck(h,h->api.AllocateCommandBuffers(u.device,&allocation,&h->command),"allocate live command buffer"))return false;
 VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 return VkCheck(h,h->api.CreateFence(u.device,&fence,nullptr,&h->fence),"create live completion fence");
}
void DestroyResources(HV_InputLiveHandle h){
 if(h->inflight||h->unfenced)return;
 auto d=h->api.unity.device;
 if(d){for(auto& i:h->imported){if(i.image)h->api.DestroyImage(d,i.image,nullptr);if(i.memory)h->api.FreeMemory(d,i.memory,nullptr);if(i.ahb)AHardwareBuffer_release(i.ahb);i={};}
  if(h->fence)h->api.DestroyFence(d,h->fence,nullptr);if(h->pool)h->api.DestroyCommandPool(d,h->pool,nullptr);}
 h->fence={};h->pool={};h->command={};h->targets={};h->device_epoch=0;h->api.unity={};h->resources_retired=true;
}
bool Import(HV_InputLiveHandle h,const HV_InputNativeFrameV1& frame){
 if(frame.slot>=3||!frame.hardware_buffer)return Error(h,"Invalid live producer slot/AHB");
 auto& out=h->imported[frame.slot];auto* ahb=static_cast<AHardwareBuffer*>(frame.hardware_buffer);
 if(out.ahb)return out.ahb==ahb?true:Error(h,"Native live pool changed while Unity imports remain owned");
 auto& api=h->api;AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(ahb,&desc);
 if(desc.width!=frame.frame.width||desc.height!=frame.frame.height||desc.layers!=1||desc.format!=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM)return Error(h,"Live AHB is not the declared RGBA8 extent");
 VkAndroidHardwareBufferFormatPropertiesANDROID format{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
 VkAndroidHardwareBufferPropertiesANDROID props{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,&format};
 if(!VkCheck(h,api.GetAndroidHardwareBufferPropertiesANDROID(api.unity.device,ahb,&props),"query live AHB import"))return false;
 if(format.format!=VK_FORMAT_R8G8B8A8_UNORM||!(format.formatFeatures&VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))return Error(h,"Unity device cannot transfer the native RGBA AHB");
 VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
 VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,&external};image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;
 image.extent={desc.width,desc.height,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
 if(!VkCheck(h,api.CreateImage(api.unity.device,&image,nullptr,&out.image),"create imported live AHB image"))return false;
 VkPhysicalDeviceMemoryProperties memories{};api.GetPhysicalDeviceMemoryProperties(api.unity.physicalDevice,&memories);uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<memories.memoryTypeCount;++i)if(props.memoryTypeBits&(1u<<i)){type=i;break;}
 if(type==UINT32_MAX)return Error(h,"Live AHB import memory type unavailable");
 VkImportAndroidHardwareBufferInfoANDROID imported{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};imported.buffer=ahb;
 VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,&imported};dedicated.image=out.image;
 VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&dedicated};memory.allocationSize=props.allocationSize;memory.memoryTypeIndex=type;
 if(!VkCheck(h,api.AllocateMemory(api.unity.device,&memory,nullptr,&out.memory),"import live AHB allocation")||!VkCheck(h,api.BindImageMemory(api.unity.device,out.image,out.memory,0),"bind live AHB memory"))return false;
 AHardwareBuffer_acquire(ahb);out.ahb=ahb;return true;
}
void ReleaseActive(HV_InputLiveHandle h){
 if(h->active.hardware_buffer){h->source->ReleaseLive(h->active.slot,h->active.frame.generation,h->active.frame.sequence);h->active={};}
}
void Complete(HV_InputLiveHandle h){
 if(!h->inflight||h->unfenced)return;auto result=h->api.GetFenceStatus(h->api.unity.device,h->fence);
 if(result==VK_NOT_READY)return;
 if(result!=VK_SUCCESS){h->unfenced=true;Error(h,"Unity live copy fence failed; resources remain quarantined");return;}
 h->inflight=false;
 h->state.Finish(true);ReleaseActive(h);h->target_slot=-1;
}
void SubmitLiveLocked(HV_InputLiveHandle h,const hvinput::LiveDeviceLifetime::Lease& device){
 if(!h->active.hardware_buffer||h->inflight)return;
 if(!DeviceIsCurrent(h,device)||h->failed||h->state.Retiring()){
  h->state.Finish(true);ReleaseActive(h);return;
 }
 if(h->state.Closing()||h->owner->stop){h->state.Close();h->state.Finish(true);ReleaseActive(h);return;}
 auto& api=h->api;const auto& u=api.unity;auto source=h->imported[h->active.slot].image;
 if(!VkCheck(h,api.ResetFences(u.device,1,&h->fence),"reset live fence")||!VkCheck(h,api.ResetCommandBuffer(h->command,0),"reset live command")){h->state.Finish(true);ReleaseActive(h);return;}
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
 if(!VkCheck(h,api.BeginCommandBuffer(h->command,&begin),"begin live command")){h->state.Finish(true);ReleaseActive(h);return;}
 const auto& target=h->target_image;
 auto acquire=Barrier(source,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,0,VK_ACCESS_TRANSFER_READ_BIT,VK_QUEUE_FAMILY_EXTERNAL_KHR,u.queueFamilyIndex);
 api.CmdPipelineBarrier(h->command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&acquire);
 // The producer's live shader writes bottom-left encoded display RGB. Copy
 // compatible RGBA32 texels without a blit/conversion: an sRGB Unity target
 // in a Linear project must preserve codes rather than double-encode them.
 VkImageCopy region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.extent={h->targets.width,h->targets.height,1};
 api.CmdCopyImage(h->command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 auto returned=Barrier(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,0,u.queueFamilyIndex,VK_QUEUE_FAMILY_EXTERNAL_KHR);
 api.CmdPipelineBarrier(h->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&returned);
 if(!VkCheck(h,api.EndCommandBuffer(h->command),"end live command")){h->state.Finish(true);ReleaseActive(h);return;}
 VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&h->command;
 auto result=api.QueueSubmit(u.graphicsQueue,1,&submit,h->fence);
 if(result!=VK_SUCCESS){h->unfenced=true;Error(h,"Unity live submit failed; producer/target remain quarantined");return;}
 h->inflight=true;
}
void UNITY_INTERFACE_API SubmitLive(int,void* data){
 auto* h=static_cast<HV_InputLiveHandle>(data);std::lock_guard<std::mutex> lock(h->mutex);
 auto device=hvinput::InputLiveDeviceLifetime().Acquire();
 SubmitLiveLocked(h,device);
 h->state.SubmissionFinished();
}
void HV_INPUT_CALL RenderLive(int,void* data){
 auto* h=static_cast<HV_InputLiveHandle>(data);if(!h)return;
 IUnityGraphicsVulkan* queue_interface=nullptr;
 {
  std::lock_guard<std::mutex> lock(h->mutex);
  auto device=hvinput::InputLiveDeviceLifetime().Acquire();
  auto* unity=hvinput::InputUnityVulkan();
  if(h->owner->stop)h->state.Close();
  HV_InputLiveGeometryV1 current{sizeof(current),1};
  if(h->source->LiveGeometry(current)==HV_INPUT_OK)h->state.AdmitGeneration(current.generation);
  if(h->device_epoch&&DeviceIsCurrent(h,device))Complete(h);
  if(h->state.Closing()||h->state.Retiring()||h->failed||!h->targets.textures[0]){
   if(!h->inflight&&h->state.TargetsRetired())DestroyResources(h);
  }else if(h->config.enabled&&!h->inflight&&!h->unfenced&&h->state.CanBegin()){
   if(!unity||!unity->Instance||!unity->AccessTexture||!unity->AccessQueue){
    Error(h,"Unity Vulkan texture or queue access is unavailable");
    h->state.EventFinished();
    return;
   }
   HV_InputNativeFrameV1 frame{sizeof(frame),1};
   if(h->source->AcquireLive(h->after,frame)==HV_INPUT_OK){
    h->after=frame.frame.sequence;int slot=h->state.Begin(frame.frame.generation,frame.frame.sequence);
    if(slot<0){h->source->ReleaseLive(frame.slot,frame.frame.generation,frame.frame.sequence);}
    else {
     h->active=frame;h->target_slot=slot;
     if(frame.producer_sync_fd>=0){::close(frame.producer_sync_fd);h->active.producer_sync_fd=-1;
      Error(h,"Live publication lacks the required observed producer completion proof");h->state.Finish(true);ReleaseActive(h);}
     else if(frame.frame.width!=h->targets.width||frame.frame.height!=h->targets.height||frame.frame.row_origin!=0||!Initialize(h,*unity)||!Import(h,frame)){
      if(h->error.empty())Error(h,"Live geometry changed; retire targets and bind the actual extent");h->state.Finish(true);ReleaseActive(h);
     }else if(!unity->AccessTexture(h->targets.textures[slot],UnityVulkanWholeImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,kUnityVulkanResourceAccess_PipelineBarrier,&h->target_image)||
       !h->target_image.image||(h->target_image.format!=VK_FORMAT_R8G8B8A8_UNORM&&h->target_image.format!=VK_FORMAT_R8G8B8A8_SRGB)||h->target_image.extent.width!=h->targets.width||h->target_image.extent.height!=h->targets.height){
      Error(h,"Unity live target must be initialized RGBA8 UNORM/sRGB at the declared extent");h->state.Finish(true);ReleaseActive(h);
     }else{
      auto& out=h->frames[slot];out={};out.size=sizeof(out);out.version=1;out.frame=frame.frame;
      out.frame.color_space=frame.transfer==13?HV_INPUT_COLOR_SRGB:0;
      out.target_generation=h->targets.target_generation;out.slot=slot;
      out.source_width=frame.source_width;out.source_height=frame.source_height;out.crop_left=frame.crop_left;out.crop_top=frame.crop_top;out.crop_right=frame.crop_right;out.crop_bottom=frame.crop_bottom;out.rotation_degrees=frame.rotation_degrees;out.mirror_x=frame.mirror_x;
      h->state.QueueSubmission();queue_interface=unity;
     }
    }
   }
  }
 }
 // Unity may run this callback synchronously or defer it to submission.
 // The separate pending reference protects the handle in either case.
 if(queue_interface)queue_interface->AccessQueue(SubmitLive,kLiveEvent,h,true);
 {std::lock_guard<std::mutex> lock(h->mutex);h->state.EventFinished();}
}
int HV_INPUT_CALL Attach(HV_InputHandle source,const HV_InputLiveConfigV1* config,HV_InputLiveHandle* out){
 if(out)*out=nullptr;if(!source||!out||!ConfigValid(config)||source->stop||!source->native_output)return HV_INPUT_INVALID;
 uint32_t expected=0;if(!source->live_consumers.compare_exchange_strong(expected,1))return HV_INPUT_BUSY;
 try{auto h=std::make_unique<HV_InputLiveConsumerOpaque>();h->owner=source;h->source=source->native_output;h->config=*config;
  std::string error;int result=h->source->ConfigureLive(*config,error);if(result!=HV_INPUT_OK){h.reset();source->live_consumers=0;return result;}
  *out=h.release();return HV_INPUT_OK;
 }catch(...){source->live_consumers=0;return HV_INPUT_ERROR;}
}
int HV_INPUT_CALL Configure(HV_InputLiveHandle h,const HV_InputLiveConfigV1* config){
 if(!h||!ConfigValid(config))return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);
 if(h->state.Closing()||h->owner->stop)return HV_INPUT_BUSY;
 if(!h->resources_retired&&(config->max_width!=h->config.max_width||config->max_height!=h->config.max_height||
    config->rotation_degrees!=h->config.rotation_degrees||config->mirror_x!=h->config.mirror_x))return HV_INPUT_BUSY;
 std::string error;int result=h->source->ConfigureLive(*config,error);if(result==HV_INPUT_OK)h->config=*config;else if(!error.empty())h->error=error;return result;
}
int HV_INPUT_CALL Geometry(HV_InputLiveHandle h,HV_InputLiveGeometryV1* out){if(!h||!Valid(out))return HV_INPUT_INVALID;return h->source->LiveGeometry(*out);}
int HV_INPUT_CALL Bind(HV_InputLiveHandle h,const HV_InputLiveTargetsV1* targets){
 if(!h||!Valid(targets)||!targets->target_generation||!targets->width||!targets->height||!targets->textures[0]||!targets->textures[1]||!targets->textures[2]||
 targets->textures[0]==targets->textures[1]||targets->textures[0]==targets->textures[2]||targets->textures[1]==targets->textures[2])return HV_INPUT_INVALID;
 std::lock_guard<std::mutex> lock(h->mutex);
 auto device=hvinput::InputLiveDeviceLifetime().Acquire();
 if(!device.Epoch()){Error(h,"Unity Vulkan device is unavailable for live preview targets");return HV_INPUT_ERROR;}
 if(!h->resources_retired||!h->state.Bind(targets->target_generation))return HV_INPUT_BUSY;
 HV_InputLiveGeometryV1 actual{sizeof(actual),1};if(h->source->LiveGeometry(actual)!=HV_INPUT_OK||actual.width!=targets->width||actual.height!=targets->height){h->state.RetireTargets();return HV_INPUT_INVALID;}
 h->targets=*targets;h->device_epoch=device.Epoch();h->failed=false;h->error.clear();h->resources_retired=false;return HV_INPUT_OK;
}
int HV_INPUT_CALL Prepare(HV_InputLiveHandle h){if(!h)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);return h->state.QueueEvent()?HV_INPUT_OK:HV_INPUT_BUSY;}
HV_InputLiveRenderEvent HV_INPUT_CALL Event(){
 if(auto* unity=hvinput::InputUnityVulkan();unity&&unity->ConfigureEvent){
  UnityVulkanPluginEventConfig config{kUnityVulkanRenderPass_EnsureOutside,kUnityVulkanGraphicsQueueAccess_DontCare,
   kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission|kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState};
  unity->ConfigureEvent(kLiveEvent,&config);
 }
 return RenderLive;
}
int HV_INPUT_CALL Poll(HV_InputLiveHandle h,uint64_t after,HV_InputLiveFrameV1* out){
 if(!h||!Valid(out))return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);if(h->owner->stop)h->state.Close();
 HV_InputLiveGeometryV1 current{sizeof(current),1};if(h->source->LiveGeometry(current)==HV_INPUT_OK)h->state.AdmitGeneration(current.generation);
 auto device=hvinput::InputLiveDeviceLifetime().Acquire();
 if(h->device_epoch&&DeviceIsCurrent(h,device))Complete(h);
 if(h->failed)return HV_INPUT_ERROR;
 int slot=h->config.enabled?h->state.Acquire(after):-1;if(slot<0)return HV_INPUT_NO_FRAME;*out=h->frames[slot];return HV_INPUT_OK;
}
int HV_INPUT_CALL ReleaseSlot(HV_InputLiveHandle h,uint32_t slot,uint64_t target_generation,uint64_t seq){
 if(!h||slot>=3)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);
 const auto& frame=h->frames[slot];if(frame.target_generation!=target_generation||frame.frame.sequence!=seq)return HV_INPUT_INVALID;
 return h->state.Release(slot,frame.frame.generation,seq)?HV_INPUT_OK:HV_INPUT_INVALID;
}
int HV_INPUT_CALL Retire(HV_InputLiveHandle h){if(!h)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);h->state.RetireTargets();return HV_INPUT_OK;}
int HV_INPUT_CALL TargetsRetired(HV_InputLiveHandle h){if(!h)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);return h->state.TargetsRetired()&&h->resources_retired?HV_INPUT_OK:HV_INPUT_BUSY;}
int HV_INPUT_CALL Close(HV_InputLiveHandle h){if(!h)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);h->state.Close();h->source->DisableLive();return HV_INPUT_OK;}
int HV_INPUT_CALL TryRelease(HV_InputLiveHandle h){
 if(!h)return HV_INPUT_INVALID;{std::lock_guard<std::mutex> lock(h->mutex);if(!h->state.Retired()||!h->resources_retired||h->inflight||h->unfenced)return HV_INPUT_BUSY;}
 auto* owner=h->owner;delete h;--owner->live_consumers;return HV_INPUT_OK;
}
int HV_INPUT_CALL Progress(HV_InputLiveHandle h,HV_InputSourceProgressV1* out){return h?HV_Input_GetSourceProgressV1(h->owner,out):HV_INPUT_INVALID;}
int HV_INPUT_CALL LastError(HV_InputLiveHandle h,char* text,uint32_t size){
 if(!h||!text||!size)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(h->mutex);
 const auto error=h->error.empty()?h->source->LiveError():h->error;std::snprintf(text,size,"%s",error.c_str());return HV_INPUT_OK;
}
}
#else
namespace {
int HV_INPUT_CALL Attach(HV_InputHandle,const HV_InputLiveConfigV1*,HV_InputLiveHandle* out){if(out)*out=nullptr;return HV_INPUT_ERROR;}
int HV_INPUT_CALL Configure(HV_InputLiveHandle,const HV_InputLiveConfigV1*){return HV_INPUT_INVALID;}
int HV_INPUT_CALL Geometry(HV_InputLiveHandle,HV_InputLiveGeometryV1*){return HV_INPUT_INVALID;}
int HV_INPUT_CALL Bind(HV_InputLiveHandle,const HV_InputLiveTargetsV1*){return HV_INPUT_INVALID;}
int HV_INPUT_CALL Prepare(HV_InputLiveHandle){return HV_INPUT_INVALID;}
HV_InputLiveRenderEvent HV_INPUT_CALL Event(){return nullptr;}
int HV_INPUT_CALL Poll(HV_InputLiveHandle,uint64_t,HV_InputLiveFrameV1*){return HV_INPUT_INVALID;}
int HV_INPUT_CALL ReleaseSlot(HV_InputLiveHandle,uint32_t,uint64_t,uint64_t){return HV_INPUT_INVALID;}
int HV_INPUT_CALL Retire(HV_InputLiveHandle){return HV_INPUT_INVALID;}
int HV_INPUT_CALL TargetsRetired(HV_InputLiveHandle){return HV_INPUT_INVALID;}
int HV_INPUT_CALL Close(HV_InputLiveHandle){return HV_INPUT_INVALID;}
int HV_INPUT_CALL TryRelease(HV_InputLiveHandle){return HV_INPUT_INVALID;}
int HV_INPUT_CALL Progress(HV_InputLiveHandle,HV_InputSourceProgressV1*){return HV_INPUT_INVALID;}
int HV_INPUT_CALL LastError(HV_InputLiveHandle,char*,uint32_t){return HV_INPUT_INVALID;}
}
#endif
extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_QueryLiveApiV1(HV_InputLiveApiV1* out){
 if(!Valid(out))return HV_INPUT_INVALID;
 *out={sizeof(*out),1,Attach,Configure,Geometry,Bind,Prepare,Event,Poll,ReleaseSlot,Retire,TargetsRetired,Close,TryRelease,Progress,LastError};return HV_INPUT_OK;
}
