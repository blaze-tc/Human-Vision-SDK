#include "plugins/pipeline/simcc/simcc_pipeline.h"
#include "common/plugin_backend.h"
#include "common/config_io.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/rtmpose_preprocess.h"
#include <cmath>
#include <chrono>
namespace {
using namespace humanvision;
struct Instance {
 explicit Instance(HV_HostServicesV1 s,bool h):pose(s),hand(h){}
 runtime::PluginBackend pose;std::unique_ptr<RtmdetModel> detector;
 bool hand;int width=192,height=256,capacity=8,frames=0;
 FrameBuffer frame;PoseInput prep;Tensor input;std::vector<Tensor> outputs;
 std::vector<Detection> boxes;
};
void Error(HV_ErrorBufferV1* e,const char* text){if(e&&e->struct_size>=sizeof(*e)&&e->data&&e->capacity){auto n=std::min(std::strlen(text),size_t(e->capacity-1));std::memcpy(e->data,text,n);e->data[n]=0;}}
HV_Result Create(const HV_PipelineConfigV1* c,const HV_HostServicesV1* s,void** out,HV_ErrorBufferV1* e,bool hand){
 if(!out)return HV_ERR_INVALID_ARGUMENT;*out=nullptr;
 if(!c||!s||c->struct_size<sizeof(*c)||c->api_version!=HV_PLUGIN_API_V1||s->struct_size<sizeof(*s)||s->api_version!=HV_PLUGIN_API_V1||!s->create_backend||!s->release_backend||!c->model_manifest_utf8||!c->asset_root_utf8||c->max_bodies<1||c->max_bodies>8)return HV_ERR_INVALID_ARGUMENT;
 try{
  auto instance=std::make_unique<Instance>(*s,hand);instance->capacity=c->max_bodies;bool loaded=false;
  const auto manifest=nlohmann::json::parse(c->model_manifest_utf8);
  for(const auto& model:manifest.at("models")){
   const auto role=model.at("role").get<std::string>();
   const auto path=runtime::ConfinedPath(std::filesystem::u8path(c->asset_root_utf8),std::filesystem::u8path(model.at("asset_path").get<std::string>()));
   int w=model.at("input_contract").at("width").get<int>(),h=model.at("input_contract").at("height").get<int>();
   if(w<32||h<32||w>2048||h>2048)throw std::runtime_error("Invalid model input size");
   std::string error;
   if(role==(hand?"hand":"body")){instance->width=w;instance->height=h;if(!instance->pose.Load(path,error))throw std::runtime_error(error);loaded=true;}
   else if(role=="detector"&&!hand){instance->detector=std::make_unique<RtmdetModel>(std::make_unique<runtime::PluginBackend>(*s),w,h,true);if(!instance->detector->Load(path,error))throw std::runtime_error(error);}
  }
  if(!loaded||(!hand&&!instance->detector))throw std::runtime_error("Missing SimCC model role");
  instance->input.name="input";instance->input.shape={1,3,instance->height,instance->width};instance->boxes.reserve(8);*out=instance.release();return HV_OK;
 }catch(const std::exception& ex){Error(e,ex.what());return HV_ERR_MODEL_LOAD;}catch(...){Error(e,"SimCC creation exception");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL CreateBody(const HV_PipelineConfigV1* c,const HV_HostServicesV1* s,void** out,HV_ErrorBufferV1* e){return Create(c,s,out,e,false);}
HV_Result HV_CALL CreateHand(const HV_PipelineConfigV1* c,const HV_HostServicesV1* s,void** out,HV_ErrorBufferV1* e){return Create(c,s,out,e,true);}
void HV_CALL Destroy(void* p){delete static_cast<Instance*>(p);}
HV_Result HV_CALL Process(void* p,const HV_PipelineInputV1* in,HV_PipelineOutputV1* out,HV_ErrorBufferV1* e){
 if(!p||!in||!out||in->struct_size<sizeof(*in)||in->api_version!=HV_PLUGIN_API_V1||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 out->body_count=out->hand_count=0;out->preprocess_ms=out->inference_ms=out->postprocess_ms=0;
 auto& a=*static_cast<Instance*>(p);
 if((in->roi_count&&!in->rois)||in->roi_count>16||(!a.hand&&(!out->bodies||out->body_capacity<uint32_t(a.capacity)))||(a.hand&&(!out->hands||out->hand_capacity<in->roi_count)))return HV_ERR_INVALID_ARGUMENT;
 try{
  std::string error;auto status=ValidateVideoFrame(&in->frame,error);if(status!=HV_OK){Error(e,error.c_str());return status;}
  auto begin=std::chrono::steady_clock::now();auto& f=a.frame;const auto& source=in->frame;
  if(f.width!=source.width||f.height!=source.height)a.boxes.clear();
  f.width=source.width;f.height=source.height;f.stride_bytes=source.stride_bytes;f.pixel_format=source.pixel_format;f.timestamp_us=source.timestamp_us;f.frame_id=source.frame_id;f.bytes.resize(source.data_bytes);std::memcpy(f.bytes.data(),source.data,source.data_bytes);
  if(!a.hand&&(a.boxes.empty()||a.frames++%5==0)){float ms=0;if(!a.detector->Detect(f,.35F,a.capacity,a.boxes,ms,error))throw std::runtime_error(error);out->inference_ms+=ms;}
  size_t n=a.hand?in->roi_count:a.boxes.size();bool reacquire=false;
  constexpr int mapping[]{HV_CANONICAL_NOSE,HV_CANONICAL_EYE_LEFT,HV_CANONICAL_EYE_RIGHT,HV_CANONICAL_EAR_LEFT,HV_CANONICAL_EAR_RIGHT,HV_CANONICAL_SHOULDER_LEFT,HV_CANONICAL_SHOULDER_RIGHT,HV_CANONICAL_ELBOW_LEFT,HV_CANONICAL_ELBOW_RIGHT,HV_CANONICAL_WRIST_LEFT,HV_CANONICAL_WRIST_RIGHT,HV_CANONICAL_HIP_LEFT,HV_CANONICAL_HIP_RIGHT,HV_CANONICAL_KNEE_LEFT,HV_CANONICAL_KNEE_RIGHT,HV_CANONICAL_ANKLE_LEFT,HV_CANONICAL_ANKLE_RIGHT,HV_CANONICAL_HEAD,HV_CANONICAL_NECK,HV_CANONICAL_PELVIS,HV_CANONICAL_FOOT_LEFT,HV_CANONICAL_FOOT_RIGHT};
  for(size_t i=0;i<n;++i){
   Detection box;if(a.hand){auto r=in->rois[i].bbox_px;box={r.x,r.y,r.x+r.width,r.y+r.height,1};}else box=a.boxes[i];
   if(!PreprocessRtmpose(f,box,a.prep,error,a.width,a.height))throw std::runtime_error(error);
   a.input.values.swap(a.prep.normalized_chw);auto start=std::chrono::steady_clock::now();bool ok=a.pose.Run(a.input,a.outputs,error);a.input.values.swap(a.prep.normalized_chw);
   out->inference_ms+=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();if(!ok)throw std::runtime_error(error);
   const Tensor *x=nullptr,*y=nullptr;for(const auto& t:a.outputs){if(t.name=="simcc_x")x=&t;if(t.name=="simcc_y")y=&t;}
   int count=a.hand?21:26;auto valid=[&](const Tensor* t,int bins){return t&&t->shape.size()==3&&t->shape[0]==1&&t->shape[1]==count&&t->shape[2]==bins&&t->values.size()==size_t(count)*bins;};
   if(!valid(x,a.width*2)||!valid(y,a.height*2))throw std::runtime_error("SimCC tensor shape mismatch");
   std::array<HV_CanonicalJointV1,26> joints{};
   for(int k=0;k<count;++k){auto xb=x->values.begin()+size_t(k)*a.width*2,yb=y->values.begin()+size_t(k)*a.height*2;
    if(!std::all_of(xb,xb+a.width*2,[](float v){return std::isfinite(v);})||!std::all_of(yb,yb+a.height*2,[](float v){return std::isfinite(v);}))throw std::runtime_error("Nonfinite SimCC output");
    auto xp=std::max_element(xb,xb+a.width*2),yp=std::max_element(yb,yb+a.height*2);auto point=TransformPoint(a.prep.transform.input_to_source,{float(xp-xb)*.5F,float(yp-yb)*.5F});auto& j=joints[k];j.struct_size=sizeof(j);j.api_version=HV_API_VERSION_040;j.x_px=point.x;j.y_px=point.y;j.x_norm=point.x/f.width;j.y_norm=point.y/f.height;j.confidence=std::min(*xp,*yp);j.valid=j.confidence>=.3F&&point.x>=0&&point.y>=0&&point.x<f.width&&point.y<f.height;j.observation_timestamp_us=f.timestamp_us;
   }
   if(a.hand){auto& hand=out->hands[out->hand_count++];hand={};hand.struct_size=sizeof(hand);hand.api_version=HV_PLUGIN_API_V1;hand.request_id=in->rois[i].request_id;hand.side=in->rois[i].side;hand.fingertip=joints[8];hand.thumb=joints[4];hand.palm=joints[0];hand.palm.x_px=hand.palm.y_px=hand.palm.x_norm=hand.palm.y_norm=0;hand.palm.derived=1;
    for(int k:{0,5,9,13,17}){hand.palm.x_px+=joints[k].x_px/5;hand.palm.y_px+=joints[k].y_px/5;hand.palm.x_norm+=joints[k].x_norm/5;hand.palm.y_norm+=joints[k].y_norm/5;hand.palm.confidence=std::min(hand.palm.confidence,joints[k].confidence);hand.palm.valid&=joints[k].valid;}
   }else{
    int valid=0;float left=float(f.width),top=float(f.height),right=0,bottom=0;
    for(int k=0;k<17;++k)if(joints[k].valid){++valid;left=std::min(left,joints[k].x_px);top=std::min(top,joints[k].y_px);right=std::max(right,joints[k].x_px);bottom=std::max(bottom,joints[k].y_px);}
    if(valid<5){reacquire=true;continue;}
    auto& body=out->bodies[out->body_count++];body={};body.struct_size=sizeof(body);body.api_version=HV_PLUGIN_API_V1;body.bbox_px={box.x1,box.y1,box.x2-box.x1,box.y2-box.y1};body.confidence=box.score;for(auto& j:body.joints){j.struct_size=sizeof(j);j.api_version=HV_API_VERSION_040;}for(int k=0;k<22;++k)body.joints[mapping[k]]=joints[k];
    const float cx=(left+right)*.5F,cy=(top+bottom)*.5F,w=std::max((right-left)*1.3F,(box.x2-box.x1)*.95F),h=std::max((bottom-top)*1.3F,(box.y2-box.y1)*.95F);
    a.boxes[i]={std::max(0.F,cx-w*.5F),std::max(0.F,cy-h*.5F),std::min(float(f.width),cx+w*.5F),std::min(float(f.height),cy+h*.5F),box.score};
   }
  }
  if(reacquire)a.boxes.clear();
  out->preprocess_ms=std::max(0.F,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-begin).count()-out->inference_ms);return HV_OK;
 }catch(const std::exception& ex){out->body_count=out->hand_count=0;Error(e,ex.what());return HV_ERR_INTERNAL;}catch(...){out->body_count=out->hand_count=0;Error(e,"SimCC processing exception");return HV_ERR_INTERNAL;}
}
const HV_PipelineApiV1 body_api{sizeof(body_api),HV_PLUGIN_API_V1,CreateBody,Destroy,Process},hand_api{sizeof(hand_api),HV_PLUGIN_API_V1,CreateHand,Destroy,Process};
}
extern "C" HV_Result HV_CALL HV_QueryTopDownPipeline(uint32_t v,HV_PluginApiV1* out){if(v!=HV_PLUGIN_API_V1||!out||out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;*out={sizeof(*out),HV_PLUGIN_API_V1,"pipeline.topdown","0.4.0-preview.1",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE|HV_CAP_MULTI_PERSON,8,&body_api,nullptr,0};return HV_OK;}
extern "C" HV_Result HV_CALL HV_QueryHandPipeline(uint32_t v,HV_PluginApiV1* out){if(v!=HV_PLUGIN_API_V1||!out||out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;*out={sizeof(*out),HV_PLUGIN_API_V1,"pipeline.hand","0.4.0-preview.1",HV_PLUGIN_PIPELINE,HV_CAP_HAND_POSE,8,&hand_api,nullptr,0};return HV_OK;}
