#pragma once
#include "humanvision_plugin.h"
#include "backend/i_inference_backend.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <limits>
namespace humanvision::runtime {
// Internal adapter for legacy model code; no C++ object crosses plugin ABI.
class PluginBackend final:public IInferenceBackend {
public:
 explicit PluginBackend(HV_HostServicesV1 services):services_(services){}
 ~PluginBackend() override {if(instance_)services_.release_backend(services_.context,api_,instance_);}
 bool Load(const std::filesystem::path& path,std::string& error) override {
  const auto name=path.u8string();HV_BackendConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,name.c_str(),nullptr,nullptr};
  char message[512]{};HV_ErrorBufferV1 buffer{sizeof(buffer),HV_PLUGIN_API_V1,message,sizeof(message)};
  const HV_BackendApiV1* api=nullptr;void* instance=nullptr;
  if(services_.create_backend(services_.context,&config,&api,&instance,&buffer)!=HV_OK){error=message;return false;}
  if(instance_)services_.release_backend(services_.context,api_,instance_);
  api_=api;instance_=instance;return true;
 }
 bool Run(const Tensor& input,std::vector<Tensor>& outputs,std::string& error) override {
  if(!instance_||input.shape.empty()||input.shape.size()>8){error="Invalid backend state or input rank";return false;}
  HV_TensorViewV1 view{};view.struct_size=sizeof(view);view.api_version=HV_PLUGIN_API_V1;view.name=input.name.c_str();view.element_type=1;
  view.rank=uint32_t(input.shape.size());std::copy(input.shape.begin(),input.shape.end(),view.dimensions);view.data=input.values.data();view.byte_count=input.values.size()*sizeof(float);
  std::array<HV_TensorViewV1,16> result{};uint32_t count=0;char message[512]{};HV_ErrorBufferV1 buffer{sizeof(buffer),HV_PLUGIN_API_V1,message,sizeof(message)};
  if(api_->run(instance_,&view,1,result.data(),uint32_t(result.size()),&count,&buffer)!=HV_OK){error=message;return false;}
  if(count>result.size()){error="Backend exceeded output capacity";return false;}
  for(uint32_t i=0;i<count;++i){const auto& item=result[i];if(item.element_type!=1||item.rank>8||item.byte_count%sizeof(float)||(!item.data&&item.byte_count)||item.byte_count>std::numeric_limits<size_t>::max()){error="Invalid float output tensor";return false;}}
  outputs.resize(count);
  for(uint32_t i=0;i<count;++i){const auto& item=result[i];auto& tensor=outputs[i];tensor.name=item.name?item.name:"";tensor.shape.assign(item.dimensions,item.dimensions+item.rank);tensor.values.resize(size_t(item.byte_count)/sizeof(float));if(item.byte_count)std::memcpy(tensor.values.data(),item.data,size_t(item.byte_count));}
  return true;
 }
private:
 HV_HostServicesV1 services_;const HV_BackendApiV1* api_=nullptr;void* instance_=nullptr;
};
}
