#include "input_vulkan_private.h"
#include "input_image_cache.h"
#include "input_target_owner.h"
#include "input_yuv_shader.h"
#include "humanvision_input.h"
#include <android/log.h>
#include <media/NdkImageReader.h>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <unistd.h>
namespace hvinput {
namespace {
constexpr int event_id=0x485649;
#define INPUT_VK_FUNCTIONS(X) \
 X(GetAndroidHardwareBufferPropertiesANDROID) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFormatProperties) \
 X(CreateImage) X(DestroyImage) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
 X(CreateSamplerYcbcrConversion) X(DestroySamplerYcbcrConversion) X(CreateSampler) X(DestroySampler) X(CreateImageView) X(DestroyImageView) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) \
 X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(ResetFences) X(CreateSemaphore) X(DestroySemaphore) X(ImportSemaphoreFdKHR) X(GetSemaphoreFdKHR) X(QueueSubmit)
struct Api {
#define DECLARE(name) PFN_vk##name name=nullptr;
 INPUT_VK_FUNCTIONS(DECLARE)
#undef DECLARE
 UnityVulkanInstance unity{};bool Load() {
  auto* u=InputUnityVulkan();if(!u)return false;unity=u->Instance();if(!unity.device||!InputForeignEnabled())return false;
  auto gd=reinterpret_cast<PFN_vkGetDeviceProcAddr>(unity.getInstanceProcAddr(unity.instance,"vkGetDeviceProcAddr"));
  if(!gd)return false;
#define LOAD(name) name=reinterpret_cast<PFN_vk##name>(gd(unity.device,"vk" #name));if(!name)name=reinterpret_cast<PFN_vk##name>(unity.getInstanceProcAddr(unity.instance,"vk" #name));if(!name)return false;
 INPUT_VK_FUNCTIONS(LOAD)
#undef LOAD
  return true;
 }
} api;
struct Imported {
 VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;
 VkSamplerYcbcrConversion conversion=VK_NULL_HANDLE;VkSampler sampler=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;VkDescriptorSet descriptor=VK_NULL_HANDLE;
 VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
 bool reference=false;
};
struct Counters {uint64_t creates=0,destroys=0,submits=0,completes=0,positive_waits=0,already_complete=0,exports=0,ownership_acquires=0,ownership_returns=0,queue_callbacks=0,target_views_created=0,target_views_destroyed=0,source_views_created=0,source_views_destroyed=0,pipelines_created=0,pipelines_destroyed=0,descriptor_pools_created=0,descriptor_pools_destroyed=0,errors=0;} counters;
uint64_t completed_token=0;
void Error(const char* stage,VkResult code=VK_ERROR_UNKNOWN){++counters.errors;__android_log_print(ANDROID_LOG_ERROR,"HVInputGate","color_result=FAIL stage=%s code=%d",stage,code);}
void DestroyResource(CacheEntry& e){
 auto* r=static_cast<Imported*>(e.resource);if(!r)return;auto d=api.unity.device;
 if(r->pipeline){api.DestroyPipeline(d,r->pipeline,nullptr);++counters.pipelines_destroyed;}
 if(r->layout)api.DestroyPipelineLayout(d,r->layout,nullptr);
 if(r->descriptor_pool){api.DestroyDescriptorPool(d,r->descriptor_pool,nullptr);++counters.descriptor_pools_destroyed;}
 if(r->set_layout)api.DestroyDescriptorSetLayout(d,r->set_layout,nullptr);
 if(r->view){api.DestroyImageView(d,r->view,nullptr);++counters.source_views_destroyed;}
 if(r->sampler)api.DestroySampler(d,r->sampler,nullptr);
 if(r->conversion)api.DestroySamplerYcbcrConversion(d,r->conversion,nullptr);
 if(r->image)api.DestroyImage(d,r->image,nullptr);
 if(r->memory)api.FreeMemory(d,r->memory,nullptr);
 if(r->reference)AHardwareBuffer_release(static_cast<AHardwareBuffer*>(e.buffer));
 delete r;e.resource=nullptr;
}
struct Importer:ImageImporter {
 bool Complete(uint64_t token)override{return token<=completed_token;}
 void Destroy(CacheEntry& e)override{DestroyResource(e);++counters.destroys;}
 bool Create(CacheEntry& e)override {
  auto resource=std::make_unique<Imported>();e.resource=resource.get();auto d=api.unity.device;
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
   AHardwareBuffer_acquire(static_cast<AHardwareBuffer*>(e.buffer));resource->reference=true;
   __android_log_print(ANDROID_LOG_INFO,"HVInputGate","ahb_import_created buffer=%p external_format=%llu width=%u height=%u model=%u range=%u descriptors=%u",e.buffer,(unsigned long long)c.external_format,c.width,c.height,c.model,c.range,descriptor_budget);
   return true;
  };
  success=work();resource.release();if(!success){DestroyResource(e);return false;}++counters.creates;return true;
 }
} importer;
InputImageCache cache(importer);
std::mutex mutex,removed_mutex;std::condition_variable retired;
std::array<AHardwareBuffer*,16> removed{};
AndroidDecodedImage image_storage;
AndroidDecodedImage* pending=nullptr;AndroidDecodedImage* active=nullptr;
void ReturnImage(AndroidDecodedImage*& p){if(p){p->Reset();p=nullptr;retired.notify_all();}}
bool enabled=false,initialized=false,closing=false,paused=false,inflight=false;
HV_InputHandle owner=nullptr;void* unity_texture=nullptr;uint32_t target_width=0,target_height=0;uint64_t target_generation=0;
int rotation=0;bool mirror=false;uint64_t token=0;
VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
VkSemaphore wait_semaphore=VK_NULL_HANDLE,release_semaphore=VK_NULL_HANDLE;
VkImageView target_view=VK_NULL_HANDLE;VkImage last_target=VK_NULL_HANDLE;
UnityVulkanImage target{};CacheEntry* source_entry=nullptr;bool target_dirty=false,retire_target=false;int fail_target_view=0;
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
void Poll(){
 if(inflight){auto status=api.GetFenceStatus(api.unity.device,fence);if(status==VK_NOT_READY)return;if(status!=VK_SUCCESS){Error("poll conversion GPU fence",status);return;}
  completed_token=token;++counters.completes;inflight=false;
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_completed sequence=%llu generation=%llu pts_us=%lld received_us=%lld decoded_us=%lld cpu_image_readbacks=0 target_width=%u target_height=%u applied_rotation=%d applied_mirror=%d source_matrix=%u source_range=%u transfer=%u primaries=%u color_space=%u",(unsigned long long)token,(unsigned long long)active->generation,(long long)active->pts_us,(long long)active->received_us,(long long)active->decoded_us,target_width,target_height,rotation,mirror,active->matrix,active->color_range,active->transfer,active->primaries,0u);
  ReturnImage(active);retired.notify_all();
 }
 {std::lock_guard<std::mutex> lock(removed_mutex);for(auto& b:removed)if(b){cache.Remove(b);b=nullptr;}}
 if(closing)cache.RemoveAll();cache.Collect();
}
void UNITY_INTERFACE_API SubmitOnUnityQueue(int,void*){
 std::lock_guard<std::mutex> lock(mutex);++counters.queue_callbacks;
 if(!active||!source_entry||inflight){Error("queue callback invalid pending conversion");return;}
 auto d=api.unity.device;auto* r=static_cast<Imported*>(source_entry->resource);
 bool waited=active->acquire_fd>=0;
 if(waited){int wait_fd=dup(active->acquire_fd);if(wait_fd<0){Error("duplicate acquire fence for safe failure return");ReturnImage(active);return;}VkImportSemaphoreFdInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,nullptr,wait_semaphore,VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,wait_fd};auto result=api.ImportSemaphoreFdKHR(d,&import);if(result!=VK_SUCCESS){close(wait_fd);Error("import actual acquire sync fd",result);ReturnImage(active);return;}++counters.positive_waits;}
 else ++counters.already_complete;
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
 active->gpu_submitted=true;inflight=true;++token;++counters.submits;++counters.ownership_acquires;++counters.ownership_returns;cache.Used(*source_entry,token);
 VkSemaphoreGetFdInfoKHR export_info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,nullptr,release_semaphore,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
 result=api.GetSemaphoreFdKHR(d,&export_info,&active->release_fd);if(result!=VK_SUCCESS){Error("export actual GPU release sync fd",result);return;}++counters.exports;
 __android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_submitted sequence=%llu buffer=%p acquire_wait=%s release_fd=%d ownership=FOREIGN_EXT_to_Unity_to_FOREIGN_EXT serialized_access_queue=1",(unsigned long long)token,active->buffer,waited?"imported_sync_fd":"actual_minus_one_already_complete",active->release_fd);
}
}
bool ColorProbeEnabled(){std::lock_guard<std::mutex> lock(mutex);return enabled;}
bool QueueColorImage(AndroidDecodedImage& image){std::lock_guard<std::mutex> lock(mutex);if(!enabled||closing||paused||pending||active)return false;image_storage.TakeFrom(image);pending=&image_storage;return true;}
void NotifyRemovedBuffer(AHardwareBuffer* buffer){std::lock_guard<std::mutex> lock(removed_mutex);for(auto& b:removed)if(!b){b=buffer;return;}Error("buffer removed notification capacity");}
void DrainColorImagesBeforeReaderClose(){std::unique_lock<std::mutex> lock(mutex);if(!enabled)return;closing=true;paused=true;ReturnImage(pending);retired.wait(lock,[]{return !active;});}
void ConfigureInputRenderEvent(){auto* u=InputUnityVulkan();if(!u)return;UnityVulkanPluginEventConfig config{kUnityVulkanRenderPass_EnsureOutside,kUnityVulkanGraphicsQueueAccess_DontCare,kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission|kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState};u->ConfigureEvent(event_id,&config);}
void InputRenderEvent(int id){
 if(id!=event_id)return;bool submit=false;
 {std::lock_guard<std::mutex> lock(mutex);if(!enabled)return;if(initialized)Poll();if(retire_target&&!inflight&&!active){if(target_view){api.DestroyImageView(api.unity.device,target_view,nullptr);target_view=VK_NULL_HANDLE;++counters.target_views_destroyed;}last_target=VK_NULL_HANDLE;unity_texture=nullptr;retire_target=false;}if(closing||paused||inflight||active||!pending||!unity_texture)return;
 if(!InitializeResources()){Error("initialize input GPU resources");ReturnImage(pending);return;}
 InputGpuCapabilities c{};if(!ProbeDecodedBuffer(pending->buffer,api.unity.physicalDevice,&c)||AdmitDecodedBuffer(c)!=CapabilityFailure::None){Error("actual decoded import capabilities");ReturnImage(pending);return;}
 if(pending->generation!=target_generation){Error("target/source generation mismatch");ReturnImage(pending);return;}
 auto* u=InputUnityVulkan();
 if(!u->AccessTexture(unity_texture,UnityVulkanWholeImage,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,kUnityVulkanResourceAccess_PipelineBarrier,&target)){Error("Unity AccessTexture actual target");ReturnImage(pending);return;}
 VkFormatProperties features{};api.GetPhysicalDeviceFormatProperties(api.unity.physicalDevice,target.format,&features);
 if(target.format!=VK_FORMAT_R8G8B8A8_UNORM||!(target.usage&VK_IMAGE_USAGE_STORAGE_BIT)||!(features.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)||target.extent.width!=target_width||target.extent.height!=target_height){Error("actual Unity target lacks qualified rgba8 storage contract");ReturnImage(pending);return;}
 if(target_dirty||target.image!=last_target){
 VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=target.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=target.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 VkResult result=VK_SUCCESS;
 const bool created=ReplaceTargetView(target_view,
  [&](VkImageView old){api.DestroyImageView(api.unity.device,old,nullptr);++counters.target_views_destroyed;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","target_view_replacement_old_retired=true");},
  [&](VkImageView& candidate){
   if(fail_target_view){--fail_target_view;result=VK_ERROR_OUT_OF_HOST_MEMORY;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","target_view_fault_injected=true retired_handle_null=%d pending_lease_returned_on_failure=1",target_view==VK_NULL_HANDLE);return false;}
   result=api.CreateImageView(api.unity.device,&view,nullptr,&candidate);return result==VK_SUCCESS;
  });
 if(!created){Error("create actual Unity storage view",result);last_target=VK_NULL_HANDLE;ReturnImage(pending);return;}
last_target=target.image;target_dirty=false;++counters.target_views_created;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","actual_unity_target image=%p format=%u usage=%u storage_features=%u width=%u height=%u",target.image,target.format,target.usage,features.optimalTilingFeatures,target_width,target_height);}
 ImageContract contract{};contract.external_format=c.external_format;contract.format=c.format;contract.features=c.format_features;contract.model=c.model;contract.range=c.range;contract.x_chroma=c.x_chroma;contract.y_chroma=c.y_chroma;std::memcpy(contract.components,c.components,sizeof(c.components));
 AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(pending->buffer,&desc);contract.width=desc.width;contract.height=desc.height;
 if(pending->matrix==1)contract.model=VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;else if(pending->matrix==2)contract.model=VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
 if(pending->color_range==1)contract.range=VK_SAMPLER_YCBCR_RANGE_ITU_FULL;else if(pending->color_range==2)contract.range=VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
 source_entry=cache.Acquire(pending->buffer,pending->generation,contract);if(!source_entry){Error("acquire reusable AHB import");ReturnImage(pending);return;}
 active=pending;pending=nullptr;submit=true;
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
