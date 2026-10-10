#include "input_vulkan_private.h"
#include "input_image_cache.h"
#include "input_buffer_registry.h"
#include "input_vulkan_proc_loader.h"
#include "input_target_owner.h"
#include "input_yuv_shader.h"
#include "humanvision_input.h"
#include "input_internal.h"
#include "android_input_vulkan.h"
#include "input_frame_ring.h"
#include "input_gpu_sync.h"
#include "input_diagnostic_sampling.h"
#include <android/log.h>
#include <media/NdkImageReader.h>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unistd.h>
namespace hvinput {
namespace {
constexpr int event_id=0x485649;
// Sparse local monotonic timestamps describe input queue/fence polling only;
// they are not sensor capture latency or a GPU execution timestamp.
int64_t diagnostic_submitted_us = 0;
#define INPUT_VK_FUNCTIONS(X) \
 X(GetAndroidHardwareBufferPropertiesANDROID) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFormatProperties) \
 X(CreateImage) X(DestroyImage) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
 X(CreateSamplerYcbcrConversion) X(DestroySamplerYcbcrConversion) X(CreateSampler) X(DestroySampler) X(CreateImageView) X(DestroyImageView) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdCopyImage) X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) \
 X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(ResetFences) X(CreateSemaphore) X(DestroySemaphore) X(ImportSemaphoreFdKHR) X(GetSemaphoreFdKHR) X(QueueSubmit)
struct Api {
#define DECLARE(name) PFN_vk##name name=nullptr;
 INPUT_VK_FUNCTIONS(DECLARE)
#undef DECLARE
 UnityVulkanInstance unity{};bool Load() {
  auto* u=InputUnityVulkan();if(!u)return false;unity=u->Instance();if(!unity.device||!InputForeignEnabled())return false;
  auto gd=reinterpret_cast<PFN_vkGetDeviceProcAddr>(unity.getInstanceProcAddr(unity.instance,"vkGetDeviceProcAddr"));
  if(!gd)return false;
#define LOAD(name) name=reinterpret_cast<PFN_vk##name>(LoadInputVulkanProc("vk" #name,[&](const char* n){return gd(unity.device,n);},[&](const char* n){return unity.getInstanceProcAddr(unity.instance,n);}));if(!name)return false;
 INPUT_VK_FUNCTIONS(LOAD)
#undef LOAD
  return true;
 }
} api;
InputBufferRegistry buffer_registry;
struct Imported {
 const void* reader=nullptr;
 VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;
 VkSamplerYcbcrConversion conversion=VK_NULL_HANDLE;VkSampler sampler=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;VkDescriptorSet descriptor=VK_NULL_HANDLE;
 VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
 bool reference=false;
};
struct Counters {uint64_t creates=0,destroys=0,submits=0,completes=0,positive_waits=0,already_complete=0,exports=0,ownership_acquires=0,ownership_returns=0,queue_callbacks=0,target_views_created=0,target_views_destroyed=0,source_views_created=0,source_views_destroyed=0,pipelines_created=0,pipelines_destroyed=0,descriptor_pools_created=0,descriptor_pools_destroyed=0,cache_ref_acquires=0,cache_ref_releases=0,errors=0;} counters;
uint64_t completed_token=0;
void ProductionError(const char*,VkResult);
void Error(const char* stage,VkResult code=VK_ERROR_UNKNOWN){++counters.errors;ProductionError(stage,code);__android_log_print(ANDROID_LOG_ERROR,"HVInputGate","color_result=FAIL stage=%s code=%d",stage,code);}
void DestroyResource(CacheEntry& e){
 auto* r=static_cast<Imported*>(e.resource);if(!r)return;auto d=api.unity.device;
 // Registry mutex is released before any Vulkan/AHB destruction.
 buffer_registry.Unregister(r->reader,e.buffer);
 if(r->pipeline){api.DestroyPipeline(d,r->pipeline,nullptr);++counters.pipelines_destroyed;}
 if(r->layout)api.DestroyPipelineLayout(d,r->layout,nullptr);
 if(r->descriptor_pool){api.DestroyDescriptorPool(d,r->descriptor_pool,nullptr);++counters.descriptor_pools_destroyed;}
 if(r->set_layout)api.DestroyDescriptorSetLayout(d,r->set_layout,nullptr);
 if(r->view){api.DestroyImageView(d,r->view,nullptr);++counters.source_views_destroyed;}
 if(r->sampler)api.DestroySampler(d,r->sampler,nullptr);
 if(r->conversion)api.DestroySamplerYcbcrConversion(d,r->conversion,nullptr);
 if(r->image)api.DestroyImage(d,r->image,nullptr);
 if(r->memory)api.FreeMemory(d,r->memory,nullptr);
 if(r->reference){AHardwareBuffer_release(static_cast<AHardwareBuffer*>(e.buffer));++counters.cache_ref_releases;}
 delete r;e.resource=nullptr;
}
struct Importer:ImageImporter {
 const void* source_reader=nullptr;
 bool Complete(uint64_t token)override{return token<=completed_token;}
 void Destroy(CacheEntry& e)override{DestroyResource(e);++counters.destroys;}
 bool Create(CacheEntry& e)override {
  InputBufferReservation reservation(buffer_registry,source_reader,e.buffer);
  if(!reservation){Error("known AHB registry domain or identity");return false;}
  auto resource=std::make_unique<Imported>();resource->reader=source_reader;e.resource=resource.get();auto d=api.unity.device;
  const auto check=[&](VkResult v,const char* stage){if(v==VK_SUCCESS)return true;Error(stage,v);return false;};
  bool success=false;
  const auto work=[&]()->bool {
   const auto& c=e.contract;
   VkExternalFormatANDROID external{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,nullptr,c.external_format};
   VkExternalMemoryImageCreateInfo external_image{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,&external,VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
   VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,&external_image};
   create.imageType=VK_IMAGE_TYPE_2D;create.format=static_cast<VkFormat>(c.format);create.extent={c.width,c.height,1};create.mipLevels=1;create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=VK_IMAGE_USAGE_SAMPLED_BIT;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;create.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
   if(!check(api.CreateImage(d,&create,nullptr,&resource->image),"create readonly AHB image"))return false;
   VkAndroidHardwareBufferPropertiesANDROID properties{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
   if(!check(api.GetAndroidHardwareBufferPropertiesANDROID(d,static_cast<AHardwareBuffer*>(e.buffer),&properties),"query import allocation"))return false;
   VkPhysicalDeviceMemoryProperties memory{};api.GetPhysicalDeviceMemoryProperties(api.unity.physicalDevice,&memory);
   uint32_t index=UINT32_MAX;for(uint32_t i=0;i<memory.memoryTypeCount;++i)if(properties.memoryTypeBits&(1u<<i)){index=i;break;}
   if(index==UINT32_MAX){Error("AHB import memory type");return false;}
   VkImportAndroidHardwareBufferInfoANDROID import{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,nullptr,static_cast<AHardwareBuffer*>(e.buffer)};
   VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,&import,resource->image,VK_NULL_HANDLE};
   VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&dedicated,properties.allocationSize,index};
   if(!check(api.AllocateMemory(d,&allocation,nullptr,&resource->memory),"import AHB memory")||!check(api.BindImageMemory(d,resource->image,resource->memory,0),"bind imported AHB"))return false;
   VkSamplerYcbcrConversionCreateInfo conversion{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,&external};
   conversion.format=static_cast<VkFormat>(c.format);conversion.ycbcrModel=static_cast<VkSamplerYcbcrModelConversion>(c.model);conversion.ycbcrRange=static_cast<VkSamplerYcbcrRange>(c.range);
   conversion.components={static_cast<VkComponentSwizzle>(c.components[0]),static_cast<VkComponentSwizzle>(c.components[1]),static_cast<VkComponentSwizzle>(c.components[2]),static_cast<VkComponentSwizzle>(c.components[3])};conversion.xChromaOffset=static_cast<VkChromaLocation>(c.x_chroma);conversion.yChromaOffset=static_cast<VkChromaLocation>(c.y_chroma);
   conversion.chromaFilter=(c.features&VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
   if(!check(api.CreateSamplerYcbcrConversion(d,&conversion,nullptr,&resource->conversion),"create actual YCbCr conversion"))return false;
   VkSamplerYcbcrConversionInfo converted{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,nullptr,resource->conversion};
   VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,&converted};sampler.magFilter=sampler.minFilter=conversion.chromaFilter;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.maxLod=0;
   if(!check(api.CreateSampler(d,&sampler,nullptr,&resource->sampler),"create immutable converted sampler"))return false;
   VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,&converted};view.image=resource->image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=static_cast<VkFormat>(c.format);view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   if(!check(api.CreateImageView(d,&view,nullptr,&resource->view),"create converted source view"))return false;++counters.source_views_created;
   VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,&resource->sampler},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
   VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,nullptr,0,2,bindings};
   if(!check(api.CreateDescriptorSetLayout(d,&set,nullptr,&resource->set_layout),"create conversion descriptor layout"))return false;
   // External opaque formats have no valid VkFormat query. The pool is
   // deliberately overprovisioned; successful allocation qualifies this device.
   // This is resource capacity, not an assertion about the external plane count.
   constexpr uint32_t descriptor_budget=16;
   VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,descriptor_budget},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
   VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,nullptr,0,1,2,sizes};
   if(!check(api.CreateDescriptorPool(d,&pool,nullptr,&resource->descriptor_pool),"create descriptor pool"))return false;++counters.descriptor_pools_created;
   VkDescriptorSetAllocateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,nullptr,resource->descriptor_pool,1,&resource->set_layout};
   if(!check(api.AllocateDescriptorSets(d,&descriptors,&resource->descriptor),"allocate converted descriptors"))return false;
   VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,32};VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,nullptr,0,1,&resource->set_layout,1,&push};
   if(!check(api.CreatePipelineLayout(d,&layout,nullptr,&resource->layout),"create conversion pipeline layout"))return false;
   VkShaderModule module=VK_NULL_HANDLE;VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,nullptr,0,sizeof(input_yuv_shader),input_yuv_shader};
   if(!check(api.CreateShaderModule(d,&shader,nullptr,&module),"create qualified input shader"))return false;
   VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};pipeline.layout=resource->layout;
   auto result=api.CreateComputePipelines(d,api.unity.pipelineCache,1,&pipeline,nullptr,&resource->pipeline);api.DestroyShaderModule(d,module,nullptr);
   if(!check(result,"create cached YCbCr pipeline"))return false;++counters.pipelines_created;
   AHardwareBuffer_acquire(static_cast<AHardwareBuffer*>(e.buffer));resource->reference=true;++counters.cache_ref_acquires;
   __android_log_print(ANDROID_LOG_INFO,"HVInputGate","ahb_import_created buffer=%p reader=%p external_format=%llu width=%u height=%u model=%u range=%u descriptors=%u",e.buffer,source_reader,(unsigned long long)c.external_format,c.width,c.height,c.model,c.range,descriptor_budget);
   return true;
  };
  success=work();resource.release();if(!success){DestroyResource(e);return false;}if(!reservation.Commit()){Error("known AHB registry reservation lost");DestroyResource(e);return false;}++counters.creates;return true;
 }
} importer;
InputImageCache cache(importer);
std::mutex mutex;std::condition_variable retired;
Counters session_baseline{};BufferRegistryStats registry_baseline{};bool terminal_reported=false;
AndroidDecodedImage image_storage, active_storage;
InputFrameRing frame_ring;
bool production=false;int output_slot=-1;
std::array<void*,3> output_textures{};
std::array<VkImageView,3> output_views{};
std::array<VkImage,3> output_images{};
HV_InputGpuFrame published_frame{};
uint64_t decoded_drops=0,old_generation_rejections=0;
AndroidDecodedImage* pending=nullptr;AndroidDecodedImage* active=nullptr;
bool enabled=false,initialized=false,closing=false,paused=false,inflight=false;
void ReturnImage(AndroidDecodedImage*& p){if(p){if(production&&!inflight)frame_ring.Cancel(output_slot);p->Reset();p=nullptr;retired.notify_all();}}

HV_InputHandle owner=nullptr;void* unity_texture=nullptr;uint32_t target_width=0,target_height=0;uint64_t target_generation=0;
void ProductionErrorText(const char* stage,const char* detail){
 if(production&&owner)owner->SetGpuError(stage,detail);
}
void ProductionError(const char* stage,VkResult code){char detail[64]{};std::snprintf(detail,sizeof(detail),"Vulkan result %d",int(code));ProductionErrorText(stage,detail);}
int rotation=0;bool mirror=false;uint64_t token=0;
VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
VkSemaphore wait_semaphore=VK_NULL_HANDLE,release_semaphore=VK_NULL_HANDLE;
VkImageView target_view=VK_NULL_HANDLE;VkImage last_target=VK_NULL_HANDLE;
UnityVulkanImage target{};CacheEntry* source_entry=nullptr;bool target_dirty=false,retire_target=false;int fail_target_view=0;
void LogContract(const char* label,int slot,const ImageContract& c){
 __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","ahb_cache_contract label=%s slot=%d external_format=%llu format=%u features=%u model=%u range=%u x_chroma=%u y_chroma=%u width=%u height=%u components=%u,%u,%u,%u",label,slot,(unsigned long long)c.external_format,c.format,c.features,c.model,c.range,c.x_chroma,c.y_chroma,c.width,c.height,c.components[0],c.components[1],c.components[2],c.components[3]);
}
void ReportCacheFailure(){
 const auto& f=cache.LastFailure();const auto removal_state=buffer_registry.Inspect();const uint32_t notifications=removal_state.pending;
 const int fence_status=fence?int(api.GetFenceStatus(api.unity.device,fence)):int(VK_NOT_READY);
 __android_log_print(ANDROID_LOG_ERROR,"HVInputGate","ahb_cache_failure reason=%s buffer=%p reader=%p generation=%llu matched_slot=%u generation_changed=%d contract_diff=0x%x live=%u removed=%u complete=%u pinned=%u token=%llu completed_token=%llu inflight=%d active=%p pending=%p output_slot=%d source_entry=%p fence=%p fence_status=%d removed_notifications=%u snapshot_before_collect=1",CacheFailureName(f.reason),f.buffer,pending?pending->reader:nullptr,(unsigned long long)f.generation,f.matched_slot,f.generation_changed,f.contract_diff,f.live,f.removed,f.complete,f.pinned,(unsigned long long)token,(unsigned long long)completed_token,inflight,active,pending,output_slot,source_entry,fence,fence_status,notifications);
 LogContract("requested",-1,f.contract);
 for(uint32_t i=0;i<f.slots.size();++i){const auto& s=f.slots[i];const auto& e=s.entry;__android_log_print(ANDROID_LOG_ERROR,"HVInputGate","ahb_cache_slot index=%u buffer=%p reader=%p generation=%llu live=%d removed=%d complete=%d last_fence=%llu resource=%p",i,e.buffer,e.live&&e.resource?static_cast<Imported*>(e.resource)->reader:nullptr,(unsigned long long)e.generation,e.live,e.removed,s.complete,(unsigned long long)e.last_fence,e.resource);if(e.buffer)LogContract("cached",int(i),e.contract);}
 for(int i=0;i<3;++i){const auto& s=frame_ring.Slot(i);__android_log_print(ANDROID_LOG_ERROR,"HVInputGate","ahb_cache_output_slot index=%d state=%d generation=%llu sequence=%llu fence=%llu observed=%d rgba_texture=%p rgba_image=%p",i,int(s.state),(unsigned long long)s.generation,(unsigned long long)s.sequence,(unsigned long long)s.fence,s.observed,output_textures[i],output_images[i]);}
 // Create already reports its actual first Vulkan stage/result. Never replace it.
 if(f.reason!=CacheFailure::CreateFailed){++counters.errors;char detail[160]{};std::snprintf(detail,sizeof(detail),"%s (live=%u removed=%u complete=%u pinned=%u)",CacheFailureName(f.reason),f.live,f.removed,f.complete,f.pinned);ProductionErrorText("AHB import cache",detail);}
}
bool InitializeResources(){
 if(initialized)return true;if(!api.Load()){Error("successful enabled input Vulkan capabilities");return false;}
 auto d=api.unity.device;VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,nullptr,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,api.unity.queueFamilyIndex};
 auto failed=[&](){if(release_semaphore)api.DestroySemaphore(d,release_semaphore,nullptr);if(wait_semaphore)api.DestroySemaphore(d,wait_semaphore,nullptr);if(fence)api.DestroyFence(d,fence,nullptr);if(command_pool)api.DestroyCommandPool(d,command_pool,nullptr);release_semaphore=wait_semaphore=VK_NULL_HANDLE;fence=VK_NULL_HANDLE;command_pool=VK_NULL_HANDLE;command=VK_NULL_HANDLE;return false;};
 if(api.CreateCommandPool(d,&pool,nullptr,&command_pool)!=VK_SUCCESS)return false;
 VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,nullptr,command_pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
 if(api.AllocateCommandBuffers(d,&commands,&command)!=VK_SUCCESS)return failed();
 VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(api.CreateFence(d,&f,nullptr,&fence)!=VK_SUCCESS)return failed();
 VkExportSemaphoreCreateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,nullptr,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
 VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};if(api.CreateSemaphore(d,&sem,nullptr,&wait_semaphore)!=VK_SUCCESS)return failed();
 sem.pNext=&export_info;if(api.CreateSemaphore(d,&sem,nullptr,&release_semaphore)!=VK_SUCCESS)return failed();
 initialized=true;return true;
}
// Caller holds mutex. Query the real fence without waiting or submitting work.
// This part is also safe at a main-thread metadata read: no Unity API calls or
// Vulkan resource destruction. A not-ready/error fence keeps its image leased.
void PollConversionCompletion(){
 if(inflight){auto status=api.GetFenceStatus(api.unity.device,fence);if(status==VK_NOT_READY)return;if(status!=VK_SUCCESS){Error("poll conversion GPU fence",status);return;}
  completed_token=token;++counters.completes;inflight=false;
  if(production){
   if(!closing&&frame_ring.Complete(output_slot,completed_token)){
    auto& f=published_frame.frame;f={};f.size=sizeof(f);f.version=1;f.sequence=token;f.generation=active->generation;f.width=target_width;f.height=target_height;
    f.received_timestamp_us=active->received_us;f.decoded_timestamp_us=active->decoded_us;f.presentation_timestamp_us=active->pts_us;
    f.clock_id=ClockId();f.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;f.timestamp_kind=HV_INPUT_TIME_LOCAL_DECODE;f.pts_valid=1;f.row_origin=0;f.color_space=0;f.decode_mode=2;
    published_frame.slot=output_slot;
    if(ShouldLogInputFrame(token,!production))__android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_frame_published sequence=%llu generation=%llu slot=%d",(unsigned long long)token,(unsigned long long)active->generation,output_slot);
   }else{++old_generation_rejections;}
   frame_ring.Collect(completed_token);
  }
  if(ShouldLogInputFrame(token,!production))__android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_completed sequence=%llu generation=%llu pts_us=%lld received_us=%lld decoded_us=%lld submitted_us=%lld converted_us=%lld cpu_image_readbacks=0 target_width=%u target_height=%u applied_rotation=%d applied_mirror=%d source_matrix=%u source_range=%u transfer=%u primaries=%u color_space=%u",(unsigned long long)token,(unsigned long long)active->generation,(long long)active->pts_us,(long long)active->received_us,(long long)active->decoded_us,(long long)diagnostic_submitted_us,(long long)NowUs(),target_width,target_height,rotation,mirror,active->matrix,active->color_range,active->transfer,active->primaries,0u);
  ReturnImage(active);retired.notify_all();
 }
}
void Poll(){
 PollConversionCompletion();
 if(inflight)return;
 BufferIdentity removed;while(buffer_registry.TakeRemoved(removed))cache.Remove(removed.buffer);
 if(closing)cache.RemoveAll();cache.Collect();if(production)frame_ring.Collect(completed_token);
}
struct SyncFdBackend:InputSyncFdBackend {
 int Duplicate(int fd)override{return dup(fd);}
 bool Import(int fd)override{VkImportSemaphoreFdInfoKHR info{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,nullptr,wait_semaphore,VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,fd};return api.ImportSemaphoreFdKHR(api.unity.device,&info)==VK_SUCCESS;}
 int Export()override{int fd=-1;VkSemaphoreGetFdInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,nullptr,release_semaphore,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};return api.GetSemaphoreFdKHR(api.unity.device,&info,&fd)==VK_SUCCESS?fd:-2;}
 void Close(int fd)override{close(fd);}
} sync_fd_backend;
InputGpuSync gpu_sync(sync_fd_backend);
void UNITY_INTERFACE_API SubmitOnUnityQueue(int,void*){
 std::lock_guard<std::mutex> lock(mutex);++counters.queue_callbacks;
 if(!active||!source_entry||inflight){Error("queue callback invalid pending conversion");return;}
 auto d=api.unity.device;auto* r=static_cast<Imported*>(source_entry->resource);
 bool waited=false;
 if(!gpu_sync.WaitAcquireFdAndOwn(active->acquire_fd,waited)){Error("import actual acquire sync fd");ReturnImage(active);return;}
 if(waited)++counters.positive_waits;else ++counters.already_complete;
 if(api.ResetCommandBuffer(command,0)!=VK_SUCCESS||api.ResetFences(d,1,&fence)!=VK_SUCCESS){Error("reset reusable GPU objects");ReturnImage(active);return;}
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,nullptr,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};if(api.BeginCommandBuffer(command,&begin)!=VK_SUCCESS){Error("begin color command");ReturnImage(active);return;}
 VkImageMemoryBarrier acquire{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};acquire.srcAccessMask=0;acquire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;acquire.oldLayout=VK_IMAGE_LAYOUT_GENERAL;acquire.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;acquire.srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;acquire.dstQueueFamilyIndex=api.unity.queueFamilyIndex;acquire.image=r->image;acquire.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 api.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&acquire);
 VkDescriptorImageInfo images[]={{r->sampler,r->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,target_view,VK_IMAGE_LAYOUT_GENERAL}};
 VkWriteDescriptorSet writes[2]{};for(int i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=r->descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&images[i];}api.UpdateDescriptorSets(d,2,writes,0,nullptr);
 struct Push {float crop[4];int32_t info[4];} push{{float(active->crop_left)/source_entry->contract.width,float(active->crop_top)/source_entry->contract.height,float(active->crop_right)/source_entry->contract.width,float(active->crop_bottom)/source_entry->contract.height},{int32_t(target_width),int32_t(target_height),rotation,int32_t(mirror)}};
 api.CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,r->pipeline);api.CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,r->layout,0,1,&r->descriptor,0,nullptr);api.CmdPushConstants(command,r->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);api.CmdDispatch(command,(target_width+7)/8,(target_height+7)/8,1);
 auto release=acquire;release.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;release.dstAccessMask=0;release.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;release.newLayout=VK_IMAGE_LAYOUT_GENERAL;release.srcQueueFamilyIndex=api.unity.queueFamilyIndex;release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
 VkImageMemoryBarrier output{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};output.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;output.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;output.oldLayout=output.newLayout=VK_IMAGE_LAYOUT_GENERAL;output.srcQueueFamilyIndex=output.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;output.image=target.image;output.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkImageMemoryBarrier barriers[]={release,output};
 api.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
 if(api.EndCommandBuffer(command)!=VK_SUCCESS){Error("end color command");ReturnImage(active);return;}
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=waited?1:0;submit.pWaitSemaphores=waited?&wait_semaphore:nullptr;submit.pWaitDstStageMask=waited?&stage:nullptr;submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&release_semaphore;
 auto result=api.QueueSubmit(api.unity.graphicsQueue,1,&submit,fence);if(result!=VK_SUCCESS){Error("serialized Unity queue color submit",result);ReturnImage(active);return;}
 active->gpu_submitted=true;inflight=true;++token;++counters.submits;++counters.ownership_acquires;++counters.ownership_returns;cache.Used(*source_entry,token);if(production)frame_ring.Queue(output_slot,token);
 if(!gpu_sync.SignalReleaseFdAndReturnOwnership(active->release_fd)){Error("export actual GPU release sync fd");return;}active->CountReleaseFd();++counters.exports;
 if(ShouldLogInputFrame(token,!production)){
  diagnostic_submitted_us=NowUs();
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_submitted sequence=%llu buffer=%p acquire_wait=%s release_fd=%d ownership=FOREIGN_EXT_to_Unity_to_FOREIGN_EXT serialized_access_queue=1",(unsigned long long)token,active->buffer,waited?"imported_sync_fd":"actual_minus_one_already_complete",active->release_fd);
 }
}
}
bool BeginInputGpu(HV_InputHandle h){std::lock_guard<std::mutex> lock(mutex);if(owner||active||pending||inflight)return false;owner=h;session_baseline=counters;registry_baseline=buffer_registry.Inspect();terminal_reported=false;production=true;enabled=true;closing=paused=false;output_slot=-1;published_frame={};frame_ring.Begin(1);return true;}
void CloseInputGpu(HV_InputHandle h){std::lock_guard<std::mutex> lock(mutex);if(!production||owner!=h)return;closing=paused=true;frame_ring.Close();published_frame={};ReturnImage(pending);retire_target=true;}
bool InputGpuRetired(HV_InputHandle h){
 std::lock_guard<std::mutex> lock(mutex);const bool done=!production||owner!=h||(!active&&!pending&&!inflight&&!retire_target&&cache.Live()==0);
 // Report once only after the decoder has deleted its reader/quiesced callbacks
 // and real GPU retirement released cache objects. Never from a removal callback.
 if(done&&production&&owner==h&&h->stop&&h->worker_done&&!terminal_reported){
  const auto s=buffer_registry.Inspect();
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","input_cache_retired owner=%p supported_object_limit=%u creates=%llu destroys=%llu cache_ref_acquires=%llu cache_ref_releases=%llu cache_owned_refs=%llu cache_live=%u registry_live=%u registry_reserved=%u registry_pending=%u known_notifications=%llu duplicate_notifications=%llu unknown_notifications=%llu submits=%llu completes=%llu source_views_created=%llu source_views_destroyed=%llu pipelines_created=%llu pipelines_destroyed=%llu descriptor_pools_created=%llu descriptor_pools_destroyed=%llu target_views_created=%llu target_views_destroyed=%llu",
   h,kInputDecoderBufferCapacity,(unsigned long long)(counters.creates-session_baseline.creates),(unsigned long long)(counters.destroys-session_baseline.destroys),(unsigned long long)(counters.cache_ref_acquires-session_baseline.cache_ref_acquires),(unsigned long long)(counters.cache_ref_releases-session_baseline.cache_ref_releases),(unsigned long long)(counters.cache_ref_acquires-counters.cache_ref_releases),cache.Live(),s.live,s.reserved,s.pending,
   (unsigned long long)(s.known_notifications-registry_baseline.known_notifications),(unsigned long long)(s.duplicate_notifications-registry_baseline.duplicate_notifications),(unsigned long long)(s.unknown_notifications-registry_baseline.unknown_notifications),(unsigned long long)(counters.submits-session_baseline.submits),(unsigned long long)(counters.completes-session_baseline.completes),
   (unsigned long long)(counters.source_views_created-session_baseline.source_views_created),(unsigned long long)(counters.source_views_destroyed-session_baseline.source_views_destroyed),(unsigned long long)(counters.pipelines_created-session_baseline.pipelines_created),(unsigned long long)(counters.pipelines_destroyed-session_baseline.pipelines_destroyed),(unsigned long long)(counters.descriptor_pools_created-session_baseline.descriptor_pools_created),(unsigned long long)(counters.descriptor_pools_destroyed-session_baseline.descriptor_pools_destroyed),(unsigned long long)(counters.target_views_created-session_baseline.target_views_created),(unsigned long long)(counters.target_views_destroyed-session_baseline.target_views_destroyed));
  terminal_reported=true;
 }
 return done;
}
void DetachInputGpu(HV_InputHandle h){std::lock_guard<std::mutex> lock(mutex);if(owner==h&&production){owner=nullptr;production=false;enabled=false;frame_ring.Close();}}
void PrepareInputGpuGeneration(Session* h){std::lock_guard<std::mutex> lock(mutex);if(production&&owner==h&&!h->stop){closing=paused=false;published_frame={};}}
int PollInputGpuMetadata(HV_InputHandle h,uint64_t after,HV_InputFrameInfo* f){std::lock_guard<std::mutex> lock(mutex);if(!h||h!=owner||!f)return HV_INPUT_INVALID;if(closing||published_frame.frame.sequence<=after||!published_frame.frame.sequence)return HV_INPUT_NO_FRAME;*f=published_frame.frame;return 0;}
bool InputGpuProduction(Session* h){std::lock_guard<std::mutex> lock(mutex);return production&&owner==h;}
bool ColorProbeEnabled(){std::lock_guard<std::mutex> lock(mutex);return enabled;}
bool QueueColorImage(AndroidDecodedImage& image){std::lock_guard<std::mutex> lock(mutex);if(!enabled||closing||paused)return false;if(production){if(pending){ReturnImage(pending);++decoded_drops;}image_storage.TakeFrom(image);pending=&image_storage;return true;}if(pending||active)return false;image_storage.TakeFrom(image);pending=&image_storage;return true;}
void NotifyRemovedBuffer(AImageReader* reader,AHardwareBuffer* buffer){buffer_registry.Notify(reader,buffer);}
void DrainColorImagesBeforeReaderClose(){std::unique_lock<std::mutex> lock(mutex);if(!enabled)return;closing=true;paused=true;if(production){frame_ring.Close();published_frame={};}ReturnImage(pending);retired.wait(lock,[]{return !active;});}
void ConfigureInputCopyProbeEvent();
void ConfigureInputRenderEvent(){ConfigureInputCopyProbeEvent();auto* u=InputUnityVulkan();if(!u)return;UnityVulkanPluginEventConfig config{kUnityVulkanRenderPass_EnsureOutside,kUnityVulkanGraphicsQueueAccess_DontCare,kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission|kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState};u->ConfigureEvent(event_id,&config);}
void InputCopyProbeEvent();
void InputRenderEvent(int id){
 if(id==event_id+1){InputCopyProbeEvent();return;}
 if(id!=event_id)return;bool submit=false;
 {std::lock_guard<std::mutex> lock(mutex);if(!enabled)return;if(initialized)Poll();if(retire_target&&!inflight&&!active){if(production){for(auto& view:output_views)if(view){api.DestroyImageView(api.unity.device,view,nullptr);view=VK_NULL_HANDLE;++counters.target_views_destroyed;}output_images={};output_textures={};target_view=VK_NULL_HANDLE;}if(target_view){api.DestroyImageView(api.unity.device,target_view,nullptr);target_view=VK_NULL_HANDLE;++counters.target_views_destroyed;}last_target=VK_NULL_HANDLE;unity_texture=nullptr;retire_target=false;}if(closing||paused||inflight||active||!pending)return;
 if(production){
  frame_ring.Collect(completed_token);
  if(!output_textures[0])return;
  output_slot=frame_ring.Acquire(pending->generation,token+1);
  if(output_slot<0){++decoded_drops;ReturnImage(pending);return;}
  unity_texture=output_textures[output_slot];target_view=output_views[output_slot];last_target=output_images[output_slot];target_dirty=!target_view;
 }
 if(!unity_texture)return;
 if(!InitializeResources()){Error("initialize input GPU resources");ReturnImage(pending);return;}
 InputGpuCapabilities c{};if(!ProbeDecodedBuffer(pending->buffer,api.unity.physicalDevice,&c)||AdmitDecodedBuffer(c)!=CapabilityFailure::None){Error("actual decoded import capabilities");ReturnImage(pending);return;}
 if(pending->generation!=target_generation){if(production){frame_ring.Cancel(output_slot);++old_generation_rejections;}else Error("target/source generation mismatch");ReturnImage(pending);return;}
 auto* u=InputUnityVulkan();
 if(!u->AccessTexture(unity_texture,UnityVulkanWholeImage,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,kUnityVulkanResourceAccess_PipelineBarrier,&target)){Error("Unity AccessTexture actual target");ReturnImage(pending);return;}
 VkFormatProperties features{};api.GetPhysicalDeviceFormatProperties(api.unity.physicalDevice,target.format,&features);
 if(target.format!=VK_FORMAT_R8G8B8A8_UNORM||!(target.usage&VK_IMAGE_USAGE_STORAGE_BIT)||!(features.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)||target.extent.width!=target_width||target.extent.height!=target_height){Error("actual Unity target lacks qualified rgba8 storage contract");ReturnImage(pending);return;}
 if(target_dirty||target.image!=last_target){
 VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=target.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=target.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 VkResult result=VK_SUCCESS;
 if(production){output_views[output_slot]=VK_NULL_HANDLE;output_images[output_slot]=VK_NULL_HANDLE;}
 const bool created=ReplaceTargetView(target_view,
  [&](VkImageView old){api.DestroyImageView(api.unity.device,old,nullptr);++counters.target_views_destroyed;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","target_view_replacement_old_retired=true");},
  [&](VkImageView& candidate){
   if(fail_target_view){--fail_target_view;result=VK_ERROR_OUT_OF_HOST_MEMORY;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","target_view_fault_injected=true retired_handle_null=%d pending_lease_returned_on_failure=1",target_view==VK_NULL_HANDLE);return false;}
   result=api.CreateImageView(api.unity.device,&view,nullptr,&candidate);return result==VK_SUCCESS;
  });
 if(!created){Error("create actual Unity storage view",result);last_target=VK_NULL_HANDLE;ReturnImage(pending);return;}
last_target=target.image;target_dirty=false;++counters.target_views_created;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","actual_unity_target image=%p format=%u usage=%u storage_features=%u width=%u height=%u",target.image,target.format,target.usage,features.optimalTilingFeatures,target_width,target_height);}
 if(production){output_views[output_slot]=target_view;output_images[output_slot]=last_target;}
 ImageContract contract{};contract.external_format=c.external_format;contract.format=c.format;contract.features=c.format_features;contract.model=c.model;contract.range=c.range;contract.x_chroma=c.x_chroma;contract.y_chroma=c.y_chroma;std::memcpy(contract.components,c.components,sizeof(c.components));
 AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(pending->buffer,&desc);contract.width=desc.width;contract.height=desc.height;
 if(pending->matrix==1)contract.model=VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;else if(pending->matrix==2)contract.model=VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
 if(pending->color_range==1)contract.range=VK_SAMPLER_YCBCR_RANGE_ITU_FULL;else if(pending->color_range==2)contract.range=VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
 importer.source_reader=pending->reader;source_entry=cache.Acquire(pending->buffer,pending->generation,contract);if(!source_entry){ReportCacheFailure();ReturnImage(pending);return;}
 if(production){active_storage.TakeFrom(*pending);pending=nullptr;active=&active_storage;}else{active=pending;pending=nullptr;}submit=true;
 }
 // Flush Unity's AccessTexture layout transition first; only this callback may submit.
 if(submit)InputUnityVulkan()->AccessQueue(SubmitOnUnityQueue,event_id,nullptr,true);
}
void ShutdownInputColor(){std::lock_guard<std::mutex> lock(mutex);if(!initialized)return;Poll();if(inflight||active){Error("shutdown before GPU retirement");return;}ReturnImage(pending);cache.RemoveAll();cache.Collect();auto d=api.unity.device;if(target_view)api.DestroyImageView(d,target_view,nullptr);if(command_pool)api.DestroyCommandPool(d,command_pool,nullptr);if(fence)api.DestroyFence(d,fence,nullptr);if(wait_semaphore)api.DestroySemaphore(d,wait_semaphore,nullptr);if(release_semaphore)api.DestroySemaphore(d,release_semaphore,nullptr);initialized=false;}
}
extern "C" {
__attribute__((visibility("default"))) void HV_Input_EnableColorProbe(){std::lock_guard<std::mutex> lock(hvinput::mutex);hvinput::enabled=true;hvinput::closing=false;hvinput::paused=false;}
__attribute__((visibility("default"))) int HV_Input_BindUnityTarget(HV_InputHandle h,void* texture,uint32_t width,uint32_t height,uint64_t generation){std::lock_guard<std::mutex> lock(hvinput::mutex);if(!h||!texture||!width||!height||!generation)return HV_INPUT_INVALID;if(hvinput::inflight||hvinput::active)return HV_INPUT_BUSY;hvinput::owner=h;hvinput::target_dirty=true;hvinput::unity_texture=texture;hvinput::target_width=width;hvinput::target_height=height;hvinput::target_generation=generation;return HV_INPUT_OK;}
__attribute__((visibility("default"))) UnityRenderingEvent HV_Input_GetRenderEventFunc(){return hvinput::InputRenderEvent;}
__attribute__((visibility("default"))) int HV_Input_GetColorProbeEventId(){return hvinput::event_id;}
__attribute__((visibility("default"))) uint64_t HV_Input_GetColorCompletedSequence(){std::lock_guard<std::mutex> lock(hvinput::mutex);return hvinput::completed_token;}
__attribute__((visibility("default"))) int HV_Input_GetColorProbeGeometry(uint32_t* width,uint32_t* height,uint64_t* generation){std::lock_guard<std::mutex> lock(hvinput::mutex);if(!width||!height||!generation)return HV_INPUT_INVALID;if(!hvinput::pending)return HV_INPUT_NO_FRAME;*width=hvinput::pending->crop_right-hvinput::pending->crop_left;*height=hvinput::pending->crop_bottom-hvinput::pending->crop_top;*generation=hvinput::pending->generation;return 0;}
__attribute__((visibility("default"))) void HV_Input_RequestColorProbeDrain(){std::lock_guard<std::mutex> lock(hvinput::mutex);hvinput::paused=true;hvinput::ReturnImage(hvinput::pending);}
__attribute__((visibility("default"))) void HV_Input_RetireColorProbeTarget(){std::lock_guard<std::mutex> lock(hvinput::mutex);hvinput::retire_target=true;}
__attribute__((visibility("default"))) int HV_Input_ColorProbeTargetRetired(){std::lock_guard<std::mutex> lock(hvinput::mutex);return !hvinput::target_view&&!hvinput::retire_target;}
__attribute__((visibility("default"))) int HV_Input_ColorProbeRetired(){std::lock_guard<std::mutex> lock(hvinput::mutex);return !hvinput::active&&!hvinput::pending&&!hvinput::inflight;}
__attribute__((visibility("default"))) int HV_Input_SetColorProbeTransform(int rotation,int mirror){if(rotation!=0&&rotation!=90&&rotation!=180&&rotation!=270)return HV_INPUT_INVALID;std::lock_guard<std::mutex> lock(hvinput::mutex);if(hvinput::inflight||hvinput::active)return HV_INPUT_BUSY;hvinput::rotation=rotation;hvinput::mirror=mirror!=0;return 0;}
__attribute__((visibility("default"))) void HV_Input_LogColorProbeCounters(){std::lock_guard<std::mutex> lock(hvinput::mutex);const auto& c=hvinput::counters;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_counters imports=%llu destroys=%llu submits=%llu completes=%llu positive_acquire_waits=%llu already_complete_acquires=%llu release_exports=%llu ownership_acquires=%llu ownership_returns=%llu queue_callbacks=%llu target_views_created=%llu target_views_destroyed=%llu source_views_created=%llu source_views_destroyed=%llu pipelines_created=%llu pipelines_destroyed=%llu descriptor_pools_created=%llu descriptor_pools_destroyed=%llu errors=%llu cache_live=%u cpu_image_readbacks=0",(unsigned long long)c.creates,(unsigned long long)c.destroys,(unsigned long long)c.submits,(unsigned long long)c.completes,(unsigned long long)c.positive_waits,(unsigned long long)c.already_complete,(unsigned long long)c.exports,(unsigned long long)c.ownership_acquires,(unsigned long long)c.ownership_returns,(unsigned long long)c.queue_callbacks,(unsigned long long)c.target_views_created,(unsigned long long)c.target_views_destroyed,(unsigned long long)c.source_views_created,(unsigned long long)c.source_views_destroyed,(unsigned long long)c.pipelines_created,(unsigned long long)c.pipelines_destroyed,(unsigned long long)c.descriptor_pools_created,(unsigned long long)c.descriptor_pools_destroyed,(unsigned long long)c.errors,hvinput::cache.Live());}
}

extern "C" __attribute__((visibility("default"))) int HV_Input_ColorProbeActive(){std::lock_guard<std::mutex> lock(hvinput::mutex);return hvinput::active&&hvinput::inflight;}

extern "C" __attribute__((visibility("default"))) void HV_Input_FailNextColorTargetView(){std::lock_guard<std::mutex> lock(hvinput::mutex);hvinput::fail_target_view=1;}
extern "C" {
HV_INPUT_API int HV_INPUT_CALL HV_Input_BindGpuTargets(HV_InputHandle h,void* const textures[3],uint32_t w,uint32_t he,uint64_t generation,int degrees,int mirrored){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(!h||h!=hvinput::owner||!hvinput::production||!textures||!textures[0]||!textures[1]||!textures[2]||!w||!he||!generation||(degrees!=0&&degrees!=90&&degrees!=180&&degrees!=270))return HV_INPUT_INVALID;
 if(hvinput::inflight||hvinput::active)return HV_INPUT_BUSY;
 for(auto view:hvinput::output_views)if(view){hvinput::retire_target=true;return HV_INPUT_BUSY;}
 for(int i=0;i<3;++i)hvinput::output_textures[i]=textures[i];
 hvinput::target_width=w;hvinput::target_height=he;hvinput::target_generation=generation;hvinput::rotation=degrees;hvinput::mirror=mirrored!=0;hvinput::closing=hvinput::paused=false;
 hvinput::frame_ring.Begin(generation);hvinput::frame_ring.Collect(hvinput::completed_token);hvinput::published_frame={};return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetGpuGeometry(HV_InputHandle h,uint32_t* w,uint32_t* he,uint64_t* g){std::lock_guard<std::mutex> lock(hvinput::mutex);if(!h||h!=hvinput::owner||!w||!he||!g)return HV_INPUT_INVALID;if(!hvinput::pending)return HV_INPUT_NO_FRAME;*w=hvinput::pending->crop_right-hvinput::pending->crop_left;*he=hvinput::pending->crop_bottom-hvinput::pending->crop_top;*g=hvinput::pending->generation;return 0;}
HV_INPUT_API int HV_INPUT_CALL HV_Input_PollGpuFrame(HV_InputHandle h,uint64_t after,HV_InputGpuFrame* f){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(!h||h!=hvinput::owner||!f)return HV_INPUT_INVALID;
 // Previously the main thread could only see a completion polled at the next
 // render callback, adding another Unity frame before preview/submission. Read
 // an already-signaled fence here; unfinished GPU work still returns NO_FRAME.
 if(hvinput::initialized)hvinput::PollConversionCompletion();
 if(hvinput::closing||hvinput::published_frame.frame.sequence<=after||!hvinput::published_frame.frame.sequence)return HV_INPUT_NO_FRAME;
 *f=hvinput::published_frame;hvinput::frame_ring.Observe(f->slot,f->frame.sequence);return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_ReleaseGpuSlot(HV_InputHandle h,uint32_t slot,uint64_t sequence){std::lock_guard<std::mutex> lock(hvinput::mutex);if(!h||h!=hvinput::owner||slot>=3)return HV_INPUT_INVALID;hvinput::frame_ring.Release(slot,sequence);hvinput::frame_ring.Collect(hvinput::completed_token);return 0;}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuRetired(HV_InputHandle h){return hvinput::InputGpuRetired(h)?1:0;}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuCopyActive(HV_InputHandle h){std::lock_guard<std::mutex> lock(hvinput::mutex);return h==hvinput::owner&&hvinput::active&&hvinput::inflight;}
HV_INPUT_API void HV_INPUT_CALL HV_Input_LogGpuCounters(){HV_Input_LogColorProbeCounters();std::lock_guard<std::mutex> lock(hvinput::mutex);__android_log_print(ANDROID_LOG_INFO,"HVInputGate","input_ring_counters slots_live=%u slot_drops=%llu decoded_drops=%llu old_generation_rejections=%llu",hvinput::frame_ring.Live(),(unsigned long long)hvinput::frame_ring.Drops(),(unsigned long long)hvinput::decoded_drops,(unsigned long long)hvinput::old_generation_rejections);}
}
extern "C" HV_INPUT_API void HV_INPUT_CALL HV_Input_RetireGpuTargets(HV_InputHandle h){std::lock_guard<std::mutex> lock(hvinput::mutex);if(h==hvinput::owner){hvinput::paused=true;hvinput::ReturnImage(hvinput::pending);hvinput::frame_ring.Close();hvinput::published_frame={};hvinput::retire_target=true;}}
extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuTargetsRetired(HV_InputHandle h){std::lock_guard<std::mutex> lock(hvinput::mutex);if(h!=hvinput::owner)return 1;return !hvinput::retire_target&&!hvinput::inflight&&!hvinput::active&&!hvinput::output_views[0]&&!hvinput::output_views[1]&&!hvinput::output_views[2];}
namespace hvinput {
namespace {
struct InputCopyProbe {void* source=nullptr;void* destination=nullptr;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;UnityVulkanImage src{},dst{};bool queued=false,submitted=false,done=true;uint64_t submits=0,completes=0,sequence=0,generation=0;} copy_probe;
void CopyProbeFailure(const char* stage){copy_probe.queued=false;copy_probe.done=true;Error(stage);}
void UNITY_INTERFACE_API CopyProbeQueue(int,void*) {
 std::lock_guard<std::mutex> lock(mutex);auto& c=copy_probe;auto d=api.unity.device;
 if(!c.pool){
  VkCommandPool candidate=VK_NULL_HANDLE;VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,nullptr,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,api.unity.queueFamilyIndex};
  if(api.CreateCommandPool(d,&pool,nullptr,&candidate)!=VK_SUCCESS){CopyProbeFailure("consumer copy command pool");return;}c.pool=candidate;
  VkCommandBuffer cmd=VK_NULL_HANDLE;VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,nullptr,c.pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
  if(api.AllocateCommandBuffers(d,&commands,&cmd)!=VK_SUCCESS){CopyProbeFailure("consumer copy command");return;}c.command=cmd;
  VkFence candidate_fence=VK_NULL_HANDLE;VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if(api.CreateFence(d,&fence_info,nullptr,&candidate_fence)!=VK_SUCCESS){CopyProbeFailure("consumer copy fence");return;}c.fence=candidate_fence;
 }

 if(api.ResetCommandBuffer(c.command,0)!=VK_SUCCESS||api.ResetFences(d,1,&c.fence)!=VK_SUCCESS){CopyProbeFailure("consumer copy reset");return;}
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,nullptr,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};if(api.BeginCommandBuffer(c.command,&begin)!=VK_SUCCESS){CopyProbeFailure("consumer copy begin");return;}
 VkImageCopy region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.extent=c.src.extent;
 api.CmdCopyImage(c.command,c.src.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,c.dst.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 if(api.EndCommandBuffer(c.command)!=VK_SUCCESS){CopyProbeFailure("consumer copy end");return;}
 VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&c.command;
 if(api.QueueSubmit(api.unity.graphicsQueue,1,&submit,c.fence)!=VK_SUCCESS){CopyProbeFailure("consumer copy queue submit");return;}
 c.queued=false;c.submitted=true;++c.submits;
 __android_log_print(ANDROID_LOG_INFO,"HVInputGate","consumer_source_copy_submitted=%llu real_gpu_fence=1 serialized_access_queue=1 source_sequence=%llu generation=%llu source_image=%p destination_image=%p",(unsigned long long)c.submits,(unsigned long long)c.sequence,(unsigned long long)c.generation,c.src.image,c.dst.image);
}
}
void ConfigureInputCopyProbeEvent(){auto* u=InputUnityVulkan();if(!u)return;UnityVulkanPluginEventConfig config{kUnityVulkanRenderPass_EnsureOutside,kUnityVulkanGraphicsQueueAccess_DontCare,kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission|kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState};u->ConfigureEvent(event_id+1,&config);}
void InputCopyProbeEvent(){bool submit=false;{std::lock_guard<std::mutex> lock(mutex);auto& c=copy_probe;if(!c.queued||c.submitted)return;if(!InitializeResources()){CopyProbeFailure("consumer copy resources");return;}auto* u=InputUnityVulkan();if(!u->AccessTexture(c.source,UnityVulkanWholeImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,kUnityVulkanResourceAccess_PipelineBarrier,&c.src)||!u->AccessTexture(c.destination,UnityVulkanWholeImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,kUnityVulkanResourceAccess_PipelineBarrier,&c.dst)||c.src.format!=c.dst.format||c.src.extent.width!=c.dst.extent.width||c.src.extent.height!=c.dst.extent.height){CopyProbeFailure("consumer actual Unity copy textures");return;}submit=true;}if(submit)InputUnityVulkan()->AccessQueue(CopyProbeQueue,event_id+1,nullptr,true);}
}
extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_QueueDiagnosticSourceCopy(HV_InputHandle h,void* source,void* destination){std::lock_guard<std::mutex> lock(hvinput::mutex);auto& c=hvinput::copy_probe;if(!h||h!=hvinput::owner||!source||!destination)return HV_INPUT_INVALID;if(c.queued||c.submitted)return HV_INPUT_BUSY;c.source=source;c.destination=destination;c.sequence=hvinput::published_frame.frame.sequence;c.generation=hvinput::published_frame.frame.generation;c.queued=true;c.done=false;return 0;}
extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_DiagnosticSourceCopyComplete(){std::lock_guard<std::mutex> lock(hvinput::mutex);auto& c=hvinput::copy_probe;if(c.done)return 1;if(!c.submitted)return 0;auto status=hvinput::api.GetFenceStatus(hvinput::api.unity.device,c.fence);if(status==VK_NOT_READY)return 0;if(status!=VK_SUCCESS){hvinput::Error("consumer copy completion",status);return 0;}c.submitted=false;c.done=true;++c.completes;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","consumer_source_copy_completed=%llu real_gpu_fence=1",(unsigned long long)c.completes);return 1;}
// Private device-fixture cleanup; production RtspFrameSource never calls diagnostic copy APIs.
extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_RetireDiagnosticSourceCopy(){std::lock_guard<std::mutex> lock(hvinput::mutex);auto& c=hvinput::copy_probe;if(c.submitted)return HV_INPUT_BUSY;if(c.queued){c.queued=false;c.done=true;}auto d=hvinput::api.unity.device;if(c.fence){hvinput::api.DestroyFence(d,c.fence,nullptr);c.fence=VK_NULL_HANDLE;}if(c.pool){hvinput::api.DestroyCommandPool(d,c.pool,nullptr);c.pool=VK_NULL_HANDLE;c.command=VK_NULL_HANDLE;}__android_log_print(ANDROID_LOG_INFO,"HVInputGate","consumer_copy_resources_retired=1 command_pool_live=0 fence_live=0 submits=%llu completes=%llu",(unsigned long long)c.submits,(unsigned long long)c.completes);return 0;}
