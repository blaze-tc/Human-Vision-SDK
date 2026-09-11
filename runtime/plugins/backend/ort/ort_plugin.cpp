#include "plugins/backend/ort/ort_plugin.h"
#include "backend/onnx/onnx_runtime_backend.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace {
struct Session {
 explicit Session(bool accelerated,bool qnn):backend(accelerated,qnn),requested(accelerated?"accelerated":"CPU"){}
 humanvision::OnnxRuntimeBackend backend;
 std::string requested;
 humanvision::Tensor input;
 std::vector<humanvision::Tensor> outputs;
};
void Error(HV_ErrorBufferV1* out,const char* message) {
 if(!out || out->struct_size<sizeof(*out) || !out->data || !out->capacity)return;
 const auto n=std::min(std::strlen(message),size_t(out->capacity-1));std::memcpy(out->data,message,n);out->data[n]=0;
}
HV_Result CreateSession(const HV_BackendConfigV1* config,void** out,HV_ErrorBufferV1* error,bool accelerated,bool qnn=false) {
 if(!out)return HV_ERR_INVALID_ARGUMENT;*out=nullptr;
 if(!config || config->struct_size<sizeof(*config) || config->api_version!=HV_PLUGIN_API_V1 || !config->model_path_utf8)return HV_ERR_INVALID_ARGUMENT;
 const char* expected="CPU";
#if defined(__ANDROID__)
 if(accelerated)expected="NNAPI";
#elif defined(HV_USE_DIRECTML)
 if(accelerated)expected="DirectML";
#endif
 if(qnn)expected="QNN_HTP";
 if(config->requested_provider_utf8 && *config->requested_provider_utf8 && std::strcmp(config->requested_provider_utf8,expected) && !( !accelerated && !qnn && !std::strcmp(config->requested_provider_utf8,"cpu"))) {
  Error(error,"Backend plugin cannot satisfy the requested provider");return HV_ERR_INVALID_ARGUMENT;
 }
 try {
  auto session=std::make_unique<Session>(accelerated,qnn);session->requested=expected;std::string detail;
  if(!session->backend.Load(std::filesystem::u8path(config->model_path_utf8),detail)){Error(error,detail.c_str());return HV_ERR_MODEL_LOAD;}
  *out=session.release();return HV_OK;
 }catch(const std::exception& e){Error(error,e.what());return HV_ERR_INTERNAL;}
 catch(...){Error(error,"CPU backend creation exception");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL Create(const HV_BackendConfigV1* c,void** out,HV_ErrorBufferV1* e){return CreateSession(c,out,e,false);}
HV_Result HV_CALL CreateAccelerated(const HV_BackendConfigV1* c,void** out,HV_ErrorBufferV1* e){return CreateSession(c,out,e,true);}
HV_Result HV_CALL CreateQnn(const HV_BackendConfigV1* c,void** out,HV_ErrorBufferV1* e){return CreateSession(c,out,e,false,true);}
void HV_CALL Destroy(void* p){delete static_cast<Session*>(p);}
HV_Result HV_CALL Run(void* p,const HV_TensorViewV1* input,uint32_t inputs,HV_TensorViewV1* output,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1* error){
 if(count)*count=0;
 if(!p || !input || inputs!=1 || !output || !capacity || !count || input->struct_size<sizeof(*input) || input->api_version!=HV_PLUGIN_API_V1 ||
  input->element_type!=1 || input->rank<1 || input->rank>8 || !input->data)return HV_ERR_INVALID_ARGUMENT;
 uint64_t elements=1;
 for(uint32_t i=0;i<input->rank;++i){
  if(input->dimensions[i]<=0 || uint64_t(input->dimensions[i])>std::numeric_limits<size_t>::max()/sizeof(float)/elements)return HV_ERR_INVALID_ARGUMENT;
  elements*=uint64_t(input->dimensions[i]);
 }
 if(elements*sizeof(float)!=input->byte_count)return HV_ERR_INVALID_ARGUMENT;
 try {
  auto& session=*static_cast<Session*>(p);auto& tensor=session.input;
  tensor.name=input->name?input->name:"";tensor.shape.assign(input->dimensions,input->dimensions+input->rank);
  tensor.values.resize(size_t(elements));std::memcpy(tensor.values.data(),input->data,size_t(input->byte_count));
  std::string detail;if(!session.backend.Run(tensor,session.outputs,detail)){Error(error,detail.c_str());return HV_ERR_INTERNAL;}
  if(session.outputs.size()>capacity){Error(error,"Output tensor capacity is too small");return HV_ERR_INVALID_ARGUMENT;}
  for(const auto& result:session.outputs)if(result.shape.size()>8){Error(error,"Output tensor rank exceeds ABI limit");return HV_ERR_INTERNAL;}
  for(size_t i=0;i<session.outputs.size();++i){
   const auto& result=session.outputs[i];auto& view=output[i];view={};view.struct_size=sizeof(view);view.api_version=HV_PLUGIN_API_V1;
   view.name=result.name.c_str();view.element_type=1;view.rank=uint32_t(result.shape.size());
   std::copy(result.shape.begin(),result.shape.end(),view.dimensions);view.data=result.values.data();view.byte_count=result.values.size()*sizeof(float);
  }
  *count=uint32_t(session.outputs.size());return HV_OK;
 }catch(const std::exception& e){Error(error,e.what());return HV_ERR_INTERNAL;}
 catch(...){Error(error,"CPU backend inference exception");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL Info(void* p,HV_BackendSessionInfoV1* out){
 if(!p || !out || out->struct_size<sizeof(*out) || out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 try {
  const auto& session=*static_cast<Session*>(p);
  *out={};out->struct_size=sizeof(*out);out->api_version=HV_PLUGIN_API_V1;
  auto copy=[](char* target,size_t size,const std::string& text){const auto n=std::min(size-1,text.size());std::memcpy(target,text.data(),n);target[n]=0;};
  const auto actual=session.backend.ActualProvider();copy(out->requested,sizeof(out->requested),session.requested);
  copy(out->actual,sizeof(out->actual),actual);copy(out->fallback_reason,sizeof(out->fallback_reason),session.backend.FallbackReason());
  out->accelerated=actual=="NNAPI" || actual=="DirectML" || actual=="QNN_HTP";return HV_OK;
 }catch(...){return HV_ERR_INTERNAL;}
}
const HV_BackendApiV1 api{sizeof(api),HV_PLUGIN_API_V1,Create,Destroy,Run,Info};
const HV_BackendApiV1 accelerated_api{sizeof(accelerated_api),HV_PLUGIN_API_V1,CreateAccelerated,Destroy,Run,Info};
const HV_BackendApiV1 qnn_api{sizeof(qnn_api),HV_PLUGIN_API_V1,CreateQnn,Destroy,Run,Info};
}
extern "C" HV_Result HV_CALL HV_QueryOrtAcceleratedPlugin(uint32_t version,HV_PluginApiV1* out){
 if(version!=HV_PLUGIN_API_V1 || !out || out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
#if defined(__ANDROID__)
 const char* id="backend.ort.nnapi";
#elif defined(HV_USE_DIRECTML)
 const char* id="backend.ort.directml";
#else
 return HV_ERR_NOT_INITIALIZED;
#endif
#if defined(__ANDROID__) || defined(HV_USE_DIRECTML)
 *out={sizeof(*out),HV_PLUGIN_API_V1,id,"0.4.0-preview.1",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE,0,nullptr,&accelerated_api,100};return HV_OK;
#endif
}
extern "C" HV_Result HV_CALL HV_QueryOrtCpuPlugin(uint32_t version,HV_PluginApiV1* out){
 if(version!=HV_PLUGIN_API_V1 || !out || out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
 *out={sizeof(*out),HV_PLUGIN_API_V1,"backend.ort.cpu","0.4.0-preview.1",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE,0,nullptr,&api,0};return HV_OK;
}
extern "C" HV_Result HV_CALL HV_QueryOrtQnnPlugin(uint32_t version,HV_PluginApiV1* out){
 if(version!=HV_PLUGIN_API_V1 || !out || out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
#if defined(HV_USE_QNN) && defined(__ANDROID__)
 *out={sizeof(*out),HV_PLUGIN_API_V1,"backend.ort.qnn","0.4.0-preview.1",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE,0,nullptr,&qnn_api,200};return HV_OK;
#else
 return HV_ERR_NOT_INITIALIZED;
#endif
}
