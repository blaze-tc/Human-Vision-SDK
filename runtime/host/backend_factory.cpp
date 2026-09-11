#include "host/backend_factory.h"
#include <algorithm>
#include <cstring>
namespace humanvision::runtime {
namespace {
void Copy(char* to,size_t capacity,const std::string& from){if(!capacity)return;const auto n=std::min(capacity-1,from.size());std::memcpy(to,from.data(),n);to[n]=0;}
void Error(HV_ErrorBufferV1* out,const std::string& text){if(out&&out->struct_size>=sizeof(*out)&&out->data)Copy(out->data,out->capacity,text);}
struct Lease {
 std::shared_ptr<const PluginModule> module;
 void* instance=nullptr;
 std::string failures;
 ~Lease(){if(instance)try{module->api.backend->destroy(instance);}catch(...){}}
};
HV_Result HV_CALL Run(void* p,const HV_TensorViewV1* in,uint32_t n,HV_TensorViewV1* out,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1* error){
 if(!p)return HV_ERR_INVALID_ARGUMENT;
 auto& lease=*static_cast<Lease*>(p);
 try{return lease.module->api.backend->run(lease.instance,in,n,out,capacity,count,error);}
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
   try{result=module->api.backend->create(config,&lease->instance,&buffer);}catch(...){Copy(text,sizeof(text),"creation threw across C ABI");}
   text[sizeof(text)-1]=0;
   if(result==HV_OK&&lease->instance){lease->failures=failures;*api=&table;*out=lease.release();return HV_OK;}
   failures+=std::string(module->api.plugin_id)+": "+(text[0]?text:"creation failed")+"; ";
  }
  Error(error,failures.empty()?"No backend candidates available":failures);return HV_ERR_MODEL_LOAD;
 }catch(...){Error(error,"Backend selection exception");return HV_ERR_INTERNAL;}
}
void HV_CALL BackendFactory::Release(void*,const HV_BackendApiV1* api,void* instance){if(api==&table)Destroy(instance);}
}
