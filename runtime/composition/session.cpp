#include "composition/session.h"
#include "composition/region_assignment.h"
#include "services/region_mask.h"
#include "plugins/pipeline/rtmo/rtmo_pipeline.h"
#include "plugins/pipeline/simcc/simcc_pipeline.h"
#include "plugins/pipeline/simcc/topdown_gpu_pipeline.h"
#include "plugins/legacy/legacy_pipeline.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "gpu/android/unity_vulkan_plugin.h"
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace humanvision::runtime {
RuntimeSession::~RuntimeSession(){
 input_.Stop();if(worker_.joinable())worker_.join();if(gpu_)gpu_->Stop();
 std::string ignored;
 if(GpuSourceLeaseCoordinator::Instance().Owns(this))GpuSourceLeaseCoordinator::Instance().End(this,ignored);
 hand_.Stop();body_.Stop();
}
bool RuntimeSession::Start(const std::filesystem::path& root,const std::string& profile,int capacity,std::string& error,
                           HV_QueryPluginV3Fn gpu_pipeline_query,GpuConsumerSource* gpu_test_source){
 if(profile=="android-ncnn-vulkan"){
  if (!gpu_pipeline_query) gpu_pipeline_query=HV_QueryTopDownGpuPipelineV3;
  factory_=std::make_unique<BackendFactory>(std::vector<std::shared_ptr<const PluginModule>>{},false);
  if(!factory_->RegisterV3(HV_QueryNcnnVulkanPluginV3,error))return false;
  if(gpu_pipeline_query&&!factory_->RegisterV3(gpu_pipeline_query,error))return false;
  profile_=ProfileManager(root/"profiles").Resolve(profile,capacity,registry_,ModelPackManager(root/"modelpacks"),error,factory_.get());
  if(!profile_)return false;
  GpuConsumerSource* source=gpu_test_source;
  if(!source){bridge_source_=std::make_unique<BridgeGpuConsumerSource>(gpu::UnityVulkanProducerBridge());source=bridge_source_.get();}
  if(!source||(!gpu_test_source&&!gpu::UnityVulkanProducerBridge())){error="Android Vulkan producer bridge unavailable";return false;}
  const auto asset_root=profile_->gpu_pack->root.u8string();
  HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,capacity,0,
      profile_->gpu_pack->manifest_json.c_str(),asset_root.c_str(),profile_->json.c_str()};
  gpu_=std::make_unique<GpuRuntimeHost>(*source);
  services_.Configure(capacity,nullptr,0,0);
  stats_.struct_size=sizeof(stats_);stats_.api_version=HV_API_VERSION_040;
  gpu_->SetObservationSink(this,[](void* context,const HV_GpuObservationFrameV3& frame,int64_t revision,int64_t capture){
   return static_cast<RuntimeSession*>(context)->AcceptGpuObservation(frame,revision,capture);
  });
  if(!gpu_->Start(profile_->gpu_body,factory_->ServicesV3(),config,error))return false;
  return true;
 }
 for(auto query:{HV_QueryOrtCpuPlugin,HV_QueryRtmoPipeline,HV_QueryTopDownPipeline,HV_QueryHandPipeline})
  if(!registry_.Register(query,error))return false;
 // Optional compiled providers may be absent; profile resolution records fallback.
 std::string optional;registry_.Register(HV_QueryOrtAcceleratedPlugin,optional);registry_.Register(HV_QueryOrtXnnpackPlugin,optional);registry_.Register(HV_QueryOrtQnnPlugin,optional);
 profile_=ProfileManager(root/"profiles").Resolve(profile,capacity,registry_,ModelPackManager(root/"modelpacks"),error);
 if(!profile_)return false;
 factory_=std::make_unique<BackendFactory>(profile_->backends,profile_->allow_backend_fallback);
 auto start=[&](RuntimeHost& host,const PipelineSelection& selection){
  const auto asset_root=selection.pack->root.u8string();
  HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,capacity,0,selection.pack->manifest_json.c_str(),asset_root.c_str(),profile_->json.c_str()};
  return host.Start(selection.plugin,config,factory_->Services(),error);
 };
 if(!start(body_,profile_->body))return false;
 if(profile_->hands_enabled&&!start(hand_,profile_->hands))return false;
 services_.Configure(capacity,nullptr,0,0);
 stats_.struct_size=sizeof(stats_);stats_.api_version=HV_API_VERSION_040;
 worker_=std::thread(&RuntimeSession::Run,this);return true;
}
bool RuntimeSession::Submit(const HV_VideoFrame& frame,std::string& error){
 if(profile_&&profile_->gpu_route){error="GPU profile accepts only Android Vulkan render events";return false;}
 FrameSubmitStatus status;return input_.Submit(frame,status,error);
}
void RuntimeSession::RecordGpuDimensions(uint32_t width,uint32_t height) noexcept {
 if(bridge_source_)bridge_source_->SetDimensions(width,height);
}
void RuntimeSession::SetGpuSourceLeaseActive(bool active) noexcept {
 if(bridge_source_)bridge_source_->SetLeaseActive(active);
}
bool RuntimeSession::SetRegions(const HV_Rect* regions,uint32_t count,int64_t revision,std::string& error){
 if(count>uint32_t(profile_->max_people)||(count&&!regions)){error="Region count must not exceed MaxPeople";return false;}
 for(uint32_t i=0;i<count;++i){auto r=regions[i];if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.x<0||r.y<0||r.width<=0||r.height<=0||r.x+r.width>1.00001F||r.y+r.height>1.00001F){error="Regions must be positive normalized rectangles within the image";return false;}}
 std::lock_guard<std::mutex> lock(mutex_);
 if(revision<=revision_){error="Region revision must increase";return false;}
 region_count_=count;for(uint32_t i=0;i<count;++i)regions_[i]=regions[i];revision_=revision;
 if(gpu_)gpu_->SetRevision(revision);
 services_.Configure(profile_->max_people,regions,count,revision);stats_.region_revision=revision;
 stats_.source_frame_id=stats_.source_timestamp_us=0;return true;
}
bool RuntimeSession::AcceptGpuObservation(const HV_GpuObservationFrameV3& gpu_frame,int64_t revision,int64_t capture_steady_us){
 std::lock_guard<std::mutex> lock(mutex_);
 if(revision!=revision_){error_="GPU observation rejected: stale Region revision";return false;}
 auto frame=gpu_frame.v1;frame.struct_size=sizeof(frame);
 RegionSet regions{regions_,region_count_};
 auto assigned=AssignRegions(frame,regions,revision,services_.Anchors(),
                             gpu_frame.detector_scores,gpu_frame.crop_track_ids);
 if(!CanPublish(assigned,revision_)){error_="GPU observation rejected: Region assignment contract";return false;}
 const auto publish_check_us=std::chrono::duration_cast<std::chrono::microseconds>(
     std::chrono::steady_clock::now().time_since_epoch()).count();
 if(!stats_v2_.CanPublish(frame.source_frame_id,assigned.frame.body_count,
                         capture_steady_us,publish_check_us,frame.source_frame_id,frame.inference_ms) ||
    frame.source_timestamp_us<=stats_.source_timestamp_us){
  error_="GPU observation rejected: duplicate source frame or incompatible native clock";return false;}
 for(uint32_t i=0;i<frame.body_count;++i)
  for(const auto& joint:frame.bodies[i].joints)
   if(joint.valid&&(joint.observation_timestamp_us!=frame.source_timestamp_us||joint.prediction_ms!=0)){
    error_="GPU observation rejected: stale or predicted joint provenance";return false;}
 services_.Observe(assigned.frame,revision,false,assigned.region_indices.data(),
                   assigned.crop_track_ids.data());
 body_sequence_=frame.sequence;
 stats_.body_sequence=frame.sequence;
 stats_.source_frame_id=frame.source_frame_id;stats_.source_timestamp_us=frame.source_timestamp_us;
 stats_.preprocess_ms=frame.preprocess_ms;stats_.inference_ms=frame.inference_ms;
 stats_.postprocess_ms=frame.postprocess_ms;
 const auto published_us=std::chrono::duration_cast<std::chrono::microseconds>(
     std::chrono::steady_clock::now().time_since_epoch()).count();
 // The source capture clock accompanies the host observation separately from
 // Unity's display clock; the host stores the pair atomically with this result.
 const auto pipeline=gpu_->Diagnostics();
 if(!stats_v2_.Publish(frame.source_frame_id,services_.Raw().count,capture_steady_us,
                   published_us,frame.source_frame_id,frame.inference_ms,
                   pipeline.detector_keyframe,pipeline.pose_person_count,true)){
  error_="GPU observation rejected after publication prevalidation";return false;}
 error_.clear();
 return true;
}
void RuntimeSession::Poll(){
 if(gpu_)return;
 HV_ObservationFrameV1 frame{};int64_t revision;
 const bool copied=body_.CopyLatest(frame,revision);
 if(copied&&frame.sequence!=body_sequence_){
  body_sequence_=frame.sequence;
  if(revision==revision_){
   const auto elapsed=frame.source_timestamp_us-stats_.source_timestamp_us;
   if(stats_.source_timestamp_us&&elapsed>0){float fps=1e6F/float(elapsed);stats_.body_fps=stats_.body_fps?stats_.body_fps*.8F+fps*.2F:fps;}
   services_.Observe(frame,revision,true);
   stats_.body_sequence=frame.sequence;
   stats_.source_frame_id=frame.source_frame_id;stats_.source_timestamp_us=frame.source_timestamp_us;
   stats_.preprocess_ms=frame.preprocess_ms;stats_.inference_ms=frame.inference_ms;stats_.postprocess_ms=frame.postprocess_ms;
  }
 }
 if(profile_->hands_enabled&&hand_.CopyLatest(frame,revision)&&frame.sequence!=hand_sequence_){
  hand_sequence_=frame.sequence;if(revision==revision_){services_.MergeHands(frame.hands,frame.hand_count,revision);stats_.hand_sequence=frame.sequence;
   if(last_hand_result_time_&&frame.source_timestamp_us>last_hand_result_time_){float fps=1e6F/float(frame.source_timestamp_us-last_hand_result_time_);stats_.hand_fps=stats_.hand_fps?stats_.hand_fps*.8F+fps*.2F:fps;}
   last_hand_result_time_=frame.source_timestamp_us;}
 }
}
BodySnapshot RuntimeSession::Copy(int64_t sample_time,HV_RuntimeStatsV1& stats){
 std::lock_guard<std::mutex> lock(mutex_);Poll();stats=stats_;stats.dropped_frames=input_.dropped_frames()+body_.DroppedFrames()+hand_.DroppedFrames();
 if(gpu_&&sample_time)stats_v2_.Sample(stats_.source_frame_id);
 return gpu_?services_.Raw():(sample_time?services_.Sample(sample_time):services_.Raw());
}
void RuntimeSession::RecordSourceArrival(bool rate_limited) noexcept {
 source_frames_seen_.fetch_add(1,std::memory_order_relaxed);
 if(rate_limited)source_rate_limited_drops_.fetch_add(1,std::memory_order_relaxed);
}
HV_RuntimeStatsV2 RuntimeSession::StatsV2(){
 std::lock_guard<std::mutex> lock(mutex_);Poll();auto result=stats_v2_.Snapshot(0,capture_provenance_.load(std::memory_order_relaxed));
 result.source_frames_seen=source_frames_seen_.load(std::memory_order_relaxed);
 result.source_rate_limited_drops=source_rate_limited_drops_.load(std::memory_order_relaxed);
 const auto now=std::chrono::duration_cast<std::chrono::microseconds>(
     std::chrono::steady_clock::now().time_since_epoch()).count();
 if(gpu_){
  const auto pipeline=gpu_->Diagnostics();
  result.detector_interval_frames=pipeline.cadence_interval_frames;
  result.detector_attempted=pipeline.detector_attempted;
  result.detector_completed=pipeline.detector_execution_count;
  result.detector_late=pipeline.detector_late;
  result.detector_discarded=pipeline.delayed_detector_discards;
  result.missed_detector_deadlines=pipeline.missed_detector_deadlines;
  result.pose_validation_failures=pipeline.pose_validation_failures;
  result.pose_job_drops=gpu_->PoseJobDrops();
  result.detector_age_ms=pipeline.last_detector_capture_steady_us>0
   ?float(std::max<int64_t>(0,now-pipeline.last_detector_capture_steady_us))/1000.f:0.f;
  result.detector_completion_lag_ms=pipeline.detector_completion_lag_ms;
  HV_AndroidGpuBridgeStatusV1 bridge{sizeof(bridge),HV_ANDROID_GPU_API_V1};
  gpu::GetUnityVulkanProducerStatus(bridge);
  result.copy_path=bridge.copy_path;
  if(auto* producer=gpu::UnityVulkanProducerBridge()){
   result.gpu_capture_submitted=producer->SuccessfulCopies();
   result.gpu_capture_fps=producer->SuccessfulCopyFps(now);
   result.gpu_copy_errors=producer->CopyErrors();
  }
  result.gpu_capture_requested=gpu_capture_requested_.load(std::memory_order_relaxed);
  result.gpu_bridge_no_free_slot_drops=gpu_no_slot_drops_.load(std::memory_order_relaxed);
  result.gpu_bridge_superseded_ready_drops=gpu_->SupersededReadyDrops();
  result.gpu_import_errors=gpu_->ImportErrors();
 }
 return result;
}
std::string RuntimeSession::LastError()const{std::lock_guard<std::mutex> lock(mutex_);if(!error_.empty())return error_;if(gpu_)return gpu_->LastError();auto error=body_.LastError();return error.empty()?hand_.LastError():error;}
std::string RuntimeSession::Diagnostics()const{
 std::lock_guard<std::mutex> lock(mutex_);
 if(profile_&&profile_->gpu_route){
  const auto backend=factory_->SelectionDiagnostics();
  const auto pipeline=gpu_->Diagnostics();
  std::ostringstream out;
  out<<"Profile="<<profile_->id<<"\nPipeline="<<profile_->gpu_body->api.v1.plugin_id
     <<"\nRequested backend=backend.ncnn.vulkan\nActual backend="<<backend.actual
     <<"\nRaw observation bodies="<<services_.Raw().count
     <<"\nDetector cadence interval="<<pipeline.cadence_interval_frames
     <<"\nDetector executions="<<pipeline.detector_execution_count
     <<"\nMissed detector deadlines="<<pipeline.missed_detector_deadlines
     <<"\nDelayed detector discards="<<pipeline.delayed_detector_discards
     <<"\nPose validation failures="<<pipeline.pose_validation_failures
     <<"\nGPU worker error="<<gpu_->LastError();
  return out.str();
 }
 const auto now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
 const auto sample=services_.Diagnostics(now);const auto pipeline=body_.Diagnostics();const auto backend=factory_->SelectionDiagnostics();
 const auto state=sample.state==BodySampleState::Predicted?"Predicted":sample.state==BodySampleState::Held?"Held":"Stale";
 const double age=stats_.source_timestamp_us>0&&now>=stats_.source_timestamp_us?double(now-stats_.source_timestamp_us)/1000.:-1.;
 std::ostringstream out;out<<std::fixed<<std::setprecision(1)
  <<"Profile="<<profile_->id<<"\nPipeline="<<profile_->body.plugin->api.plugin_id
  <<"\nRequested backend="<<backend.requested<<"\nActual backend="<<backend.actual
  <<"\nRaw observation bodies="<<services_.Raw().count<<"\nTracked bodies="<<sample.tracked_body_count
  <<"\nSampled bodies="<<sample.sampled_body_count<<"\nRaw body FPS="<<stats_.body_fps
  <<"\nBody pre/infer/post ms="<<stats_.preprocess_ms<<" / "<<stats_.inference_ms<<" / "<<stats_.postprocess_ms
  <<"\nResult age ms="<<age<<"\nObservation period EWMA ms="<<sample.observation_period_ewma_ms
  <<"\nRender hold ms="<<sample.render_hold_ms<<"\nSample age ms="<<sample.sample_age_ms
  <<"\nSample state="<<state<<"\nDropped input frames="<<input_.dropped_frames()
  <<"\nDropped body frames="<<body_.DroppedFrames()<<"\nHands enabled="<<(profile_->hands_enabled?"true":"false");
 const std::string pipeline_id=profile_->body.plugin->api.plugin_id;
 if(pipeline_id=="pipeline.rtmo")out<<"\nRaw detections="<<pipeline.raw_detection_count
  <<"\nAccepted detections="<<pipeline.accepted_detection_count<<"\nMax detection score="<<pipeline.max_detection_score;
 if(pipeline_id=="pipeline.topdown")out<<"\nDetector inference ms="<<pipeline.detector_inference_ms
  <<"\nDetector executions="<<pipeline.detector_execution_count<<"\nDetector FPS="<<pipeline.detector_fps
  <<"\nPose inference total ms="<<pipeline.pose_inference_total_ms<<"\nPose person count="<<pipeline.pose_person_count;
 const auto providers=factory_->Diagnostics();if(!providers.empty())out<<"\nBackend sessions="<<providers;
 if(!profile_->fallback_reason.empty())out<<"\nBackend selection notes="<<profile_->fallback_reason;
 return out.str();
}
void RuntimeSession::Run(){
 FrameBuffer frame,masked;
 while(input_.WaitTake(frame)){
  try{
   std::array<HV_Rect,8> regions;uint32_t count;int64_t revision;
   {std::lock_guard<std::mutex> lock(mutex_);regions=regions_;count=region_count_;revision=revision_;}
   HV_VideoFrame source{sizeof(source),frame.width,frame.height,frame.stride_bytes,frame.pixel_format,frame.frame_id,frame.timestamp_us,frame.bytes.data(),int32_t(frame.bytes.size())};
   std::string error;
   if(!MaskRegions(source,regions.data(),count,masked,error)){std::lock_guard<std::mutex> lock(mutex_);error_=error;continue;}
   source.data=masked.bytes.data();
   std::lock_guard<std::mutex> lock(mutex_);if(revision!=revision_)continue;Poll();
   if(!last_body_submit_||source.timestamp_us-last_body_submit_>=1000000/profile_->body_fps){
    if(body_.Submit(source,nullptr,0,revision,error))last_body_submit_=source.timestamp_us;else error_=error;
   }
   if(profile_->hands_enabled&&!hand_.Busy()){
    HV_RegionOfInterestV1 rois[2]{};auto size=services_.HandRequests(rois,2,source.timestamp_us,1000000/profile_->hand_fps);
    if(size&&!hand_.Submit(source,rois,size,revision,error))error_=error;
   }
  }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex_);error_=e.what();}
  catch(...){std::lock_guard<std::mutex> lock(mutex_);error_="Runtime coordinator failed";}
 }
}
}
