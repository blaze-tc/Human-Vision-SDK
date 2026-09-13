#include "composition/session.h"
#include "services/region_mask.h"
#include "plugins/pipeline/rtmo/rtmo_pipeline.h"
#include "plugins/pipeline/simcc/simcc_pipeline.h"
#include "plugins/legacy/legacy_pipeline.h"
#include "plugins/backend/ort/ort_plugin.h"
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace humanvision::runtime {
RuntimeSession::~RuntimeSession(){input_.Stop();if(worker_.joinable())worker_.join();hand_.Stop();body_.Stop();}
bool RuntimeSession::Start(const std::filesystem::path& root,const std::string& profile,int capacity,std::string& error){
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
bool RuntimeSession::Submit(const HV_VideoFrame& frame,std::string& error){FrameSubmitStatus status;return input_.Submit(frame,status,error);}
bool RuntimeSession::SetRegions(const HV_Rect* regions,uint32_t count,int64_t revision,std::string& error){
 if(count>uint32_t(profile_->max_people)||(count&&!regions)){error="Region count must not exceed MaxPeople";return false;}
 for(uint32_t i=0;i<count;++i){auto r=regions[i];if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.x<0||r.y<0||r.width<=0||r.height<=0||r.x+r.width>1.00001F||r.y+r.height>1.00001F){error="Regions must be positive normalized rectangles within the image";return false;}}
 std::lock_guard<std::mutex> lock(mutex_);
 if(revision<=revision_){error="Region revision must increase";return false;}
 region_count_=count;for(uint32_t i=0;i<count;++i)regions_[i]=regions[i];revision_=revision;
 services_.Configure(profile_->max_people,regions,count,revision);stats_.region_revision=revision;
 stats_.source_frame_id=stats_.source_timestamp_us=0;return true;
}
void RuntimeSession::Poll(){
 HV_ObservationFrameV1 frame{};int64_t revision;
 if(body_.CopyLatest(frame,revision)&&frame.sequence!=body_sequence_){
  body_sequence_=frame.sequence;
  if(revision==revision_){
   const auto elapsed=frame.source_timestamp_us-stats_.source_timestamp_us;
   if(stats_.source_timestamp_us&&elapsed>0){float fps=1e6F/float(elapsed);stats_.body_fps=stats_.body_fps?stats_.body_fps*.8F+fps*.2F:fps;}
   services_.Observe(frame,revision);stats_.body_sequence=frame.sequence;
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
 return sample_time?services_.Sample(sample_time):services_.Raw();
}
std::string RuntimeSession::LastError()const{std::lock_guard<std::mutex> lock(mutex_);if(!error_.empty())return error_;auto error=body_.LastError();return error.empty()?hand_.LastError():error;}
std::string RuntimeSession::Diagnostics()const{
 std::lock_guard<std::mutex> lock(mutex_);
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
