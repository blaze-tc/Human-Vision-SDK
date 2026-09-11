#include "plugins/backend/ort/ort_plugin.h"
#include "backend/onnx/onnx_runtime_backend.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace {
struct Session {
 humanvision::OnnxRuntimeBackend backend;
 humanvision::Tensor input;
 std::vector<humanvision::Tensor> outputs;
};
void Error(HV_ErrorBufferV1* out,const char* message) {
 if(!out || out->struct_size<sizeof(*out) || !out->data || !out->capacity)return;
 const auto n=std::min(std::strlen(message),size_t(out->capacity-1));std::memcpy(out->data,message,n);out->data[n]=0;
}
HV_Result HV_CALL Create(const HV_BackendConfigV1* config,void** out,HV_ErrorBufferV1* error) {
 if(!out)return HV_ERR_INVALID_ARGUMENT;*out=nullptr;
 if(!config || config->struct_size<sizeof(*config) || config->api_version!=HV_PLUGIN_API_V1 || !config->model_path_utf8)return HV_ERR_INVALID_ARGUMENT;
 if(config->requested_provider_utf8 && *config->requested_provider_utf8 && std::strcmp(config->requested_provider_utf8,"cpu") && std::strcmp(config->requested_provider_utf8,"CPU")) {
  Error(error,"CPU plugin cannot satisfy the requested provider");return HV_ERR_INVALID_ARGUMENT;
 }
 try {
  auto session=std::make_unique<Session>();std::string detail;
  if(!session->backend.Load(std::filesystem::u8path(config->model_path_utf8),detail)){Error(error,detail.c_str());return HV_ERR_MODEL_LOAD;}
  *out=session.release();return HV_OK;
 }catch(const std::exception& e){Error(error,e.what());return HV_ERR_INTERNAL;}
 catch(...){Error(error,"CPU backend creation exception");return HV_ERR_INTERNAL;}
}
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
 *out={};out->struct_size=sizeof(*out);out->api_version=HV_PLUGIN_API_V1;std::memcpy(out->requested,"CPU",4);std::memcpy(out->actual,"CPU",4);return HV_OK;
}
const HV_BackendApiV1 api{sizeof(api),HV_PLUGIN_API_V1,Create,Destroy,Run,Info};
}
extern "C" HV_Result HV_CALL HV_QueryOrtCpuPlugin(uint32_t version,HV_PluginApiV1* out){
 if(version!=HV_PLUGIN_API_V1 || !out || out->struct_size<sizeof(*out))return HV_ERR_INVALID_ARGUMENT;
 *out={sizeof(*out),HV_PLUGIN_API_V1,"backend.ort.cpu","0.4.0-preview.1",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE,0,nullptr,&api,0};return HV_OK;
}
