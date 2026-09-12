#include "composition/session.h"
#include <algorithm>
#include <cstring>
#include <chrono>
using humanvision::runtime::RuntimeSession;
namespace {void Message(char* out,uint32_t capacity,const std::string& message){if(out&&capacity){auto n=std::min(size_t(capacity-1),message.size());std::memcpy(out,message.data(),n);out[n]=0;}}}
extern "C" {
int64_t HV_CALL HV_RuntimeClockUs(void){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
HV_Result HV_CALL HV_RuntimeCreate(const HV_RuntimeConfigV1* config,HV_RuntimeHandle* out,char* error,uint32_t capacity){
 if(out)*out=nullptr;
 if(!config||!out||config->struct_size<sizeof(*config)||config->api_version!=HV_API_VERSION_040||!config->runtime_root_utf8||!config->profile_id_utf8){Message(error,capacity,"Invalid runtime configuration");return HV_ERR_INVALID_ARGUMENT;}
 try{auto session=std::make_unique<RuntimeSession>();std::string message;if(!session->Start(std::filesystem::u8path(config->runtime_root_utf8),config->profile_id_utf8,config->max_people,message)){Message(error,capacity,message);return HV_ERR_INTERNAL;}*out=session.release();Message(error,capacity,"");return HV_OK;}
 catch(const std::exception& e){Message(error,capacity,e.what());return HV_ERR_INTERNAL;}catch(...){Message(error,capacity,"Runtime creation failed");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL HV_RuntimeSubmit(HV_RuntimeHandle handle,const HV_VideoFrame* frame){if(!handle||!frame)return HV_ERR_INVALID_ARGUMENT;try{std::string error;return static_cast<RuntimeSession*>(handle)->Submit(*frame,error)?HV_OK:HV_ERR_INVALID_ARGUMENT;}catch(...){return HV_ERR_INTERNAL;}}
HV_Result HV_CALL HV_RuntimeSetRegions(HV_RuntimeHandle handle,const HV_Rect* regions,uint32_t count,int64_t revision){if(!handle)return HV_ERR_INVALID_ARGUMENT;try{std::string error;return static_cast<RuntimeSession*>(handle)->SetRegions(regions,count,revision,error)?HV_OK:HV_ERR_INVALID_ARGUMENT;}catch(...){return HV_ERR_INTERNAL;}}
HV_Result HV_CALL HV_RuntimeCopy(HV_RuntimeHandle handle,int64_t time,HV_CanonicalBodyV1* bodies,uint32_t capacity,uint32_t* written,HV_RuntimeStatsV1* stats){
 if(written)*written=0;if(!handle||!written||!stats||stats->struct_size<sizeof(*stats)||stats->api_version!=HV_API_VERSION_040||(capacity&&!bodies)||time<0)return HV_ERR_INVALID_ARGUMENT;
 try{auto snapshot=static_cast<RuntimeSession*>(handle)->Copy(time,*stats);if(capacity<snapshot.count)return HV_ERR_INVALID_ARGUMENT;std::copy_n(snapshot.bodies.begin(),snapshot.count,bodies);*written=snapshot.count;return HV_OK;}catch(...){return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL HV_RuntimeGetError(HV_RuntimeHandle handle,char* error,uint32_t capacity){if(!handle||!error||!capacity)return HV_ERR_INVALID_ARGUMENT;try{Message(error,capacity,static_cast<RuntimeSession*>(handle)->LastError());return HV_OK;}catch(...){Message(error,capacity,"Runtime error query failed");return HV_ERR_INTERNAL;}}
void HV_CALL HV_RuntimeDestroy(HV_RuntimeHandle handle){try{delete static_cast<RuntimeSession*>(handle);}catch(...){}}
HV_Result HV_CALL HV_RuntimeGetDiagnostics(HV_RuntimeHandle handle,char* text,uint32_t capacity){if(!handle||!text||!capacity)return HV_ERR_INVALID_ARGUMENT;try{Message(text,capacity,static_cast<RuntimeSession*>(handle)->Diagnostics());return HV_OK;}catch(...){return HV_ERR_INTERNAL;}}
}
