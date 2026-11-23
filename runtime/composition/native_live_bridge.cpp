#include "composition/native_live_bridge.h"
#include "humanvision/humanvision_runtime_control.h"
#include <cstring>
#include <new>
namespace humanvision::runtime {
namespace {
struct Bridge {HV_InputLiveApiV1 api;HV_InputLiveHandle consumer;HV_InputLiveRenderEvent render;std::shared_ptr<void> module;};
Bridge* Owner(HV_RuntimeLiveHandle h){return static_cast<Bridge*>(h);}
template<class A,class B> B Convert(const A& input){static_assert(sizeof(A)==sizeof(B));B out;std::memcpy(&out,&input,sizeof(out));return out;}
int32_t HV_CALL Configure(HV_RuntimeLiveHandle h,const HV_RuntimeLiveConfigV1* config){
 if(!h||!config)return HV_INPUT_INVALID;auto input=Convert<HV_RuntimeLiveConfigV1,HV_InputLiveConfigV1>(*config);
 return Owner(h)->api.Configure(Owner(h)->consumer,&input);
}
int32_t HV_CALL Geometry(HV_RuntimeLiveHandle h,HV_RuntimeLiveGeometryV1* out){
 if(!h||!out||out->struct_size<sizeof(*out)||out->api_version!=1)return HV_INPUT_INVALID;
 HV_InputLiveGeometryV1 value{sizeof(value),1};auto* b=Owner(h);const auto result=b->api.GetGeometry(b->consumer,&value);
 if(result==0)*out=Convert<HV_InputLiveGeometryV1,HV_RuntimeLiveGeometryV1>(value);return result;
}
int32_t HV_CALL Bind(HV_RuntimeLiveHandle h,const HV_RuntimeLiveTargetsV1* targets){
 if(!h||!targets)return HV_INPUT_INVALID;auto input=Convert<HV_RuntimeLiveTargetsV1,HV_InputLiveTargetsV1>(*targets);
 return Owner(h)->api.BindTargets(Owner(h)->consumer,&input);
}
int32_t HV_CALL Prepare(HV_RuntimeLiveHandle h){return h?Owner(h)->api.PrepareRenderEvent(Owner(h)->consumer):HV_INPUT_INVALID;}
void HV_CALL Render(int id,void* h){if(h){auto* b=Owner(h);b->render(id,b->consumer);}}
HV_RuntimeLiveRenderEvent HV_CALL Event(){return Render;}
int32_t HV_CALL Poll(HV_RuntimeLiveHandle h,uint64_t after,HV_RuntimeLiveFrameV1* out){
 if(!h||!out||out->struct_size<sizeof(*out)||out->api_version!=1)return HV_INPUT_INVALID;
 HV_InputLiveFrameV1 value{sizeof(value),1};auto* b=Owner(h);const auto result=b->api.Poll(b->consumer,after,&value);
 if(result==0)*out=Convert<HV_InputLiveFrameV1,HV_RuntimeLiveFrameV1>(value);return result;
}
int32_t HV_CALL ReleaseSlot(HV_RuntimeLiveHandle h,uint32_t slot,uint64_t generation,uint64_t sequence){
 return h?Owner(h)->api.ReleaseSlot(Owner(h)->consumer,slot,generation,sequence):HV_INPUT_INVALID;
}
#define LIVE_FORWARD(name) int32_t HV_CALL name(HV_RuntimeLiveHandle h){return h?Owner(h)->api.name(Owner(h)->consumer):HV_INPUT_INVALID;}
LIVE_FORWARD(RetireTargets) LIVE_FORWARD(TargetsRetired) LIVE_FORWARD(Close)
#undef LIVE_FORWARD
int32_t HV_CALL Release(HV_RuntimeLiveHandle h){
 if(!h)return HV_INPUT_INVALID;auto* b=Owner(h);const auto result=b->api.TryRelease(b->consumer);
 if(result==HV_INPUT_OK)delete b;return result;
}
int32_t HV_CALL Error(HV_RuntimeLiveHandle h,char* text,uint32_t capacity){return h?Owner(h)->api.GetLastError(Owner(h)->consumer,text,capacity):HV_INPUT_INVALID;}
const HV_RuntimeLiveApiV1 api{sizeof(api),1,Configure,Geometry,Bind,Prepare,Event,Poll,ReleaseSlot,RetireTargets,TargetsRetired,Close,Release,Error};
}
int AttachNativeLiveBridge(const HV_InputLiveApiV1& input,HV_InputHandle source,
 const HV_RuntimeLiveConfigV1& config,uint64_t source_id,uint64_t epoch,int64_t offset,
 uint64_t clock,std::shared_ptr<void> module,HV_RuntimeLiveBindingV1& out){
 if(config.struct_size<sizeof(config)||config.api_version!=1||!source_id||!epoch||!source||
    input.size<sizeof(input)||input.version!=1||!input.Attach||!input.Configure||!input.GetGeometry||!input.BindTargets||
    !input.PrepareRenderEvent||!input.GetRenderEventFunc||!input.Poll||!input.ReleaseSlot||!input.RetireTargets||
    !input.TargetsRetired||!input.Close||!input.TryRelease||!input.GetLastError)return HV_ERR_INVALID_ARGUMENT;
 auto bridge=std::make_unique<Bridge>();bridge->api=input;bridge->module=std::move(module);
 // Configure the Unity Vulkan event before it can be queued, not from inside
 // the callback that already needs its render-pass/submission guarantees.
 bridge->render=input.GetRenderEventFunc();if(!bridge->render)return HV_CONTROL_UNSUPPORTED;
 const auto options=Convert<HV_RuntimeLiveConfigV1,HV_InputLiveConfigV1>(config);
 const int result=input.Attach(source,&options,&bridge->consumer);
 if(result!=HV_INPUT_OK)return result==HV_INPUT_BUSY?HV_CONTROL_BUSY:HV_CONTROL_UNSUPPORTED;
 out={sizeof(out),1,source_id,epoch,offset,clock,bridge.release(),&api};return HV_CONTROL_OK;
}
}
