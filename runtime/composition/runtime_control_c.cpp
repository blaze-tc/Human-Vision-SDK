#include "composition/runtime_controller.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
using humanvision::runtime::RuntimeController;
namespace {
RuntimeController* Controller(HV_RuntimeControllerHandle h){return reinterpret_cast<RuntimeController*>(h);}
template<class F> int32_t Guard(F f) noexcept {try{return f();}catch(...){return HV_ERR_INTERNAL;}}
void Message(char* out,uint32_t capacity,const std::string& text) {
    if(out&&capacity){const auto n=std::min(size_t(capacity-1),text.size());std::memcpy(out,text.data(),n);out[n]=0;}
}
int32_t HV_CALL Create(const HV_RuntimeControllerConfigV1* c,HV_RuntimeControllerHandle* out,char* error,uint32_t size) {
    if(out)*out=nullptr;
    if(!c||!out){Message(error,size,"Missing deferred runtime configuration/output");return HV_ERR_INVALID_ARGUMENT;}
    return Guard([&] {std::string message;auto controller=RuntimeController::Create(*c,message);
        Message(error,size,message);if(!controller)return int32_t(HV_ERR_INVALID_ARGUMENT);
        *out=reinterpret_cast<HV_RuntimeControllerHandle>(controller.release());return int32_t(0);});
}
int32_t Request(HV_RuntimeControllerHandle h,uint64_t* id,RuntimeController::Operation operation) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->Request(operation,id);});
}
#define REQUEST(name,operation) int32_t HV_CALL name(HV_RuntimeControllerHandle h,uint64_t* id){return Request(h,id,RuntimeController::Operation::operation);}
REQUEST(Start,Start) REQUEST(Pause,Pause) REQUEST(Resume,Resume) REQUEST(Stop,Stop) REQUEST(Close,Close)
#undef REQUEST
int32_t HV_CALL Source(HV_RuntimeControllerHandle h,const HV_RuntimeSourceV1* config,uint64_t* id) {
    if(!h||!config)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{humanvision::runtime::OwnedSourceConfig copy;std::string error;
        if(!humanvision::runtime::CopySourceConfig(*config,copy,error))return int(HV_ERR_INVALID_ARGUMENT);
        return Controller(h)->Request(RuntimeController::Operation::Source,id,nullptr,&copy);});
}
int32_t HV_CALL Reconfigure(HV_RuntimeControllerHandle h,const HV_RuntimeControllerConfigV1* config,uint64_t* id) {
    if(!h||!config)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{humanvision::runtime::OwnedRuntimeConfig copy;std::string error;
        if(!humanvision::runtime::CopyRuntimeConfig(*config,copy,error))return int(HV_ERR_INVALID_ARGUMENT);
        return Controller(h)->Request(RuntimeController::Operation::Reconfigure,id,&copy);});
}
int32_t HV_CALL Release(HV_RuntimeControllerHandle h) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{if(!Controller(h)->IsRetired())return int(HV_CONTROL_BUSY);
        delete Controller(h);return int(0);});
}
int32_t HV_CALL Copy(HV_RuntimeControllerHandle h,int64_t time,HV_CanonicalBodyV1* out,uint32_t capacity,uint32_t* written,HV_RuntimeStatsV1* stats) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->Copy(time,out,capacity,written,stats);});
}
int32_t HV_CALL Status(HV_RuntimeControllerHandle h,uint64_t id,HV_RuntimeRequestStatusV1* out) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->RequestStatus(id,out);});
}
int32_t HV_CALL Capabilities(HV_RuntimeControllerHandle h,HV_RuntimeCapabilitiesV1* out) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->Capabilities(out);});
}
int32_t HV_CALL Stats(HV_RuntimeControllerHandle h,HV_RuntimeStatsV3* out) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->Stats(out);});
}
int32_t HV_CALL Observations(HV_RuntimeControllerHandle h,uint64_t cursor,HV_RuntimeObservationV1* out,uint32_t capacity,HV_RuntimeObservationReadV1* result) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->ReadObservations(cursor,out,capacity,result);});
}
int32_t HV_CALL Submit(HV_RuntimeControllerHandle h,const HV_VideoFrame* frame) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->Submit(frame);});
}
int32_t HV_CALL Regions(HV_RuntimeControllerHandle h,const HV_Rect* regions,uint32_t count,int64_t revision) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->SetRegions(regions,count,revision);});
}
int32_t HV_CALL Diagnostics(HV_RuntimeControllerHandle h,char* text,uint32_t capacity) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->Diagnostics(text,capacity);});
}
int32_t HV_CALL Preview(HV_RuntimeControllerHandle h,uint64_t after,void* bytes,uint32_t capacity,HV_RuntimePreviewV1* info) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->CopyPreview(after,bytes,capacity,info);});
}
int32_t HV_CALL Snapshot(HV_RuntimeControllerHandle h,uint64_t* id) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->RequestSnapshot(id);});
}
int32_t HV_CALL CopySnapshot(HV_RuntimeControllerHandle h,uint64_t id,void* bytes,uint32_t capacity,HV_RuntimePreviewV1* info) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->CopySnapshot(id,bytes,capacity,info);});
}
int32_t HV_CALL CancelSnapshot(HV_RuntimeControllerHandle h,uint64_t id) {
    if(!h)return HV_ERR_INVALID_ARGUMENT;return Guard([&]{return Controller(h)->CancelSnapshot(id);});
}
}
extern "C" int32_t HV_CALL HV_QueryRuntimeControlApiV1(uint32_t version,HV_RuntimeControlApiV1* out) {
    // The original twenty-entry prefix remains queryable. New callers can
    // request the appended cancellation member without moving older offsets.
    constexpr uint32_t prefix_size=offsetof(HV_RuntimeControlApiV1,CancelSnapshot);
    if(!out||version!=1||out->struct_size<prefix_size||out->api_version!=1)return HV_ERR_INVALID_ARGUMENT;
    const auto bytes=std::min<uint32_t>(out->struct_size,sizeof(*out));
    const HV_RuntimeControlApiV1 api{bytes,1,Create,Start,Source,Pause,Resume,Stop,Reconfigure,Close,Release,Copy,Status,
        Capabilities,Stats,Observations,Submit,Regions,Diagnostics,Preview,Snapshot,CopySnapshot,CancelSnapshot};
    std::memcpy(out,&api,bytes);return 0;
}
