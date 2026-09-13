#include "host/backend_factory.h"
#include <algorithm>
#include <cstring>
#include <chrono>
namespace humanvision::runtime {
namespace {
void Copy(char* to,size_t capacity,const std::string& from){if(!capacity)return;const auto n=std::min(capacity-1,from.size());std::memcpy(to,from.data(),n);to[n]=0;}
void Error(HV_ErrorBufferV1* out,const std::string& text){if(out&&out->struct_size>=sizeof(*out)&&out->data)Copy(out->data,out->capacity,text);}
struct Lease {
 std::shared_ptr<const PluginModule> module;
 void* instance=nullptr;
 std::string failures;
 std::shared_ptr<BackendDiagnostic> diagnostic;
 ~Lease(){if(instance)try{module->api.backend->destroy(instance);}catch(...){}}
};
HV_Result HV_CALL Run(void* p,const HV_TensorViewV1* in,uint32_t n,HV_TensorViewV1* out,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1* error){
 if(!p)return HV_ERR_INVALID_ARGUMENT;
 auto& lease=*static_cast<Lease*>(p);
 try{
  auto started=std::chrono::steady_clock::now();
  auto result=lease.module->api.backend->run(lease.instance,in,n,out,capacity,count,error);
  if(lease.diagnostic){HV_BackendSessionInfoV1 info{};info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
   if(lease.module->api.backend->session_info(lease.instance,&info)==HV_OK){std::lock_guard<std::mutex> lock(lease.diagnostic->mutex);lease.diagnostic->info=info;lease.diagnostic->inference_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-started).count();}}
  return result;
 }
 catch(...){if(count)*count=0;Error(error,"Backend run threw across C ABI");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL Info(void* p,HV_BackendSessionInfoV1* out){
 if(!p||!out||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 auto& lease=*static_cast<Lease*>(p);
 try{
  auto result=lease.module->api.backend->session_info(lease.instance,out);
  if(result==HV_OK){out->fallback_reason[sizeof(out->fallback_reason)-1]=0;Copy(out->fallback_reason,sizeof(out->fallback_reason),lease.failures+out->fallback_reason);}
  return result;
 }catch(...){return HV_ERR_INTERNAL;}
}
void HV_CALL Destroy(void* p){delete static_cast<Lease*>(p);}
HV_Result HV_CALL NoCreate(const HV_BackendConfigV1*,void**,HV_ErrorBufferV1*){return HV_ERR_INVALID_ARGUMENT;}
const HV_BackendApiV1 table{sizeof(table),HV_PLUGIN_API_V1,NoCreate,Destroy,Run,Info};
}
HV_HostServicesV1 BackendFactory::Services(){return {sizeof(HV_HostServicesV1),HV_PLUGIN_API_V1,this,Create,Release};}
HV_Result HV_CALL BackendFactory::Create(void* context,const HV_BackendConfigV1* config,const HV_BackendApiV1** api,void** out,HV_ErrorBufferV1* error){
 if(api)*api=nullptr;if(out)*out=nullptr;
 if(!context||!config||!api||!out||config->struct_size<sizeof(*config)||config->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 try{
  auto& factory=*static_cast<BackendFactory*>(context);std::string failures;
  for(const auto& module:factory.candidates_){
   if(!module||module->api.type!=HV_PLUGIN_BACKEND)continue;
   auto lease=std::make_unique<Lease>();lease->module=module;
   char text[512]{};HV_ErrorBufferV1 buffer{sizeof(buffer),HV_PLUGIN_API_V1,text,sizeof(text)};
   HV_Result result=HV_ERR_INTERNAL;
   HV_BackendConfigV1 selected=*config;
   if(!factory.allow_fallback_)selected.requested_provider_utf8=module->api.plugin_id;
   try{result=module->api.backend->create(&selected,&lease->instance,&buffer);}catch(...){Copy(text,sizeof(text),"creation threw across C ABI");}
   text[sizeof(text)-1]=0;
   if(result==HV_OK&&lease->instance){
    lease->failures=failures;lease->diagnostic=std::make_shared<BackendDiagnostic>();
    lease->diagnostic->creation_failures=failures;
    lease->diagnostic->model_name=config->model_path_utf8?std::filesystem::u8path(config->model_path_utf8).filename().u8string():"model";
    auto& info=lease->diagnostic->info;info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
    module->api.backend->session_info(lease->instance,&info);
    {std::lock_guard<std::mutex> lock(factory.diagnostics_mutex_);factory.diagnostics_.push_back(lease->diagnostic);}
    *api=&table;*out=lease.release();return HV_OK;
   }
   failures+=std::string(module->api.plugin_id)+": "+(text[0]?text:"creation failed")+"; ";
   if(!factory.allow_fallback_)break;
  }
  Error(error,failures.empty()?"No backend candidates available":failures);return HV_ERR_MODEL_LOAD;
 }catch(...){Error(error,"Backend selection exception");return HV_ERR_INTERNAL;}
}
void HV_CALL BackendFactory::Release(void*,const HV_BackendApiV1* api,void* instance){if(api==&table)Destroy(instance);}
std::string BackendFactory::Diagnostics() const {
 std::lock_guard<std::mutex> lock(diagnostics_mutex_);std::string result;
 for(const auto& diagnostic:diagnostics_){std::lock_guard<std::mutex> item(diagnostic->mutex);auto info=diagnostic->info;
  info.requested[sizeof(info.requested)-1]=0;info.actual[sizeof(info.actual)-1]=0;info.fallback_reason[sizeof(info.fallback_reason)-1]=0;
  if(!result.empty())result+=" | ";result+=diagnostic->model_name+": "+info.requested+" -> "+info.actual+" / "+std::to_string(int(diagnostic->inference_ms))+" ms";
  if(!diagnostic->creation_failures.empty()||info.fallback_reason[0])result+=" ("+diagnostic->creation_failures+info.fallback_reason+")";}
 return result;
}
}
