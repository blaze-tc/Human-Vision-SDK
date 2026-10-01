#include "plugins/pipeline/yolo/yolo_gpu_pipeline.h"
#include "plugins/pipeline/yolo/yolo_decoder.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/backend/ncnn/ncnn_model_options.h"
#include "gpu/android/unity_vulkan_plugin.h"
#include "common/pipeline_diagnostics.h"
#include "json/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
using namespace humanvision::runtime;
using Clock=std::chrono::steady_clock;
void Error(HV_ErrorBufferV1* e,const char* message) {
    if(!e||!e->data||!e->capacity)return;
    const auto n=std::min(std::strlen(message),size_t(e->capacity-1));
    std::memcpy(e->data,message,n);e->data[n]=0;
}
struct Instance {
    HV_HostServicesV3 host{};
    ncnn_backend::InputContract contract;
    std::string manifest,root;
    const HV_GpuBackendApiV2* backend=nullptr;void* session=nullptr;
    yolo::Decoder decoder;
    uint64_t generation=0,executions=0;
    int64_t last_frame=-1;
    int capacity=0;
    Instance(const HV_HostServicesV3& services,int limit,int width,int height):host(services),decoder(width,height),capacity(limit){}
    void Release() {if(session)host.release_gpu_backend_v3(host.v2.v1.context,backend,session);session=nullptr;backend=nullptr;}
    ~Instance(){Release();}
    bool Initialize(const HV_GpuFrameRefV1& frame,HV_ErrorBufferV1* error) {
        if(session&&generation==frame.generation)return true;
        if(generation&&frame.generation<generation){Error(error,"Stale YOLO GPU resource generation");return false;}
        Release();last_frame=-1;
        HV_GpuDeviceContextV1 device{};device.struct_size=sizeof(device);device.api_version=HV_GPU_FRAME_API_V1;
#if defined(__ANDROID__)
        humanvision::gpu::VulkanDeviceContext unity{};
        if(!humanvision::gpu::UnityVulkanProducerContext(unity)){Error(error,"Unity Vulkan producer context unavailable");return false;}
        ncnn_backend::HostContext context{};context.unity_device=unity;context.bridge=humanvision::gpu::UnityVulkanProducerBridge();
        const auto identity=humanvision::gpu::QueryDeviceIdentity(unity);
        std::copy(identity.device_uuid.begin(),identity.device_uuid.end(),device.device_uuid);
        std::copy(identity.driver_uuid.begin(),identity.driver_uuid.end(),device.driver_uuid);device.host_context=&context;
#endif
        HV_GpuBackendConfigV1 config{sizeof(config),HV_GPU_FRAME_API_V1,manifest.c_str(),root.c_str(),"backend.ncnn.vulkan"};
        const auto status=host.create_gpu_backend_v3(host.v2.v1.context,&config,&device,&backend,&session,error);
        if(status!=HV_OK||!backend||!session||backend->v1.struct_size<sizeof(HV_GpuBackendApiV2)||
           backend->v1.api_version!=HV_GPU_FRAME_API_V1||!backend->v1.run_image){Release();return false;}
        generation=frame.generation;return true;
    }
};
HV_Result HV_CALL Create(const HV_PipelineConfigV1* c,const HV_HostServicesV3* h,void** out,HV_ErrorBufferV1* e) {
    if(out)*out=nullptr;
    if(!c||!h||!out||c->struct_size<sizeof(*c)||c->api_version!=HV_PLUGIN_API_V1||
       h->v2.v1.struct_size<sizeof(*h)||h->v2.v1.api_version!=HV_PLUGIN_API_V1||
       !c->model_manifest_utf8||!c->asset_root_utf8||!c->options_utf8||c->max_bodies<1||c->max_bodies>8||
       !h->create_gpu_backend_v3||!h->release_gpu_backend_v3)return HV_ERR_INVALID_ARGUMENT;
    try {
        auto manifest=nlohmann::json::parse(c->model_manifest_utf8);
        const auto options=nlohmann::json::parse(c->options_utf8);
        const auto execution_contract=manifest.at("execution_contract").get<std::string>();
        const bool sgemm=execution_contract=="raw_tensor_fp32_sgemm_v1";
        if(manifest.at("schema_version")!=2||!manifest.at("local_evaluation_only").get<bool>()||
           !options.at("local_evaluation_only").get<bool>()||manifest.at("pipeline_id")!="pipeline.yolo.pose"||
           (execution_contract!="raw_tensor_fp32_v1"&&!sgemm)||manifest.at("models").size()!=1||
           manifest.at("max_people").get<int>()<c->max_bodies)
            throw std::runtime_error("YOLO requires a local evaluation schema-2 raw FP32 ModelPack/profile");
        const auto& model=manifest.at("models").at(0);
        if(model.at("role")!="body"||model.at("format")!="ncnn"||model.at("decoder_id")!="yolov8_pose_dfl17_v1"||
           model.at("execution_contract")!=execution_contract)throw std::runtime_error("YOLO body model decoder/execution contract mismatch");
        ncnn_backend::BackendOptions backend_options;std::string reason;
        if(!ncnn_backend::ParseBackendOptions(model,backend_options,reason))throw std::runtime_error(reason);
        const auto& output=model.at("output_contract");auto input=model.at("input_contract");input["output_blobs"]=output.at("output_blobs");
        ncnn_backend::InputContract contract;
        if(!ncnn_backend::ParseInputContract(input,contract,reason))throw std::runtime_error(reason);
        const int anchors=yolo::AnchorCount(contract.width,contract.height);
        if(sgemm&&(contract.width!=640||contract.height!=384))
            throw std::runtime_error("YOLO FP32 SGEMM requires reviewed rectangle640x384 input geometry");
        if(!anchors||contract.output_type!=HV_GPU_TENSOR_FP32||
           contract.output_elempack!=1||contract.input_blob!="in0"||contract.crop_mode!=ncnn_backend::InputContract::CropMode::Letterbox||
           contract.output_blobs!=std::vector<std::string>{"out0","out1"}||output.at("decoder")!="yolov8_pose_dfl17_v1"||
           output.at("max_output_bytes").at("out0")!=anchors*65*4||output.at("max_output_bytes").at("out1")!=anchors*51*4)
            throw std::runtime_error("YOLO requires reviewed square320/416 or rectangle512x288/640x384 RGB FP32 pack1 and exact bounded out0/out1");
        for(int i=0;i<3;++i)if(contract.mean[i]!=0||contract.norm[i]!=1.f/255.f||contract.pad_rgb[i]!=114)
            throw std::runtime_error("YOLO requires RGB /255 and pad114");
        auto self=std::make_unique<Instance>(*h,c->max_bodies,contract.width,contract.height);
        manifest["active_role"]="body";self->manifest=manifest.dump();self->root=c->asset_root_utf8;self->contract=std::move(contract);
        if(!RegisterPipelineDiagnostics(self.get()))throw std::runtime_error("Pipeline diagnostics capacity exhausted");
        *out=self.release();return HV_OK;
    }catch(const std::exception& ex){Error(e,ex.what());return HV_ERR_MODEL_LOAD;}
    catch(...){Error(e,"YOLO pipeline creation failed");return HV_ERR_INTERNAL;}
}
void HV_CALL Destroy(void* p){UnregisterPipelineDiagnostics(p);delete static_cast<Instance*>(p);}
HV_Result HV_CALL Process(void* p,const HV_GpuFrameRefV1* f,HV_ObservationFrameV1* out,HV_ErrorBufferV1* e) {
    if(!p||!f||!out||f->struct_size<sizeof(*f)||f->api_version!=HV_GPU_FRAME_API_V1||!f->opaque_slot||
       !f->generation||f->frame_id<0||f->width<1||f->height<1||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    auto& self=*static_cast<Instance*>(p);out->body_count=out->hand_count=0;
    out->preprocess_ms=out->inference_ms=out->postprocess_ms=0;
    out->source_frame_id=f->frame_id;out->source_timestamp_us=f->timestamp_us;out->width=f->width;out->height=f->height;
    try {
        yolo::Geometry g{};
        if(!yolo::BuildGeometry(f->width,f->height,self.contract.width,self.contract.height,g)) {
            Error(e,"YOLO rectangular input requires reviewed 16:9 landscape source geometry");return HV_ERR_INVALID_ARGUMENT;
        }
        if(!self.Initialize(*f,e))return HV_ERR_NOT_INITIALIZED;
        if(f->frame_id<=self.last_frame){Error(e,"Duplicate or stale YOLO source frame");return HV_ERR_INVALID_ARGUMENT;}
        self.last_frame=f->frame_id;
        HV_GpuImageTransformV1 transform{};transform.struct_size=sizeof(transform);transform.api_version=HV_GPU_FRAME_API_V1;
        transform.output_width=g.width;transform.output_height=g.height;transform.output_type=HV_GPU_TENSOR_FP32;
        transform.output_elempack=1;transform.channel_order=1;
        const double nominal=double(g.width)/std::max(f->width,f->height);
        const int rw=f->width>f->height?g.width:int(f->width*nominal);
        const int rh=g.width!=g.height?g.width*9/16:(f->width>f->height?int(f->height*nominal):g.height);
        const float sx=float(rw)/f->width,sy=float(rh)/f->height;
        transform.source_rect_px={-g.left/sx,-g.top/sy,g.width/sx,g.height/sy};
        for(int i=0;i<3;++i)transform.norm[i]=self.contract.norm[i];
        const auto begin=Clock::now();HV_TensorViewV1 tensors[2]{};uint32_t tensor_count=0;
        const auto status=self.backend->v1.run_image(self.session,f,&transform,tensors,2,&tensor_count,e);
        out->inference_ms=std::chrono::duration<float,std::milli>(Clock::now()-begin).count();
        if(status!=HV_OK)return status;
        float scores[8]{};uint32_t count=0;const auto decode_begin=Clock::now();
        if(!self.decoder.Decode(tensors,tensor_count,g,f->timestamp_us,out->bodies,scores,self.capacity,count)){
            Error(e,"YOLO output shape or finite-value contract invalid");return HV_ERR_INTERNAL;}
        auto& consumer=*static_cast<humanvision::gpu::ConsumerFrame*>(f->opaque_slot);
        std::string reason;
        if(!humanvision::gpu::CompleteGpuRole(consumer,true,reason)){
            Error(e,reason.c_str());return HV_ERR_INTERNAL;}
        out->body_count=count;out->postprocess_ms=std::chrono::duration<float,std::milli>(Clock::now()-decode_begin).count();
        if(out->struct_size>=sizeof(HV_GpuObservationFrameV3)) {
            auto& sidecar=*reinterpret_cast<HV_GpuObservationFrameV3*>(out);
            for(uint32_t i=0;i<HV_MAX_PEOPLE;++i){sidecar.detector_scores[i]=i<count?scores[i]:0;sidecar.crop_track_ids[i]=0;}
        }
        PipelineDiagnostics diagnostics{};diagnostics.detector_keyframe=true;diagnostics.cadence_interval_frames=1;
        diagnostics.detector_execution_count=++self.executions;diagnostics.detector_attempted=self.executions;
        diagnostics.pose_person_count=count;diagnostics.accepted_detection_count=count;
        diagnostics.detector_inference_ms=out->inference_ms;PublishPipelineDiagnostics(p,diagnostics);return HV_OK;
    }catch(const std::exception& ex){out->body_count=0;Error(e,ex.what());return HV_ERR_INTERNAL;}
    catch(...){out->body_count=0;Error(e,"YOLO GPU processing failed");return HV_ERR_INTERNAL;}
}
const HV_GpuPipelineApiV2 api{sizeof(api),HV_GPU_PIPELINE_API_V2,Create,Destroy,Process};
}
extern "C" HV_Result HV_CALL HV_QueryYoloGpuPipelineV3(uint32_t version,HV_PluginApiV3* out) {
    if(version!=HV_PLUGIN_API_V3||!out||out->v1.struct_size<sizeof(*out)||out->v1.api_version!=HV_PLUGIN_API_V3)return HV_ERR_INVALID_ARGUMENT;
    *out={};out->v1={sizeof(*out),HV_PLUGIN_API_V3,"pipeline.yolo.pose","0.4.0-preview.4",HV_PLUGIN_PIPELINE,
        HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON|HV_CAP_GPU_INPUT,8,nullptr,nullptr,0};out->gpu_pipeline=&api;return HV_OK;
}
