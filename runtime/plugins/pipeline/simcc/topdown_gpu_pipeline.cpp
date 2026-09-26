#include "plugins/pipeline/simcc/topdown_gpu_pipeline.h"
#include "plugins/pipeline/simcc/detector_cadence.h"
#include "plugins/pipeline/simcc/gpu_track_crops.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "common/pipeline_diagnostics.h"
#include "models/rtmpose/pose_affine.h"
#include "gpu/android/unity_vulkan_bridge.h"
#include "gpu/android/unity_vulkan_plugin.h"
#include "json/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#if defined(__ANDROID__)
#include <poll.h>
#endif

namespace humanvision::runtime {
bool BuildGpuPoseCrop(const Detection& box,HV_GpuImageTransformV1& transform,
                      HV_Rect& inverse_rect){
    PoseAffineTransform affine{};std::string error;
    if(!BuildPoseAffine(box,affine,error,192,256))return false;
    inverse_rect={affine.center_x-affine.scale_width*.5f,
        affine.center_y-affine.scale_height*.5f,affine.scale_width,affine.scale_height};
    // GpuPreprocess samples output pixel centers; offset the source
    // rectangle so integer model coordinates match BuildPoseAffine.
    transform.source_rect_px={
        inverse_rect.x+.5f-affine.scale_width/(2.f*192.f),
        inverse_rect.y+.5f-affine.scale_height/(2.f*256.f),
        affine.scale_width,affine.scale_height};
    return true;
}
}

namespace {
using namespace humanvision;
using namespace humanvision::runtime;
using Clock = std::chrono::steady_clock;
void Error(HV_ErrorBufferV1* out, const char* message) {
    if (!out || !out->data || !out->capacity) return;
    const size_t n = std::min(std::strlen(message), static_cast<size_t>(out->capacity-1));
    std::memcpy(out->data,message,n); out->data[n]=0;
}
bool WaitProducer(void*, gpu::SyncFd& fd) noexcept {
    if (!fd.HasPayload()) return false;
    if (fd.State()==gpu::SyncPayloadState::AlreadySignaled) return true;
#if defined(__ANDROID__)
    pollfd descriptor{fd.Get(),POLLIN,0};
    return poll(&descriptor,1,5000)==1 && (descriptor.revents&POLLIN)!=0;
#else
    return false;
#endif
}
struct Backend {
    const HV_GpuBackendApiV2* api=nullptr;
    void* instance=nullptr;
    HV_GpuImageTransformV1 transform{};
};
bool Tensor(const HV_TensorViewV1& view,const char* name,int rows,int cols) {
    return view.name && std::strcmp(view.name,name)==0 && view.element_type==1 &&
        view.data && view.rank==3 && view.dimensions[0]==1 &&
        view.dimensions[1]==rows && view.dimensions[2]==cols &&
        view.byte_count==uint64_t(rows)*cols*sizeof(float);
}
float IoU(const Detection& a,const Detection& b) {
    const float w=std::max(0.f,std::min(a.x2,b.x2)-std::max(a.x1,b.x1));
    const float h=std::max(0.f,std::min(a.y2,b.y2)-std::max(a.y1,b.y1));
    const float aa=(a.x2-a.x1)*(a.y2-a.y1),bb=(b.x2-b.x1)*(b.y2-b.y1);
    return w*h/(aa+bb-w*h+1e-9f);
}
bool DecodeDetector(const HV_TensorViewV1* views,uint32_t count,int width,int height,
                    std::vector<Detection>& candidates,std::vector<Detection>& selected) {
    const HV_TensorViewV1 *cls=nullptr,*bbox=nullptr;
    for(uint32_t i=0;i<count;++i){if(Tensor(views[i],"cls",2100,1))cls=&views[i];
        if(Tensor(views[i],"bbox",2100,4))bbox=&views[i];}
    if(!cls||!bbox)return false;
    const auto* scores=static_cast<const float*>(cls->data);
    const auto* distances=static_cast<const float*>(bbox->data);
    candidates.clear();selected.clear();
    // The V3 AHB preprocessor resizes the declared full source rectangle to
    // 320x320. Reverse that exact transform, including non-square sources.
    const float scale_x=320.f/width,scale_y=320.f/height;
    for(int i=0;i<2100;++i){
        const float raw=scores[i],score=1.f/(1.f+std::exp(-raw));
        if(!std::isfinite(raw)||score<.35f)continue;
        const int level=i<1600?0:i<2000?1:2;
        const int local=i-(level==0?0:level==1?1600:2000);
        const int grid=level==0?40:level==1?20:10;
        const float stride=static_cast<float>(8<<level);
        const float cx=(local%grid+.5f)*stride,cy=(local/grid+.5f)*stride;
        const float* d=distances+i*4;
        if(!std::all_of(d,d+4,[](float v){return std::isfinite(v)&&v>=0;}))return false;
        Detection box{std::clamp((cx-d[0])/scale_x,0.f,float(width)),
                      std::clamp((cy-d[1])/scale_y,0.f,float(height)),
                      std::clamp((cx+d[2])/scale_x,0.f,float(width)),
                      std::clamp((cy+d[3])/scale_y,0.f,float(height)),score};
        if(box.x2>box.x1&&box.y2>box.y1)candidates.push_back(box);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    for(const auto& box:candidates){
        bool keep=true;for(const auto& old:selected)if(IoU(box,old)>.6f){keep=false;break;}
        if(keep)selected.push_back(box);
        if(selected.size()==8)break;
    }
    return true;
}
constexpr int kMapping[22]={HV_CANONICAL_NOSE,HV_CANONICAL_EYE_LEFT,HV_CANONICAL_EYE_RIGHT,
    HV_CANONICAL_EAR_LEFT,HV_CANONICAL_EAR_RIGHT,HV_CANONICAL_SHOULDER_LEFT,
    HV_CANONICAL_SHOULDER_RIGHT,HV_CANONICAL_ELBOW_LEFT,HV_CANONICAL_ELBOW_RIGHT,
    HV_CANONICAL_WRIST_LEFT,HV_CANONICAL_WRIST_RIGHT,HV_CANONICAL_HIP_LEFT,
    HV_CANONICAL_HIP_RIGHT,HV_CANONICAL_KNEE_LEFT,HV_CANONICAL_KNEE_RIGHT,
    HV_CANONICAL_ANKLE_LEFT,HV_CANONICAL_ANKLE_RIGHT,HV_CANONICAL_HEAD,
    HV_CANONICAL_NECK,HV_CANONICAL_PELVIS,HV_CANONICAL_FOOT_LEFT,
    HV_CANONICAL_FOOT_RIGHT};
bool DecodePose(const HV_TensorViewV1* views,uint32_t count,const HV_Rect& rect,
                const HV_GpuFrameRefV1& frame,std::vector<HV_CanonicalJointV1>& joints,
                HV_BodyObservationV1& body) {
    const HV_TensorViewV1 *x=nullptr,*y=nullptr;
    for(uint32_t i=0;i<count;++i){if(Tensor(views[i],"simcc_x",26,384))x=&views[i];
        if(Tensor(views[i],"simcc_y",26,512))y=&views[i];}
    if(!x||!y)return false;
    const auto* xs=static_cast<const float*>(x->data);const auto* ys=static_cast<const float*>(y->data);
    joints.clear();int valid=0;
    for(int k=0;k<26;++k){
        const auto* xb=xs+k*384;const auto* yb=ys+k*512;
        if(!std::all_of(xb,xb+384,[](float v){return std::isfinite(v);})||
           !std::all_of(yb,yb+512,[](float v){return std::isfinite(v);}))return false;
        const auto* xp=std::max_element(xb,xb+384);const auto* yp=std::max_element(yb,yb+512);
        const float px=rect.x+float(xp-xb)*rect.width/384.f;
        const float py=rect.y+float(yp-yb)*rect.height/512.f;
        HV_CanonicalJointV1 joint{};joint.struct_size=sizeof(joint);
        joint.api_version=HV_API_VERSION_040;joint.x_px=px;joint.y_px=py;
        joint.x_norm=px/frame.width;joint.y_norm=py/frame.height;
        joint.confidence=std::min(*xp,*yp);
        joint.valid=joint.confidence>=.3f&&px>=0&&py>=0&&px<frame.width&&py<frame.height;
        joint.observation_timestamp_us=frame.timestamp_us;
        if(k<17&&joint.valid)++valid;
        joints.push_back(joint);
    }
    if(valid<5)return false;
    body={};body.struct_size=sizeof(body);body.api_version=HV_PLUGIN_API_V1;
    for(auto& joint:body.joints){joint.struct_size=sizeof(joint);joint.api_version=HV_API_VERSION_040;}
    float confidence=1.f;for(int k=0;k<22;++k){body.joints[kMapping[k]]=joints[k];
        if(k<17&&joints[k].valid)confidence=std::min(confidence,joints[k].confidence);}
    body.confidence=confidence;
    return true;
}
HV_GpuImageTransformV1 Transform(const ncnn_backend::InputContract& c) {
    HV_GpuImageTransformV1 out{};out.struct_size=sizeof(out);out.api_version=HV_GPU_FRAME_API_V1;
    out.output_width=c.width;out.output_height=c.height;out.output_type=c.output_type;
    out.output_elempack=c.output_elempack;out.channel_order=c.channel_order;
    for(int i=0;i<3;++i){out.mean[i]=c.mean[i];out.norm[i]=c.norm[i];}
    return out;
}
struct Instance {
    HV_HostServicesV3 services{};
    std::string manifest,root;
    ncnn_backend::InputContract detector_contract,pose_contract;
    Backend detector,pose;
    DetectorCadenceScheduler cadence;
    std::unique_ptr<GpuTrackCrops> crops;
    std::vector<HV_CanonicalJointV1> joints;
    std::vector<Detection> candidates,selected,finished_boxes;
    std::thread detector_worker;
    std::mutex mutex;std::condition_variable wake;
    HV_GpuPreparedRefV1 pending{};DetectorResultMeta pending_meta{},finished_meta{};
    bool running=true,job_ready=false,result_ready=false,fatal=false;
    char fatal_message[256]{};
    uint64_t detector_executions=0,delayed_discards=0,pose_failures=0;
    uint64_t generation=0;
    int64_t region_revision=0;
    int64_t last_processed_frame=-1;
    std::size_t last_selected_count=0;
    int width=0,height=0,capacity=0,interval_frames=0;
    int64_t max_gap_us=0;
    Instance(const HV_HostServicesV3& host,int cap,int interval,int64_t gap)
      :services(host),cadence(interval,gap),capacity(cap),interval_frames(interval),max_gap_us(gap){
        joints.reserve(26);candidates.reserve(2100);selected.reserve(8);finished_boxes.reserve(8);
    }
    ~Instance(){
        StopWorker();ReleaseBackends();
    }
    void StopWorker(){
        {std::lock_guard<std::mutex> lock(mutex);running=false;wake.notify_all();}
        if(detector_worker.joinable())detector_worker.join();
    }
    void ReleaseBackends(){
        if(detector.instance)services.release_gpu_backend_v3(services.v2.v1.context,detector.api,detector.instance);
        if(pose.instance)services.release_gpu_backend_v3(services.v2.v1.context,pose.api,pose.instance);
        detector={};pose={};
    }
    bool StartBackend(Backend& target,const char* role,const HV_GpuDeviceContextV1& device,HV_ErrorBufferV1* error){
        auto json=nlohmann::json::parse(manifest);json["active_role"]=role;
        const auto role_manifest=json.dump();
        HV_GpuBackendConfigV1 config{sizeof(config),HV_GPU_FRAME_API_V1,
            role_manifest.c_str(),root.c_str(),"backend.ncnn.vulkan"};
        return services.create_gpu_backend_v3(services.v2.v1.context,&config,&device,
            &target.api,&target.instance,error)==HV_OK&&target.api&&target.instance;
    }
    bool Initialize(const HV_GpuFrameRefV1& frame,int64_t revision,HV_ErrorBufferV1* error){
        if(detector.instance){
            if(frame.generation==generation&&frame.width==width&&frame.height==height){
                if(revision==region_revision)return true;
                // Drain the old detector job before replacing its association state.
                // This runs on the native processing worker, never the render callback.
                StopWorker();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    cadence=DetectorCadenceScheduler(interval_frames,max_gap_us);
                    job_ready=result_ready=fatal=false;pending={};finished_boxes.clear();
                    fatal_message[0]=0;running=true;
                }
                region_revision=revision;
                last_selected_count=0;
                crops=std::make_unique<GpuTrackCrops>(capacity,width,height,region_revision,generation);
                detector_worker=std::thread([this]{DetectorLoop();});
                return true;
            }
            StopWorker();ReleaseBackends();
            crops.reset();
            last_processed_frame=-1;
            last_selected_count=0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                cadence=DetectorCadenceScheduler(interval_frames,max_gap_us);
                job_ready=result_ready=fatal=false;pending={};finished_boxes.clear();
                fatal_message[0]=0;running=true;
            }
        }
        HV_GpuDeviceContextV1 device{};device.struct_size=sizeof(device);device.api_version=HV_GPU_FRAME_API_V1;
#if defined(__ANDROID__)
        gpu::VulkanDeviceContext unity{};
        if(!gpu::UnityVulkanProducerContext(unity)){Error(error,"Unity Vulkan producer context unavailable");return false;}
        ncnn_backend::HostContext host{};host.unity_device=unity;host.bridge=gpu::UnityVulkanProducerBridge();
        const auto identity=gpu::QueryDeviceIdentity(unity);
        std::copy(identity.device_uuid.begin(),identity.device_uuid.end(),device.device_uuid);
        std::copy(identity.driver_uuid.begin(),identity.driver_uuid.end(),device.driver_uuid);
        device.host_context=&host;
#endif
        if(!StartBackend(detector,"detector",device,error)||
           !StartBackend(pose,"body",device,error))return false;
        detector.transform=Transform(detector_contract);pose.transform=Transform(pose_contract);
        generation=frame.generation;width=frame.width;height=frame.height;
        region_revision=revision;
        crops=std::make_unique<GpuTrackCrops>(capacity,width,height,region_revision,generation);
        detector_worker=std::thread([this]{DetectorLoop();});
        return true;
    }
    void DetectorLoop(){
        std::vector<Detection> local_candidates,local_selected;
        local_candidates.reserve(2100);local_selected.reserve(8);
        for(;;){
            HV_GpuPreparedRefV1 ref{};DetectorResultMeta meta{};
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[this]{return !running||job_ready;});
             if(!running&&!job_ready)break;ref=pending;meta=pending_meta;
             job_ready=false;}
            HV_TensorViewV1 views[2]{};uint32_t count=0;char message[256]{};
            HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,message,sizeof(message)};
            const auto status=detector.api->prepared->run_prepared(detector.instance,&ref,views,2,&count,&error);
            const bool decoded=status==HV_OK&&DecodeDetector(views,count,width,height,local_candidates,local_selected);
            // The paired native capture clock includes time spent in Unity/AHB
            // queues before prepare_image. Keep published timestamps in the
            // original Unity clock domain.
            const auto now_us=std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now().time_since_epoch()).count();
            meta.arrival_us=meta.capture_us+std::max<int64_t>(0,now_us-meta.capture_steady_us);
            {std::lock_guard<std::mutex> lock(mutex);
             cadence.FinishDetector(ref.frame_id,ref.generation);
             if(!decoded){fatal=true;const char* reason=message[0]?message:"Detector output contract invalid";
                 const size_t length=std::min(std::strlen(reason),sizeof(fatal_message)-1);
                 std::memcpy(fatal_message,reason,length);fatal_message[length]=0;}
             else{finished_meta=meta;finished_boxes.swap(local_selected);result_ready=true;
                 ++detector_executions;}
            }
        }
    }
};

struct PendingDetectorWake {
    Instance& owner;
    bool armed=false;
    void Notify() noexcept {if(armed){armed=false;owner.wake.notify_one();}}
    ~PendingDetectorWake(){Notify();}
};

HV_Result HV_CALL Create(const HV_PipelineConfigV1* config,const HV_HostServicesV3* host,
                         void** out,HV_ErrorBufferV1* error){
    if(out)*out=nullptr;
    if(!config||!host||!out||host->v2.v1.struct_size<sizeof(*host)||
       host->v2.v1.api_version!=HV_PLUGIN_API_V1||config->struct_size<sizeof(*config)||
       config->api_version!=HV_PLUGIN_API_V1||!config->model_manifest_utf8||
       !config->asset_root_utf8||!config->options_utf8||config->max_bodies<1||
       config->max_bodies>8||!host->create_gpu_backend_v3||!host->release_gpu_backend_v3)
        return HV_ERR_INVALID_ARGUMENT;
    try{
        const auto profile=nlohmann::json::parse(config->options_utf8);
        const auto d=profile.at("detector");
        const int interval=d.at("cadence_interval_frames").get<int>();
        const int64_t gap=d.at("max_capture_gap_us").get<int64_t>();
        auto instance=std::make_unique<Instance>(*host,config->max_bodies,interval,gap);
        auto manifest=nlohmann::json::parse(config->model_manifest_utf8);
        if(manifest.at("schema_version")!=2)throw std::runtime_error("V3 TopDown requires schema-2 ModelPack");
        bool have_detector=false,have_pose=false;
        for(const auto& model:manifest.at("models")){
            const auto role=model.at("role").get<std::string>();
            auto input=model.at("input_contract");input["output_blobs"]=model.at("output_contract").at("output_blobs");
            std::string reason;ncnn_backend::InputContract contract;
            if(!ncnn_backend::ParseInputContract(input,contract,reason))throw std::runtime_error(reason);
            if(role=="detector"){instance->detector_contract=contract;have_detector=true;}
            if(role=="body"){instance->pose_contract=contract;have_pose=true;}
        }
        if(!have_detector||!have_pose||instance->detector_contract.width!=320||
           instance->detector_contract.height!=320||instance->pose_contract.width!=192||
           instance->pose_contract.height!=256)throw std::runtime_error("V3 TopDown model roles or shapes missing");
        instance->manifest=std::move(manifest).dump();instance->root=config->asset_root_utf8;
        if(!RegisterPipelineDiagnostics(instance.get()))
            throw std::runtime_error("V3 pipeline diagnostics capacity exhausted");
        *out=instance.release();return HV_OK;
    }catch(const std::exception& e){Error(error,e.what());return HV_ERR_MODEL_LOAD;}
}
void HV_CALL Destroy(void* instance){UnregisterPipelineDiagnostics(instance);delete static_cast<Instance*>(instance);}
HV_Result HV_CALL Process(void* opaque,const HV_GpuFrameRefV1* frame,
                          HV_ObservationFrameV1* output,HV_ErrorBufferV1* error){
    if(!opaque||!frame||!output||frame->struct_size<sizeof(*frame)||
       frame->api_version!=HV_GPU_FRAME_API_V1||!frame->opaque_slot||
       frame->width<1||frame->height<1||!frame->generation||
       output->struct_size<sizeof(*output))return HV_ERR_INVALID_ARGUMENT;
    auto& self=*static_cast<Instance*>(opaque);
    const auto revision=frame->struct_size>=sizeof(HV_GpuFrameRefRegionV1)
        ? reinterpret_cast<const HV_GpuFrameRefRegionV1*>(frame)->region_revision : 0;
    const auto paired_capture=frame->struct_size>=sizeof(HV_GpuFrameRefRegionV1)
        ? reinterpret_cast<const HV_GpuFrameRefRegionV1*>(frame)->capture_steady_us : 0;
    if(revision<0)return HV_ERR_INVALID_ARGUMENT;
    output->body_count=output->hand_count=0;
    output->preprocess_ms=output->inference_ms=output->postprocess_ms=0;
    output->source_frame_id=frame->frame_id;
    output->source_timestamp_us=frame->timestamp_us;
    output->width=frame->width;output->height=frame->height;
    PendingDetectorWake wake{self};
    try{
        const auto begin=Clock::now();
        if(!self.Initialize(*frame,revision,error))return HV_ERR_NOT_INITIALIZED;
        if(frame->frame_id<=self.last_processed_frame){
            Error(error,"Duplicate or stale GPU source frame");return HV_ERR_INVALID_ARGUMENT;
        }
        self.last_processed_frame=frame->frame_id;
        DetectorCadenceTrigger trigger=DetectorCadenceTrigger::None;
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            if(self.fatal){Error(error,self.fatal_message);return HV_ERR_INTERNAL;}
            if(self.result_ready){
                self.finished_meta.arrival_us=std::max(self.finished_meta.arrival_us,frame->timestamp_us);
                if(!self.crops->ApplyDetection(self.finished_meta,self.finished_boxes))
                    ++self.delayed_discards;
                self.result_ready=false;self.finished_boxes.clear();}
        }
        const auto& next=self.crops->NextCrops(frame->frame_id,frame->timestamp_us);
        if(self.crops->NeedsReacquisition())trigger=DetectorCadenceTrigger::NoTracks;
        else if(self.last_selected_count&&next.size()!=self.last_selected_count)
            trigger=DetectorCadenceTrigger::BodyCountChanged;
        else for(const auto& crop:next){
            const float margin_x=frame->width*.02f,margin_y=frame->height*.02f;
            if(crop.box.x1<=margin_x||crop.box.y1<=margin_y||
               crop.box.x2>=frame->width-margin_x||crop.box.y2>=frame->height-margin_y){
                trigger=DetectorCadenceTrigger::CropAtEdge;break;
            }
        }
        self.last_selected_count=next.size();
        bool prepared=false;
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            if(self.cadence.ShouldCapture(frame->frame_id,frame->timestamp_us,trigger)){
                if(self.cadence.Busy()&&self.job_ready){
                    const auto status=self.detector.api->prepared->discard_prepared(
                        self.detector.instance,&self.pending,error);
                    if(status!=HV_OK)return status;
                    self.cadence.CancelUnstarted(self.pending.frame_id,self.pending.generation);
                    self.pending={};self.job_ready=false;
                }
                if(!self.cadence.Busy()&&self.cadence.AdmitPrepared(
                    frame->frame_id,frame->timestamp_us,frame->generation))prepared=true;
            }
        }
        if(prepared){
            self.detector.transform.source_rect_px={0,0,float(frame->width),float(frame->height)};
            HV_GpuPreparedRefV1 ref{sizeof(ref),HV_GPU_PREPARED_API_V1};
            const auto native_now=std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now().time_since_epoch()).count();
            const auto capture_steady=paired_capture>0?paired_capture:frame->timestamp_us;
            if(capture_steady<=0||capture_steady>native_now+1000000||
               (!paired_capture&&native_now-capture_steady>5000000)){
                std::lock_guard<std::mutex> lock(self.mutex);
                self.cadence.CancelGeneration(frame->generation);self.fatal=true;
                Error(error,"GPU capture clock anchor missing or invalid");
                std::strcpy(self.fatal_message,"GPU capture clock anchor missing or invalid");
                return HV_ERR_INVALID_ARGUMENT;
            }
            const auto status=self.detector.api->prepared->prepare_image(self.detector.instance,
                frame,&self.detector.transform,&ref,error);
            if(status!=HV_OK){
                std::lock_guard<std::mutex> lock(self.mutex);
                self.cadence.CancelGeneration(frame->generation);
                self.fatal=true;
                const char* reason=error&&error->data&&error->data[0]?error->data:
                    "Detector prepare_image failed; ncnn Vulkan session stopped";
                const auto length=std::min(std::strlen(reason),sizeof(self.fatal_message)-1);
                std::memcpy(self.fatal_message,reason,length);self.fatal_message[length]=0;
                return status;
            }
            std::lock_guard<std::mutex> lock(self.mutex);
            self.pending=ref;self.pending_meta={frame->frame_id,frame->timestamp_us,0,revision,
                frame->generation,capture_steady};
            self.job_ready=true;
            wake.armed=true;
        }
        bool posed=false;
        for(const auto& crop:next){
            const auto& box=crop.box;
            const HV_Rect rect{box.x1,box.y1,box.x2-box.x1,box.y2-box.y1};
            HV_Rect decode_rect{};
            if(!BuildGpuPoseCrop(box,self.pose.transform,decode_rect)){
                self.joints.clear();self.crops->ApplyPose(frame->frame_id,frame->timestamp_us,
                    crop.track_id,self.joints,false);continue;
            }
            HV_TensorViewV1 views[2]{};uint32_t count=0;
            const auto pose_begin=Clock::now();
            const auto status=self.pose.api->v1.run_image(self.pose.instance,frame,
                &self.pose.transform,views,2,&count,error);
            if(status!=HV_OK)return status;
            output->inference_ms+=std::chrono::duration<float,std::milli>(Clock::now()-pose_begin).count();
            posed=true;
            HV_BodyObservationV1 body{};
            const bool valid=DecodePose(views,count,decode_rect,*frame,self.joints,body);
            const bool accepted=self.crops->ApplyPose(frame->frame_id,frame->timestamp_us,
                crop.track_id,self.joints,valid);
            if(!accepted){++self.pose_failures;continue;}
            body.bbox_px=rect;output->bodies[output->body_count++]=body;
        }
        if(posed){
            auto& consumer=*static_cast<gpu::ConsumerFrame*>(frame->opaque_slot);
            std::string reason;
            if(!gpu::CompleteGpuRole(consumer,true,reason)){Error(error,reason.c_str());return HV_ERR_INTERNAL;}
        }else if(!prepared){
#if defined(__ANDROID__)
            auto* bridge=gpu::UnityVulkanProducerBridge();
            auto& consumer=*static_cast<gpu::ConsumerFrame*>(frame->opaque_slot);
            if(!bridge||gpu::RetireUnsubmittedConsumer(*bridge,consumer,WaitProducer,nullptr)!=gpu::SlotResult::Ok){
                Error(error,"Cannot retire empty GPU observation");return HV_ERR_INTERNAL;}
#endif
        }
        wake.Notify();
        output->preprocess_ms=std::max(0.f,
            std::chrono::duration<float,std::milli>(Clock::now()-begin).count()-output->inference_ms);
        PipelineDiagnostics diagnostics{};
        diagnostics.cadence_interval_frames=static_cast<uint32_t>(self.interval_frames);
        diagnostics.pose_person_count=static_cast<uint32_t>(next.size());
        diagnostics.accepted_detection_count=output->body_count;
        diagnostics.pose_inference_total_ms=output->inference_ms;
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            diagnostics.detector_execution_count=self.detector_executions;
            diagnostics.missed_detector_deadlines=self.cadence.MissedDeadlines();
            diagnostics.delayed_detector_discards=self.delayed_discards;
            diagnostics.pose_validation_failures=self.pose_failures;
        }
        PublishPipelineDiagnostics(opaque,diagnostics);
        return HV_OK;
    }catch(const std::exception& e){output->body_count=0;Error(error,e.what());return HV_ERR_INTERNAL;}
    catch(...){output->body_count=0;Error(error,"V3 TopDown processing failed");return HV_ERR_INTERNAL;}
}
const HV_GpuPipelineApiV2 kPipeline{sizeof(kPipeline),HV_GPU_PIPELINE_API_V2,Create,Destroy,Process};
}

extern "C" HV_Result HV_CALL HV_QueryTopDownGpuPipelineV3(uint32_t version,HV_PluginApiV3* out){
    if(version!=HV_PLUGIN_API_V3||!out||out->v1.struct_size<sizeof(*out)||
       out->v1.api_version!=HV_PLUGIN_API_V3)return HV_ERR_INVALID_ARGUMENT;
    *out={};out->v1={sizeof(*out),HV_PLUGIN_API_V3,"pipeline.topdown","0.4.0-preview.4",
        HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON|HV_CAP_GPU_INPUT,8,nullptr,nullptr,0};
    out->gpu_pipeline=&kPipeline;return HV_OK;
}
