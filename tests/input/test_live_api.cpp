#include "humanvision_input_live.h"
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(false)
int main(){
 CHECK(HV_Input_QueryLiveApiV1(nullptr)==HV_INPUT_INVALID);
 HV_InputLiveApiV1 api{sizeof(api),2};CHECK(HV_Input_QueryLiveApiV1(&api)==HV_INPUT_INVALID);
 api.version=1;CHECK(HV_Input_QueryLiveApiV1(&api)==HV_INPUT_OK);
 CHECK(api.Attach&&api.Configure&&api.GetGeometry&&api.BindTargets&&api.PrepareRenderEvent&&api.GetRenderEventFunc&&api.Poll&&api.ReleaseSlot&&api.RetireTargets&&api.TargetsRetired&&api.Close&&api.TryRelease&&api.GetProgress&&api.GetLastError);
 HV_InputLiveHandle live=reinterpret_cast<HV_InputLiveHandle>(1);
 HV_InputLiveConfigV1 config{sizeof(config),1,1920,1080,0,0,1,0};
 CHECK(api.Attach(nullptr,&config,&live)!=HV_INPUT_OK);CHECK(live==nullptr);
 CHECK(api.GetRenderEventFunc()==nullptr); // Host must not pretend to be Android Vulkan.
 HV_InputSourceProgressV1 progress{sizeof(progress),1};CHECK(HV_Input_GetSourceProgressV1(nullptr,&progress)==HV_INPUT_INVALID);
 std::cout<<"live C ABI and explicit unsupported host contract passed\n";
}
