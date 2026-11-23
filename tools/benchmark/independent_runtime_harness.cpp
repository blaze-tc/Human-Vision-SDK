#include "humanvision/humanvision_runtime_control.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
using Clock=std::chrono::steady_clock;
int64_t Us(){return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();}
bool Done(const HV_RuntimeControlApiV1& api,HV_RuntimeControllerHandle handle,uint64_t request){
 HV_RuntimeRequestStatusV1 status{};status.struct_size=sizeof(status);status.api_version=1;
 const auto deadline=Clock::now()+std::chrono::seconds(120);
 do {
  if(api.GetRequestStatus(handle,request,&status)!=0)return false;
  if(status.status==HV_REQUEST_SUCCEEDED)return true;
  if(status.status>=HV_REQUEST_FAILED){std::cerr<<status.message<<'\n';return false;}
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }while(Clock::now()<deadline);
 std::cerr<<"Request did not retire within harness observation deadline\n";return false;
}
}
int main(int argc,char** argv){
 if(argc<6){std::cerr<<"usage: hv_independent_runtime ROOT PROFILE INPUT_LIBRARY RTSP_URL SECONDS [HARDWARE_AVC_CODEC]\n";return 2;}
 const int seconds=std::atoi(argv[5]);if(seconds<3||seconds>86400)return 2;
 HV_RuntimeControlApiV1 api{};api.struct_size=sizeof(api);api.api_version=1;
 if(HV_QueryRuntimeControlApiV1(1,&api)!=0){std::cerr<<"Control API unavailable\n";return 3;}
 HV_RuntimeControllerConfigV1 config{};config.struct_size=sizeof(config);config.api_version=1;
 config.runtime={sizeof(HV_RuntimeConfigV1),HV_API_VERSION_040,argv[1],argv[2],4,0};
 config.execution_policy=HV_EXECUTION_INDEPENDENT_REQUIRED;config.retirement_timeout_ms=5000;
 HV_RuntimeControllerHandle handle=nullptr;char message[2048]{};
 if(api.CreateDeferred(&config,&handle,message,sizeof(message))!=0){std::cerr<<message<<'\n';return 3;}
 HV_RuntimeSourceV1 source{};source.struct_size=sizeof(source);source.api_version=1;source.kind=HV_NATIVE_SOURCE_RTSP;
 source.library_utf8=argv[3];source.url_utf8=argv[4];source.hardware_avc_codec_utf8=argc>6?argv[6]:nullptr;
 source.source_id=1;source.width=512;source.height=288;source.timeout_ms=5000;source.reconnect_delay_ms=1000;source.transport_tcp=1;
 uint64_t request=0;bool ok=api.RequestSource(handle,&source,&request)==0&&Done(api,handle,request);
 if(ok)ok=api.RequestStart(handle,&request)==0&&Done(api,handle,request);
 uint64_t first=0,last=0;const auto begin=Us();
 if(ok){
  HV_RuntimeCapabilitiesV1 caps{};caps.struct_size=sizeof(caps);caps.api_version=1;
  if(api.GetCapabilities(handle,&caps)==0)std::cout<<"capability="<<caps.capability_level<<" source="<<caps.source<<" image_service="<<caps.image_service<<" backend="<<caps.backend<<'\n';
  for(int i=0;i<=seconds;++i){
   HV_RuntimeStatsV3 stats{};stats.struct_size=sizeof(stats);stats.api_version=3;
   if(api.GetStatsV3(handle,&stats)!=0){ok=false;break;}
   if(i==0)first=stats.runtime.fresh_observation_frames;last=stats.runtime.fresh_observation_frames;
   std::cout<<"elapsed_us="<<Us()-begin<<" input="<<stats.native_input_frames<<" fresh="<<last
    <<" input_frame="<<stats.frame_id<<" canonical_frame="<<stats.runtime.source_frame_id<<" state="<<stats.state<<" input_errors="<<stats.input_errors<<" runtime_errors="<<stats.runtime_errors<<std::endl;
   if(i<seconds)std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  // No Copy/read journal/Unity calls were made in this window.
  ok=ok&&last>first;
 }
 api.RequestClose(handle,&request);
 const auto release_deadline=Clock::now()+std::chrono::seconds(30);
 int status=HV_CONTROL_BUSY;
 while(status==HV_CONTROL_BUSY&&Clock::now()<release_deadline){status=api.TryRelease(handle);if(status==HV_CONTROL_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 if(status!=0){std::cerr<<"Owners remain retiring; retained handle, no forced unload\n";return 5;}
 return ok?0:4;
}
