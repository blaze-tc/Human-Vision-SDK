#include "plugins/pipeline/rtmo/rtmo_pipeline.h"
#include "common/plugin_backend.h"
#include "common/config_io.h"
#include "core/frame_buffer.h"
#include <cmath>
#include <chrono>
namespace {
using namespace humanvision;
struct Instance {
 explicit Instance(HV_HostServicesV1 s):backend(s){}
 runtime::PluginBackend backend;Tensor input;std::vector<Tensor> outputs;
 int width=416,height=416,capacity=8;float threshold=.35F;
};
void Error(HV_ErrorBufferV1* e,const char* text){if(e&&e->struct_size>=sizeof(*e)&&e->data&&e->capacity){auto n=std::min(std::strlen(text),size_t(e->capacity-1));std::memcpy(e->data,text,n);e->data[n]=0;}}
HV_Result HV_CALL Create(const HV_PipelineConfigV1* c,const HV_HostServicesV1* s,void** out,HV_ErrorBufferV1* e){
 if(!out)return HV_ERR_INVALID_ARGUMENT;*out=nullptr;
 if(!c||!s||c->struct_size<sizeof(*c)||c->api_version!=HV_PLUGIN_API_V1||s->struct_size<sizeof(*s)||s->api_version!=HV_PLUGIN_API_V1||!s->create_backend||!s->release_backend||!c->model_manifest_utf8||!c->asset_root_utf8||c->max_bodies<1||c->max_bodies>8)return HV_ERR_INVALID_ARGUMENT;
 try{
  auto instance=std::make_unique<Instance>(*s);instance->capacity=c->max_bodies;
  auto manifest=nlohmann::json::parse(c->model_manifest_utf8);bool loaded=false;
  for(const auto& model:manifest.at("models"))if(model.at("role")=="body"){
   instance->width=model.at("input_contract").at("width").get<int>();instance->height=model.at("input_contract").at("height").get<int>();
   if(instance->width<32||instance->height<32||instance->width>2048||instance->height>2048)throw std::runtime_error("Invalid RTMO input dimensions");
   const auto path=runtime::ConfinedPath(std::filesystem::u8path(c->asset_root_utf8),std::filesystem::u8path(model.at("asset_path").get<std::string>()));
   std::string error;if(!instance->backend.Load(path,error))throw std::runtime_error(error);loaded=true;break;
  }
  if(!loaded)throw std::runtime_error("RTMO body model role is missing");
  instance->input.name="input";instance->input.shape={1,3,instance->height,instance->width};instance->input.values.resize(size_t(instance->width)*instance->height*3);
  *out=instance.release();return HV_OK;
 }catch(const std::exception& ex){Error(e,ex.what());return HV_ERR_MODEL_LOAD;}catch(...){Error(e,"RTMO creation exception");return HV_ERR_INTERNAL;}
}
void HV_CALL Destroy(void* p){delete static_cast<Instance*>(p);}
float Pixel(const HV_VideoFrame& frame,int x,int y,int channel){
 if(x<0||y<0||x>=frame.width||y>=frame.height)return 114;
 const auto* p=static_cast<const uint8_t*>(frame.data)+size_t(y)*frame.stride_bytes+size_t(x)*BytesPerPixel(frame.pixel_format);
 bool rgb=frame.pixel_format==HV_PIXEL_RGB24||frame.pixel_format==HV_PIXEL_RGBA32;return p[rgb?2-channel:channel];
}
HV_Result HV_CALL Process(void* p,const HV_PipelineInputV1* in,HV_PipelineOutputV1* out,HV_ErrorBufferV1* e){
 if(!p||!in||!out||in->struct_size<sizeof(*in)||in->api_version!=HV_PLUGIN_API_V1||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 out->body_count=out->hand_count=0;out->preprocess_ms=out->inference_ms=out->postprocess_ms=0;
 auto& instance=*static_cast<Instance*>(p);if(!out->bodies||out->body_capacity<uint32_t(instance.capacity)||in->roi_count)return HV_ERR_INVALID_ARGUMENT;
 try{
  std::string error;auto valid=ValidateVideoFrame(&in->frame,error);if(valid!=HV_OK){Error(e,error.c_str());return valid;}
  auto begin=std::chrono::steady_clock::now();const auto& frame=in->frame;
  const float scale=std::min(float(instance.width)/frame.width,float(instance.height)/frame.height);
  const float tx=(instance.width-frame.width*scale)*.5F,ty=(instance.height-frame.height*scale)*.5F;
  const size_t plane=size_t(instance.width)*instance.height;
  for(int y=0;y<instance.height;++y)for(int x=0;x<instance.width;++x){
   // OpenCV affine interpolation uses a 1/32-pixel interpolation table.
   const float sx=std::round(((x-tx)/scale)*32)/32,sy=std::round(((y-ty)/scale)*32)/32;
   const int ix=int(std::floor(sx)),iy=int(std::floor(sy));const float fx=sx-ix,fy=sy-iy;
   for(int c=0;c<3;++c){const float top=Pixel(frame,ix,iy,c)*(1-fx)+Pixel(frame,ix+1,iy,c)*fx;
    const float bottom=Pixel(frame,ix,iy+1,c)*(1-fx)+Pixel(frame,ix+1,iy+1,c)*fx;
    instance.input.values[size_t(c)*plane+size_t(y)*instance.width+x]=std::floor(top*(1-fy)+bottom*fy+.5F);}
  }
  const auto prepared=std::chrono::steady_clock::now();out->preprocess_ms=std::chrono::duration<float,std::milli>(prepared-begin).count();
  if(!instance.backend.Run(instance.input,instance.outputs,error)){Error(e,error.c_str());return HV_ERR_INTERNAL;}
  const auto inferred=std::chrono::steady_clock::now();out->inference_ms=std::chrono::duration<float,std::milli>(inferred-prepared).count();
  const Tensor *dets=nullptr,*keys=nullptr;for(const auto& tensor:instance.outputs){if(tensor.name=="dets")dets=&tensor;if(tensor.name=="keypoints")keys=&tensor;}
  if(!dets||!keys||dets->shape.size()!=3||dets->shape[0]!=1||dets->shape[2]!=5||keys->shape.size()!=4||keys->shape[0]!=1||keys->shape[1]!=dets->shape[1]||keys->shape[2]!=17||keys->shape[3]!=3||dets->shape[1]<0||dets->values.size()!=size_t(dets->shape[1])*5||keys->values.size()!=size_t(dets->shape[1])*51)throw std::runtime_error("RTMO output contract must be dets[1,N,5],keypoints[1,N,17,3]");
  constexpr int mapping[]{HV_CANONICAL_NOSE,HV_CANONICAL_EYE_LEFT,HV_CANONICAL_EYE_RIGHT,HV_CANONICAL_EAR_LEFT,HV_CANONICAL_EAR_RIGHT,HV_CANONICAL_SHOULDER_LEFT,HV_CANONICAL_SHOULDER_RIGHT,HV_CANONICAL_ELBOW_LEFT,HV_CANONICAL_ELBOW_RIGHT,HV_CANONICAL_WRIST_LEFT,HV_CANONICAL_WRIST_RIGHT,HV_CANONICAL_HIP_LEFT,HV_CANONICAL_HIP_RIGHT,HV_CANONICAL_KNEE_LEFT,HV_CANONICAL_KNEE_RIGHT,HV_CANONICAL_ANKLE_LEFT,HV_CANONICAL_ANKLE_RIGHT};
  for(size_t n=0;n<size_t(dets->shape[1])&&out->body_count<uint32_t(instance.capacity);++n){
   const float* box=dets->values.data()+n*5;if(!std::isfinite(box[4])||box[4]<instance.threshold)continue;
   bool finite=true;for(int i=0;i<4;++i)finite&=std::isfinite(box[i]);if(!finite||box[2]<=box[0]||box[3]<=box[1])continue;
   auto& body=out->bodies[out->body_count++];body={};body.struct_size=sizeof(body);body.api_version=HV_PLUGIN_API_V1;body.confidence=box[4];
   body.bbox_px={(box[0]-tx)/scale,(box[1]-ty)/scale,(box[2]-box[0])/scale,(box[3]-box[1])/scale};
   for(auto& joint:body.joints){joint.struct_size=sizeof(joint);joint.api_version=HV_API_VERSION_040;}
   for(int k=0;k<17;++k){const float* point=keys->values.data()+n*51+k*3;auto& joint=body.joints[mapping[k]];
    if(!std::isfinite(point[0])||!std::isfinite(point[1])||!std::isfinite(point[2]))continue;
    joint.x_px=(point[0]-tx)/scale;joint.y_px=(point[1]-ty)/scale;joint.x_norm=joint.x_px/frame.width;joint.y_norm=joint.y_px/frame.height;
    joint.confidence=point[2];joint.valid=point[2]>=.3F;joint.observation_timestamp_us=frame.timestamp_us;
   }
  }
  out->postprocess_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-inferred).count();return HV_OK;
 }catch(const std::exception& ex){out->body_count=0;Error(e,ex.what());return HV_ERR_INTERNAL;}catch(...){out->body_count=0;Error(e,"RTMO processing exception");return HV_ERR_INTERNAL;}
}
const HV_PipelineApiV1 api{sizeof(api),HV_PLUGIN_API_V1,Create,Destroy,Process};
}
extern "C" HV_Result HV_CALL HV_QueryRtmoPipeline(uint32_t version,HV_PluginApiV1* out){
 if(version!=HV_PLUGIN_API_V1||!out||out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
 *out={sizeof(*out),HV_PLUGIN_API_V1,"pipeline.rtmo","0.4.0-preview.1",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON,8,&api,nullptr,0};return HV_OK;
}
