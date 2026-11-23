#include "input_vulkan_private.h"
#include "input_image_cache.h"
#include "input_buffer_registry.h"
#include "input_vulkan_proc_loader.h"
#include "input_target_owner.h"
#include "input_conversion.h"
#include "humanvision_input.h"
#include "input_internal.h"
#include "android_input_vulkan.h"
#include "input_frame_ring.h"
#include "input_split_schedule.h"
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
struct Api : InputConversionApi {
 UnityVulkanInstance unity{};
 bool Load() {
  auto* u=InputUnityVulkan(); if(!u)return false;
  unity=u->Instance();
  return unity.device && InputForeignEnabled() && LoadFunctions(unity.device, unity.instance, unity.getInstanceProcAddr);
 }
} api;
InputBufferRegistry buffer_registry;
Counters counters{};
uint64_t completed_token=0;
void ProductionError(const char*,VkResult);
void Error(const char* stage,VkResult code=VK_ERROR_UNKNOWN){++counters.errors;ProductionError(stage,code);__android_log_print(ANDROID_LOG_ERROR,"HVInputGate","color_result=FAIL stage=%s code=%d",stage,code);}
InputConversionImporter importer(api,buffer_registry,counters,completed_token,Error);
InputImageCache cache(importer);
std::mutex mutex;std::condition_variable retired;
Counters session_baseline{};BufferRegistryStats registry_baseline{};bool terminal_reported=false;
AndroidDecodedImage image_storage, active_storage, preview_storage;
InputFrameRing frame_ring;
bool production=false;int output_slot=-1,paired_preview_slot=-1;
std::array<void*,3> output_textures{};
std::array<VkImageView,3> output_views{};
std::array<VkImage,3> output_images{};
HV_InputGpuFrame published_frame{};
struct AnalysisOutput {
 InputFrameRing ring;
 std::array<void*,3> textures{};
 std::array<VkImageView,3> views{};
 std::array<VkImage,3> images{};
 HV_InputGpuFrame published{};
 uint32_t width=0,height=0;
 uint64_t generation=0;
} analysis;
enum class Conversion { Legacy, Analysis, Preview, PreviewOnly };
Conversion conversion=Conversion::Legacy;
uint32_t preview_width=0,preview_height=0;
uint64_t decoded_sequence=0,active_sequence=0;
int64_t active_analysis_completed_us=0;
uint64_t acknowledged_sequence=0;
int64_t acknowledged_us=0;
InputSplitSchedule split_schedule;
InputFrameRing& ConversionRing() { return conversion==Conversion::Analysis ? analysis.ring : frame_ring; }
uint64_t decoded_drops=0,decoded_pending_replaced=0,old_generation_rejections=0;
AndroidDecodedImage* pending=nullptr;AndroidDecodedImage* active=nullptr;AndroidDecodedImage* preview_pending=nullptr;
bool enabled=false,initialized=false,closing=false,paused=false,inflight=false;
void ReturnImage(AndroidDecodedImage*& p){if(p){
 // Only the active image owns an acquired output slot. Replacing either
 // mailbox must leave an already scheduled AccessQueue callback untouched.
 if(p==active){
  if(production&&!inflight){
   ConversionRing().Cancel(output_slot);
   frame_ring.Cancel(paired_preview_slot);
  }
  paired_preview_slot=-1;
  active_sequence=0;
  active_analysis_completed_us=0;
 }
 if(p==preview_pending)split_schedule.Reset();
 p->Reset();p=nullptr;retired.notify_all();}}

HV_InputHandle owner=nullptr;void* unity_texture=nullptr;uint32_t target_width=0,target_height=0;uint64_t target_generation=0;
void ProductionErrorText(const char* stage,const char* detail){
 if(production&&owner)owner->SetGpuError(stage,detail);
}
void ProductionError(const char* stage,VkResult code){char detail[64]{};std::snprintf(detail,sizeof(detail),"Vulkan result %d",int(code));ProductionErrorText(stage,detail);}
int rotation=0;bool mirror=false;uint64_t token=0;
VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
VkSemaphore wait_semaphore=VK_NULL_HANDLE,release_semaphore=VK_NULL_HANDLE;
VkImageView target_view=VK_NULL_HANDLE;VkImage last_target=VK_NULL_HANDLE;
UnityVulkanImage target{},paired_preview_target{};CacheEntry* source_entry=nullptr;bool target_dirty=false,retire_target=false;int fail_target_view=0;
// One bounded log per second, outside per-pixel work. These are host observation
// intervals (including queue/fence polling), not GPU execution timestamps.
struct InputCadence {
 int64_t report_us=0,submitted_us=0,decode_wait_us=0,completion_wait_us=0;
 uint64_t events=0,busy=0,empty=0,complete=0,no_free_slot=0,start_unobserved=0;
 uint64_t fence_pending_events=0, preview_published=0;
 uint64_t last_drops=0,last_overwrites=0,last_superseded_unobserved=0;
} cadence;
struct SplitCadence {
 uint64_t analysis_completed=0, acknowledgements=0, preview_started_after_ack=0;
 uint64_t preview_without_ack=0, preview_expired=0, preview_skipped_no_slot=0, analysis_no_slot=0;
 uint64_t preview_yield_to_analysis=0, preview_batched=0, preview_replaced_by_batch=0;
 uint64_t preview_only_submitted=0, preview_only_published=0;
 uint64_t preview_target_prepare_failures=0;
 uint64_t decoded=0, decoded_replaced=0, analysis_published=0;
 int64_t analysis_decode_wait_us=0, analysis_fence_wait_us=0;
} split_cadence;
bool SplitEnabled() { return analysis.generation!=0 && analysis.generation==target_generation; }
void LogSplitFrame(const char* phase, int64_t now) {
 if (!active || !ShouldLogInputFrame(active_sequence, false)) return;
 __android_log_print(ANDROID_LOG_INFO,"HVInputGate",
  "input_split_frame phase=%s generation=%llu sequence=%llu monotonic_us=%lld analysis_complete_us=%lld analysis_ack_us=%lld",
  phase,(unsigned long long)active->generation,(unsigned long long)active_sequence,(long long)now,
  (long long)active_analysis_completed_us,(long long)(active_sequence==acknowledged_sequence?acknowledged_us:0));
}
void ReportInputCadence(){
 if(!production)return;
 const auto now=NowUs();
 if(!cadence.report_us){cadence.report_us=now;cadence.last_drops=frame_ring.Drops();cadence.last_overwrites=decoded_pending_replaced;cadence.last_superseded_unobserved=frame_ring.SupersededUnobserved();return;}
 const auto elapsed=now-cadence.report_us;if(elapsed<1000000)return;
 __android_log_print(ANDROID_LOG_INFO,"HVInputGate",
  "input_gpu_cadence generation=%llu interval_ms=%.3f render_events=%llu busy_events=%llu no_image_events=%llu completed=%llu converted_fps=%.3f slot_drops=%llu pending_replaced=%llu decoded_to_submit_mean_ms=%.3f submit_to_observed_fence_mean_ms=%.3f host_observation_not_gpu_execution=1 no_free_slot_events=%llu start_unobserved_events=%llu superseded_unobserved=%llu fence_pending_events=%llu preview_published=%llu",
  (unsigned long long)target_generation,elapsed/1000.0,(unsigned long long)cadence.events,
  (unsigned long long)cadence.busy,(unsigned long long)cadence.empty,(unsigned long long)cadence.complete,
  cadence.complete*1000000.0/elapsed,(unsigned long long)(frame_ring.Drops()-cadence.last_drops),
  (unsigned long long)(decoded_pending_replaced-cadence.last_overwrites),
  cadence.complete?cadence.decode_wait_us/1000.0/cadence.complete:0,
  cadence.complete?cadence.completion_wait_us/1000.0/cadence.complete:0,
  (unsigned long long)cadence.no_free_slot,(unsigned long long)cadence.start_unobserved,
  (unsigned long long)(frame_ring.SupersededUnobserved()-cadence.last_superseded_unobserved),
  (unsigned long long)cadence.fence_pending_events,(unsigned long long)cadence.preview_published);
 if(SplitEnabled()){
  const auto& s=split_cadence;
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate",
   "input_analysis_cadence generation=%llu interval_ms=%.3f completed=%llu analysis_fps=%.3f acknowledgements=%llu preview_started_after_ack=%llu preview_without_ack=%llu preview_expired=%llu preview_skipped_no_slot=%llu analysis_no_slot=%llu decoded_to_submit_mean_ms=%.3f submit_to_observed_fence_mean_ms=%.3f host_observation_not_gpu_execution=1 preview_yield_to_analysis=%llu split_decoded=%llu split_decoded_replaced=%llu analysis_published=%llu preview_batched=%llu preview_replaced_by_batch=%llu preview_target_prepare_failures=%llu preview_only_submitted=%llu preview_only_published=%llu",
   (unsigned long long)target_generation,elapsed/1000.0,(unsigned long long)s.analysis_completed,
   s.analysis_completed*1000000.0/elapsed,(unsigned long long)s.acknowledgements,
   (unsigned long long)s.preview_started_after_ack,(unsigned long long)s.preview_without_ack,
   (unsigned long long)s.preview_expired,(unsigned long long)s.preview_skipped_no_slot,
   (unsigned long long)s.analysis_no_slot,
   s.analysis_completed?s.analysis_decode_wait_us/1000.0/s.analysis_completed:0,
   s.analysis_completed?s.analysis_fence_wait_us/1000.0/s.analysis_completed:0,
   (unsigned long long)s.preview_yield_to_analysis,
   (unsigned long long)s.decoded,(unsigned long long)s.decoded_replaced,(unsigned long long)s.analysis_published,
   (unsigned long long)s.preview_batched,(unsigned long long)s.preview_replaced_by_batch,
   (unsigned long long)s.preview_target_prepare_failures,
   (unsigned long long)s.preview_only_submitted,(unsigned long long)s.preview_only_published);
 }
 split_cadence={};
 cadence.report_us=now;cadence.events=cadence.busy=cadence.empty=cadence.complete=0;
 cadence.no_free_slot=cadence.start_unobserved=0;
 cadence.fence_pending_events=cadence.preview_published=0;
 cadence.decode_wait_us=cadence.completion_wait_us=0;
 cadence.last_drops=frame_ring.Drops();cadence.last_overwrites=decoded_pending_replaced;
 cadence.last_superseded_unobserved=frame_ring.SupersededUnobserved();
}
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
bool PublishConversion(InputFrameRing& ring, HV_InputGpuFrame& output, int slot,
                       uint32_t width, uint32_t height, const char* branch) {
 if(closing||paused||!ring.Complete(slot,completed_token)){
  ++old_generation_rejections;
  return false;
 }
 auto& f=output.frame;f={};f.size=sizeof(f);f.version=1;f.sequence=active_sequence;f.generation=active->generation;f.width=width;f.height=height;
 f.received_timestamp_us=active->received_us;f.decoded_timestamp_us=active->decoded_us;f.presentation_timestamp_us=active->pts_us;
 f.clock_id=ClockId();f.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;f.timestamp_kind=HV_INPUT_TIME_LOCAL_DECODE;f.pts_valid=1;f.row_origin=0;f.color_space=0;f.decode_mode=2;
 output.slot=slot;
 if(ShouldLogInputFrame(active_sequence,false))__android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_frame_published sequence=%llu generation=%llu slot=%d branch=%s",(unsigned long long)active_sequence,(unsigned long long)active->generation,slot,branch);
 return true;
}
void LogColorCompletion(uint32_t width, uint32_t height, int64_t now) {
 if(ShouldLogInputFrame(active_sequence,!production))__android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_completed sequence=%llu generation=%llu pts_us=%lld received_us=%lld decoded_us=%lld submitted_us=%lld converted_us=%lld cpu_image_readbacks=0 target_width=%u target_height=%u applied_rotation=%d applied_mirror=%d source_matrix=%u source_range=%u transfer=%u primaries=%u color_space=%u",(unsigned long long)active_sequence,(unsigned long long)active->generation,(long long)active->pts_us,(long long)active->received_us,(long long)active->decoded_us,(long long)diagnostic_submitted_us,(long long)now,width,height,rotation,mirror,active->matrix,active->color_range,active->transfer,active->primaries,0u);
}
void PollConversionCompletion(){
 if(inflight){auto status=api.GetFenceStatus(api.unity.device,fence);if(status==VK_NOT_READY)return;if(status!=VK_SUCCESS){Error("poll conversion GPU fence",status);return;}
  completed_token=token;++counters.completes;inflight=false;
  const auto now=NowUs();
  const bool model_pass=conversion==Conversion::Analysis;
  const bool batched_preview=paired_preview_slot>=0;
  if(model_pass){
   ++split_cadence.analysis_completed;
   split_cadence.analysis_decode_wait_us+=cadence.submitted_us-active->decoded_us;
   split_cadence.analysis_fence_wait_us+=now-cadence.submitted_us;
  }
  if(!model_pass||batched_preview){
   ++cadence.complete;cadence.decode_wait_us+=cadence.submitted_us-active->decoded_us;
   cadence.completion_wait_us+=now-cadence.submitted_us;
  }
  bool published=false;
  bool paired_preview_published=false;
  if(production){
   published=PublishConversion(ConversionRing(),model_pass?analysis.published:published_frame,
                               output_slot,target_width,target_height,model_pass?"analysis":"preview");
   if(batched_preview)paired_preview_published=PublishConversion(frame_ring,published_frame,paired_preview_slot,preview_width,preview_height,"preview");
   if(paired_preview_published||(!model_pass&&published))++cadence.preview_published;
   frame_ring.Collect(completed_token);
   analysis.ring.Collect(completed_token);
  }
  LogColorCompletion(target_width,target_height,now);
  if(model_pass&&published){
   ++split_cadence.analysis_published;
   active_analysis_completed_us=now;
   LogSplitFrame("analysis_ready",now);
   if(!batched_preview){
    if(InputSplitSchedule::PreviewExpired(now,active->decoded_us)){
     ++split_cadence.preview_expired;
    }else{
     // HD slots were full at submit. Keep one fresh source image for an idle
     // opportunity, only until its decode-age limit or the next decision.
     ReturnImage(preview_pending);
     split_schedule.OfferPreview(active_sequence,now,active->decoded_us);
     preview_storage.TakeFrom(*active);
     preview_pending=&preview_storage;
     active=nullptr;
     active_sequence=0;
     active_analysis_completed_us=0;
     return;
    }
   }
  }
  if(paired_preview_published||(conversion==Conversion::Preview&&published))LogSplitFrame("preview_ready",now);
  if(conversion==Conversion::PreviewOnly&&published){
   ++split_cadence.preview_only_published;
   LogSplitFrame("preview_only_ready",now);
  }
  ReturnImage(active);retired.notify_all();
 }
}
void Poll(){
 PollConversionCompletion();
 if(inflight||active)return;
 BufferIdentity removed;while(buffer_registry.TakeRemoved(removed))cache.Remove(removed.buffer);
 if(closing)cache.RemoveAll();cache.Collect();if(production){frame_ring.Collect(completed_token);analysis.ring.Collect(completed_token);}
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
 if(closing||paused){ReturnImage(active);return;}
 auto d=api.unity.device;auto* r=static_cast<Imported*>(source_entry->resource);
 const bool model_pass=conversion==Conversion::Analysis;
 bool waited=false;
 // A deferred preview reuses an image whose analysis fence completed.
 // Its original decoder acquire has already been observed.
 if(conversion!=Conversion::Preview&&!gpu_sync.WaitAcquireFdAndOwn(active->acquire_fd,waited)){Error("import actual acquire sync fd");ReturnImage(active);return;}
 if(waited)++counters.positive_waits;else ++counters.already_complete;
 if(api.ResetCommandBuffer(command,0)!=VK_SUCCESS||api.ResetFences(d,1,&fence)!=VK_SUCCESS){Error("reset reusable GPU objects");ReturnImage(active);return;}
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,nullptr,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};if(api.BeginCommandBuffer(command,&begin)!=VK_SUCCESS){Error("begin color command");ReturnImage(active);return;}
 VkImageMemoryBarrier acquire{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};acquire.srcAccessMask=0;acquire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;acquire.oldLayout=VK_IMAGE_LAYOUT_GENERAL;acquire.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;acquire.srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;acquire.dstQueueFamilyIndex=api.unity.queueFamilyIndex;acquire.image=r->image;acquire.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 api.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&acquire);
 const auto record_conversion=[&](VkDescriptorSet descriptor,VkImageView view,uint32_t width,uint32_t height){
  RecordInputConversion(api,d,command,r,descriptor,view,width,height,*active,
      source_entry->contract,rotation,mirror,false);
 };
 api.CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,r->pipeline);
 record_conversion(r->descriptors[0],target_view,target_width,target_height);
 const bool batched_preview=paired_preview_slot>=0;
 if(batched_preview)
  record_conversion(r->descriptors[1],output_views[paired_preview_slot],preview_width,preview_height);
 auto release=acquire;release.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;release.dstAccessMask=0;release.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;release.newLayout=VK_IMAGE_LAYOUT_GENERAL;release.srcQueueFamilyIndex=api.unity.queueFamilyIndex;release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
 VkImageMemoryBarrier output{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};output.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;output.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;output.oldLayout=output.newLayout=VK_IMAGE_LAYOUT_GENERAL;output.srcQueueFamilyIndex=output.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;output.image=target.image;output.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 VkImageMemoryBarrier barriers[]={release,output,output};
 if(batched_preview)barriers[2].image=paired_preview_target.image;
 api.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,batched_preview?3:2,barriers);
 if(api.EndCommandBuffer(command)!=VK_SUCCESS){Error("end color command");ReturnImage(active);return;}
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=waited?1:0;submit.pWaitSemaphores=waited?&wait_semaphore:nullptr;submit.pWaitDstStageMask=waited?&stage:nullptr;submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=model_pass?0:1;submit.pSignalSemaphores=model_pass?nullptr:&release_semaphore;
 auto result=api.QueueSubmit(api.unity.graphicsQueue,1,&submit,fence);if(result!=VK_SUCCESS){Error("serialized Unity queue color submit",result);ReturnImage(active);return;}
 cadence.submitted_us=NowUs();
 active->gpu_submitted=true;inflight=true;++token;++counters.submits;++counters.ownership_acquires;++counters.ownership_returns;cache.Used(*source_entry,token);if(production)ConversionRing().Queue(output_slot,token);
 if(batched_preview){
  frame_ring.Queue(paired_preview_slot,token);
  ++split_cadence.preview_batched;
 }
 // Analysis is not returned to the decoder until this real fence completes.
 if(!model_pass){
  if(!gpu_sync.SignalReleaseFdAndReturnOwnership(active->release_fd)){Error("export actual GPU release sync fd");return;}
  active->CountReleaseFd();++counters.exports;
 }
 if(conversion==Conversion::Preview||batched_preview){
  if(active_sequence==acknowledged_sequence)++split_cadence.preview_started_after_ack;
  else ++split_cadence.preview_without_ack;
  LogSplitFrame("preview_submitted",cadence.submitted_us);
 }
  if(conversion==Conversion::PreviewOnly){
   ++split_cadence.preview_only_submitted;
   LogSplitFrame("preview_only_submitted",cadence.submitted_us);
  }
 if(ShouldLogInputFrame(active_sequence,!production)){
  diagnostic_submitted_us=NowUs();
  const char* wait_kind=conversion==Conversion::Preview?"held_after_analysis_fence":waited?"imported_sync_fd":"actual_minus_one_already_complete";
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","gpu_color_submitted sequence=%llu buffer=%p acquire_wait=%s release_fd=%d ownership=FOREIGN_EXT_to_Unity_to_FOREIGN_EXT serialized_access_queue=1",(unsigned long long)active_sequence,active->buffer,wait_kind,active->release_fd);
 }
}
}
void CloseOutputRings(){
 ReturnImage(preview_pending);
 acknowledged_sequence=0;
 acknowledged_us=0;
 frame_ring.Close();
 analysis.ring.Close();
 published_frame={};
 analysis.published={};
}
static void PauseOutputBindings(){
 paused=true;
 CloseOutputRings();
}
bool BeginInputGpu(HV_InputHandle h){std::lock_guard<std::mutex> lock(mutex);if(owner||active||pending||preview_pending||inflight)return false;owner=h;session_baseline=counters;registry_baseline=buffer_registry.Inspect();terminal_reported=false;production=true;enabled=true;closing=paused=false;target_generation=0;output_slot=-1;published_frame={};analysis.published={};frame_ring.Begin(1);split_cadence={};acknowledged_sequence=0;acknowledged_us=0;return true;}
void CloseInputGpu(HV_InputHandle h){std::lock_guard<std::mutex> lock(mutex);if(!production||owner!=h)return;closing=paused=true;CloseOutputRings();ReturnImage(pending);retire_target=true;}
bool InputGpuRetired(HV_InputHandle h){
 std::lock_guard<std::mutex> lock(mutex);const bool done=!production||owner!=h||(!active&&!pending&&!preview_pending&&!inflight&&!retire_target&&cache.Live()==0);
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
void DetachInputGpu(HV_InputHandle h){std::lock_guard<std::mutex> lock(mutex);if(owner==h&&production){owner=nullptr;production=false;enabled=false;CloseOutputRings();}}
void PrepareInputGpuGeneration(Session* h){std::lock_guard<std::mutex> lock(mutex);if(production&&owner==h&&!h->stop){closing=paused=false;published_frame={};}}
int PollInputGpuMetadata(HV_InputHandle h,uint64_t after,HV_InputFrameInfo* f){std::lock_guard<std::mutex> lock(mutex);if(!h||h!=owner||!f)return HV_INPUT_INVALID;if(closing||published_frame.frame.sequence<=after||!published_frame.frame.sequence)return HV_INPUT_NO_FRAME;*f=published_frame.frame;return 0;}
bool InputGpuProduction(Session* h){std::lock_guard<std::mutex> lock(mutex);return production&&owner==h;}
bool ColorProbeEnabled(){std::lock_guard<std::mutex> lock(mutex);return enabled;}
bool QueueColorImage(AndroidDecodedImage& image){
 std::lock_guard<std::mutex> lock(mutex);
 if(!enabled||closing)return false;
 if(production){
  if(!paused&&target_generation&&image.generation!=target_generation){
   PauseOutputBindings();
   split_cadence={};
  }
  if(!paused&&SplitEnabled()){
   ++split_cadence.decoded;
   if(pending)++split_cadence.decoded_replaced;
  }
  // Keep only the newest decoded image during texture retirement. It also
  // carries the geometry Unity needs to bind the next generation.
  if(pending){ReturnImage(pending);++decoded_drops;++decoded_pending_replaced;}
 }else if(paused||pending||active)return false;
 image_storage.TakeFrom(image);pending=&image_storage;return true;
}
void NotifyRemovedBuffer(AImageReader* reader,AHardwareBuffer* buffer){buffer_registry.Notify(reader,buffer);}
void DrainColorImagesBeforeReaderClose(){std::unique_lock<std::mutex> lock(mutex);if(!enabled)return;closing=true;paused=true;if(production)CloseOutputRings();ReturnImage(pending);retired.wait(lock,[]{return !active;});}
void ConfigureInputCopyProbeEvent();
void ConfigureInputRenderEvent(){ConfigureInputCopyProbeEvent();auto* u=InputUnityVulkan();if(!u)return;UnityVulkanPluginEventConfig config{kUnityVulkanRenderPass_EnsureOutside,kUnityVulkanGraphicsQueueAccess_DontCare,kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission|kUnityVulkanEventConfigFlag_ModifiesCommandBuffersState};u->ConfigureEvent(event_id,&config);}
void InputCopyProbeEvent();
void RetireOutputTargets(){
 if(production){
  auto destroy=[&](auto& views){
   for(auto& view:views)if(view){
    api.DestroyImageView(api.unity.device,view,nullptr);
    view=VK_NULL_HANDLE;
    ++counters.target_views_destroyed;
   }
  };
  destroy(output_views);destroy(analysis.views);
  output_images={};output_textures={};target_view=VK_NULL_HANDLE;
  analysis.images={};analysis.textures={};analysis.generation=0;
  analysis.width=analysis.height=0;
 }
 if(target_view){
  api.DestroyImageView(api.unity.device,target_view,nullptr);
  target_view=VK_NULL_HANDLE;
  ++counters.target_views_destroyed;
 }
 last_target=VK_NULL_HANDLE;unity_texture=nullptr;retire_target=false;
}
bool SelectOutputTarget(AndroidDecodedImage& image, uint64_t sequence){
 auto& ring=ConversionRing();
 const bool model_pass=conversion==Conversion::Analysis;
 auto& textures=model_pass?analysis.textures:output_textures;
 auto& views=model_pass?analysis.views:output_views;
 auto& images=model_pass?analysis.images:output_images;
 ring.Collect(completed_token);
 if(!textures[0])return false;
 if(!ring.CanStartConversion()){
  if(model_pass)++split_cadence.analysis_no_slot;else ++cadence.no_free_slot;
  return false;
 }
 output_slot=ring.Acquire(image.generation,sequence);
 if(output_slot<0){++old_generation_rejections;return false;}
 if(!model_pass&&ring.HasUnobservedPublication())++cadence.start_unobserved;
 unity_texture=textures[output_slot];target_view=views[output_slot];last_target=images[output_slot];target_dirty=!target_view;
 target_width=model_pass?analysis.width:preview_width;
 target_height=model_pass?analysis.height:preview_height;
 return true;
}
static bool PrepareOutputTexture(void* texture, uint32_t width, uint32_t height,
                          UnityVulkanImage& image, VkImageView& image_view,
                          VkImage& view_image, bool dirty, bool optional_preview) {
 const auto fail=[&](const char* stage,VkResult result=VK_ERROR_UNKNOWN){
  if(!optional_preview)Error(stage,result);
  else if(++split_cadence.preview_target_prepare_failures==1)
   __android_log_print(ANDROID_LOG_WARN,"HVInputGate","input_preview_target_unavailable generation=%llu stage=%s result=%d",
                       (unsigned long long)target_generation,stage,int(result));
  return false;
 };
 auto* u=InputUnityVulkan();
 if(!u->AccessTexture(texture,UnityVulkanWholeImage,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,kUnityVulkanResourceAccess_PipelineBarrier,&image)){
  return fail("Unity AccessTexture actual target");
 }
 VkFormatProperties features{};api.GetPhysicalDeviceFormatProperties(api.unity.physicalDevice,image.format,&features);
 if(image.format!=VK_FORMAT_R8G8B8A8_UNORM||!(image.usage&VK_IMAGE_USAGE_STORAGE_BIT)||!(features.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)||image.extent.width!=width||image.extent.height!=height){
  return fail("actual Unity target lacks qualified rgba8 storage contract");
 }
 if(dirty||!image_view||image.image!=view_image){
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  VkResult result=VK_SUCCESS;
  const bool created=ReplaceTargetView(image_view,
   [&](VkImageView old){api.DestroyImageView(api.unity.device,old,nullptr);++counters.target_views_destroyed;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","target_view_replacement_old_retired=true");},
   [&](VkImageView& candidate){
    if(fail_target_view){--fail_target_view;result=VK_ERROR_OUT_OF_HOST_MEMORY;__android_log_print(ANDROID_LOG_INFO,"HVInputGate","target_view_fault_injected=true retired_handle_null=%d pending_lease_returned_on_failure=1",image_view==VK_NULL_HANDLE);return false;}
    result=api.CreateImageView(api.unity.device,&view,nullptr,&candidate);return result==VK_SUCCESS;
   });
  if(!created){view_image=VK_NULL_HANDLE;return fail("create actual Unity storage view",result);}
  view_image=image.image;++counters.target_views_created;
  __android_log_print(ANDROID_LOG_INFO,"HVInputGate","actual_unity_target image=%p format=%u usage=%u storage_features=%u width=%u height=%u",image.image,image.format,image.usage,features.optimalTilingFeatures,width,height);
 }
 return true;
}
bool PrepareConversion(AndroidDecodedImage& image){
 if(!unity_texture)return false;
 if(!InitializeResources()){Error("initialize input GPU resources");return false;}
 InputGpuCapabilities c{};
 if(!ProbeDecodedBuffer(image.buffer,api.unity.physicalDevice,&c)||AdmitDecodedBuffer(c)!=CapabilityFailure::None){
  Error("actual decoded import capabilities");return false;
 }
 if(image.generation!=target_generation){
  if(production)++old_generation_rejections;else Error("target/source generation mismatch");
  return false;
 }
 const bool prepared=PrepareOutputTexture(unity_texture,target_width,target_height,target,target_view,last_target,
                                          target_dirty,conversion==Conversion::Preview||conversion==Conversion::PreviewOnly);
 if(production){
  const bool model_pass=conversion==Conversion::Analysis;
  (model_pass?analysis.views:output_views)[output_slot]=target_view;
  (model_pass?analysis.images:output_images)[output_slot]=last_target;
 }
 if(!prepared)return false;
 target_dirty=false;
 if(paired_preview_slot>=0){
  const int slot=paired_preview_slot;
  if(!PrepareOutputTexture(output_textures[slot],preview_width,preview_height,
                           paired_preview_target,output_views[slot],output_images[slot],false,true)){
   frame_ring.Cancel(slot);
   paired_preview_slot=-1;
  }
 }
 const auto contract=DecodedImageContract(image,c);
 importer.device=api.unity.device; importer.physical_device=api.unity.physicalDevice;
 importer.pipeline_cache=api.unity.pipelineCache;
 importer.source_reader=image.reader;source_entry=cache.Acquire(image.buffer,image.generation,contract);
 if(!source_entry){ReportCacheFailure();return false;}
 return true;
}
void InputRenderEvent(int id){
 if(id==event_id+1){InputCopyProbeEvent();return;}
 if(id!=event_id)return;
 {
  std::lock_guard<std::mutex> lock(mutex);
  if(!enabled)return;
  if(initialized)Poll();
  ++cadence.events;
  if(inflight)++cadence.fence_pending_events;
  if(inflight||active)++cadence.busy;else if(!pending&&!preview_pending)++cadence.empty;
  ReportInputCadence();
  if(retire_target&&!inflight&&!active)RetireOutputTargets();
  if(closing||paused||inflight||active)return;
  const bool pending_current=pending&&(!production||pending->generation==target_generation);
  auto* mailbox=&pending;
  uint64_t sequence=token+1;
  int64_t analysis_completed_us=0;
  bool batch_preview=false;
  conversion=Conversion::Legacy;
  if(production&&SplitEnabled()){
   const bool analysis_ready=pending_current&&analysis.ring.CanStartConversion();
   if(pending_current&&!analysis_ready)++split_cadence.analysis_no_slot;
   const bool preview_slot_free=frame_ring.CanStartConversion();
   if(!preview_slot_free&&(pending_current||preview_pending))++cadence.no_free_slot;
   const auto now=NowUs();
   const auto work=split_schedule.Choose(now,analysis_ready,preview_slot_free,pending_current?pending->decoded_us:0);
   if(preview_pending&&work!=SplitWork::Preview){
    if(split_schedule.Expired(now))++split_cadence.preview_expired;
    else if(!preview_slot_free)++split_cadence.preview_skipped_no_slot;
    else if(work==SplitWork::AnalysisAndPreview)++split_cadence.preview_replaced_by_batch;
    else ++split_cadence.preview_yield_to_analysis;
    ReturnImage(preview_pending);
   }
   switch(work){
   case SplitWork::AnalysisAndPreview:
   case SplitWork::Analysis:
    batch_preview=work==SplitWork::AnalysisAndPreview;
    conversion=Conversion::Analysis;
    sequence=decoded_sequence+1;
    break;
   case SplitWork::PreviewOnly:
    conversion=Conversion::PreviewOnly;
    sequence=decoded_sequence+1;
    break;
   case SplitWork::Preview:
    mailbox=&preview_pending;
    conversion=Conversion::Preview;
    sequence=split_schedule.Sequence();
    analysis_completed_us=split_schedule.CompletedUs();
    break;
   case SplitWork::DiscardPreview:
   case SplitWork::None:
    return;
   }
  }else if(!pending_current){
   return;
  }
  if(production&&!SelectOutputTarget(**mailbox,sequence))return;
  if(batch_preview){
   paired_preview_slot=frame_ring.Acquire((*mailbox)->generation,sequence);
   if(paired_preview_slot<0)++old_generation_rejections;
   else if(frame_ring.HasUnobservedPublication())++cadence.start_unobserved;
  }
  if(!unity_texture)return;
  if(production){active_storage.TakeFrom(**mailbox);active=&active_storage;}else active=*mailbox;
  *mailbox=nullptr;
  if(conversion==Conversion::Preview)split_schedule.Reset();
  active_sequence=sequence;
  active_analysis_completed_us=analysis_completed_us;
  if(!PrepareConversion(*active)){ReturnImage(active);return;}
  if(conversion==Conversion::Analysis||conversion==Conversion::PreviewOnly)decoded_sequence=sequence;
 }
 // Flush Unity's AccessTexture layout transition first; only this callback may submit.
 InputUnityVulkan()->AccessQueue(SubmitOnUnityQueue,event_id,nullptr,true);
}
void ShutdownInputColor(){std::lock_guard<std::mutex> lock(mutex);if(!initialized)return;Poll();if(inflight||active){Error("shutdown before GPU retirement");return;}ReturnImage(pending);ReturnImage(preview_pending);cache.RemoveAll();cache.Collect();auto d=api.unity.device;if(target_view)api.DestroyImageView(d,target_view,nullptr);if(command_pool)api.DestroyCommandPool(d,command_pool,nullptr);if(fence)api.DestroyFence(d,fence,nullptr);if(wait_semaphore)api.DestroySemaphore(d,wait_semaphore,nullptr);if(release_semaphore)api.DestroySemaphore(d,release_semaphore,nullptr);initialized=false;}
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
 for(auto view:hvinput::analysis.views)if(view){hvinput::retire_target=true;return HV_INPUT_BUSY;}
 if(hvinput::analysis.generation&&hvinput::analysis.generation!=generation)return HV_INPUT_INVALID;
 for(int i=0;i<3;++i)hvinput::output_textures[i]=textures[i];
 hvinput::preview_width=w;hvinput::preview_height=he;hvinput::target_generation=generation;
 hvinput::rotation=degrees;hvinput::mirror=mirrored!=0;
 hvinput::paused=hvinput::closing||(hvinput::pending&&hvinput::pending->generation!=generation);
 hvinput::frame_ring.Begin(generation);hvinput::frame_ring.Collect(hvinput::completed_token);hvinput::published_frame={};
 if(hvinput::SplitEnabled()){
  if(hvinput::decoded_sequence<hvinput::token)hvinput::decoded_sequence=hvinput::token;
  hvinput::analysis.ring.Begin(generation);hvinput::analysis.ring.Collect(hvinput::completed_token);hvinput::analysis.published={};
 }
 return HV_INPUT_OK;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_BindGpuAnalysisTargets(HV_InputHandle h,void* const textures[3],uint32_t w,uint32_t he,uint64_t generation){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(!h||h!=hvinput::owner||!hvinput::production||!textures||!textures[0]||!textures[1]||!textures[2]||w!=512||he!=288||!generation)return HV_INPUT_INVALID;
 if(hvinput::active||hvinput::inflight||hvinput::retire_target||hvinput::output_textures[0])return HV_INPUT_BUSY;
 for(int i=0;i<3;++i)hvinput::analysis.textures[i]=textures[i];
 hvinput::analysis.width=w;hvinput::analysis.height=he;hvinput::analysis.generation=generation;
 return HV_INPUT_OK;
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
HV_INPUT_API int HV_INPUT_CALL HV_Input_PollGpuAnalysisFrame(HV_InputHandle h,uint64_t after,HV_InputGpuFrame* f){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(!h||h!=hvinput::owner||!f)return HV_INPUT_INVALID;
 if(hvinput::initialized)hvinput::PollConversionCompletion();
 const auto& output=hvinput::analysis.published;
 if(hvinput::closing||!hvinput::SplitEnabled()||output.frame.sequence<=after)return HV_INPUT_NO_FRAME;
 *f=output;hvinput::analysis.ring.Observe(f->slot,f->frame.sequence);return HV_INPUT_OK;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_ReleaseGpuAnalysisSlot(HV_InputHandle h,uint32_t slot,uint64_t sequence){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(!h||h!=hvinput::owner||slot>=3)return HV_INPUT_INVALID;
 hvinput::analysis.ring.Release(slot,sequence);hvinput::analysis.ring.Collect(hvinput::completed_token);
 return HV_INPUT_OK;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_AcknowledgeGpuAnalysisFrame(HV_InputHandle h,uint64_t generation,uint64_t sequence){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(!h||h!=hvinput::owner)return HV_INPUT_INVALID;
 if(hvinput::closing||hvinput::paused||!hvinput::SplitEnabled()||generation!=hvinput::target_generation||!sequence)
  return HV_INPUT_NO_FRAME;
 for(int i=0;i<3;++i){
  const auto& slot=hvinput::analysis.ring.Slot(i);
  if(slot.sequence!=sequence||slot.generation!=generation||!slot.observed)continue;
  // ACK is retained as telemetry and never schedules or blocks either lane.
  if(sequence>hvinput::acknowledged_sequence){
   hvinput::acknowledged_sequence=sequence;
   hvinput::acknowledged_us=hvinput::NowUs();
   ++hvinput::split_cadence.acknowledgements;
   if(hvinput::ShouldLogInputFrame(sequence,false))__android_log_print(ANDROID_LOG_INFO,"HVInputGate",
    "input_split_frame phase=analysis_ack generation=%llu sequence=%llu monotonic_us=%lld",
    (unsigned long long)generation,(unsigned long long)sequence,(long long)hvinput::acknowledged_us);
  }
  return HV_INPUT_OK;
 }
 return HV_INPUT_NO_FRAME;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuRetired(HV_InputHandle h){return hvinput::InputGpuRetired(h)?1:0;}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuCopyActive(HV_InputHandle h){std::lock_guard<std::mutex> lock(hvinput::mutex);return h==hvinput::owner&&hvinput::active&&hvinput::inflight;}
HV_INPUT_API void HV_INPUT_CALL HV_Input_LogGpuCounters(){HV_Input_LogColorProbeCounters();std::lock_guard<std::mutex> lock(hvinput::mutex);__android_log_print(ANDROID_LOG_INFO,"HVInputGate","input_ring_counters slots_live=%u slot_drops=%llu decoded_drops=%llu old_generation_rejections=%llu",hvinput::frame_ring.Live(),(unsigned long long)hvinput::frame_ring.Drops(),(unsigned long long)hvinput::decoded_drops,(unsigned long long)hvinput::old_generation_rejections);}
}
extern "C" HV_INPUT_API void HV_INPUT_CALL HV_Input_RetireGpuTargets(HV_InputHandle h){std::lock_guard<std::mutex> lock(hvinput::mutex);if(h==hvinput::owner){hvinput::PauseOutputBindings();hvinput::retire_target=true;}}
extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuTargetsRetired(HV_InputHandle h){
 std::lock_guard<std::mutex> lock(hvinput::mutex);
 if(h!=hvinput::owner)return 1;
 if(hvinput::retire_target||hvinput::inflight||hvinput::active)return 0;
 for(auto view:hvinput::output_views)if(view)return 0;
 for(auto view:hvinput::analysis.views)if(view)return 0;
 return 1;
}
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
