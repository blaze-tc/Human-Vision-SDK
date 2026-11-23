#include "composition/native_live_bridge.h"
#include "humanvision/humanvision_runtime_control.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
using namespace humanvision::runtime;
namespace {
struct LiveInput {
    bool configured_event=false,closed=false,released=false,held=false;
    int attached=0,callbacks=0;
    uint64_t target=101,sequence=9000,generation=3;
    static LiveInput* last;
    static LiveInput& Get(HV_InputLiveHandle h){return *reinterpret_cast<LiveInput*>(h);}
    static int HV_INPUT_CALL Attach(HV_InputHandle input,const HV_InputLiveConfigV1* config,HV_InputLiveHandle* out){
        auto& self=*reinterpret_cast<LiveInput*>(input);EXPECT_TRUE(self.configured_event);
        EXPECT_EQ(config->max_width,1920u);EXPECT_EQ(config->max_height,1080u);++self.attached;
        *out=reinterpret_cast<HV_InputLiveHandle>(&self);return HV_INPUT_OK;
    }
    static int HV_INPUT_CALL Configure(HV_InputLiveHandle,const HV_InputLiveConfigV1*){return HV_INPUT_OK;}
    static int HV_INPUT_CALL Geometry(HV_InputLiveHandle h,HV_InputLiveGeometryV1* out){
        *out={sizeof(*out),1,Get(h).generation,1920,1080,1920,1080,0,0};return HV_INPUT_OK;
    }
    static int HV_INPUT_CALL Bind(HV_InputLiveHandle h,const HV_InputLiveTargetsV1* value){
        Get(h).target=value->target_generation;EXPECT_NE(value->textures[0],nullptr);return HV_INPUT_OK;
    }
    static int HV_INPUT_CALL Prepare(HV_InputLiveHandle h){++Get(h).callbacks;return HV_INPUT_OK;}
    static void HV_INPUT_CALL Render(int id,void* h){EXPECT_EQ(id,HV_RUNTIME_LIVE_RENDER_EVENT_ID);--static_cast<LiveInput*>(h)->callbacks;}
    static HV_InputLiveRenderEvent HV_INPUT_CALL Event(){last->configured_event=true;return Render;}
    static int HV_INPUT_CALL Poll(HV_InputLiveHandle h,uint64_t after,HV_InputLiveFrameV1* out){
        auto& self=Get(h);if(self.held||after>=self.sequence)return HV_INPUT_NO_FRAME;
        *out={sizeof(*out),1};out->frame={sizeof(out->frame),1};out->frame.sequence=self.sequence;out->frame.generation=self.generation;
        out->frame.clock_id=42;out->frame.decoded_timestamp_us=100;
        out->frame.width=out->source_width=1920;out->frame.height=out->source_height=1080;
        out->target_generation=self.target;out->slot=2;self.held=true;return HV_INPUT_OK;
    }
    static int HV_INPUT_CALL ReleaseSlot(HV_InputLiveHandle h,uint32_t slot,uint64_t target,uint64_t sequence){
        auto& self=Get(h);if(slot!=2||target!=self.target||sequence!=self.sequence||!self.held)return HV_INPUT_INVALID;
        self.held=false;return HV_INPUT_OK;
    }
    static int HV_INPUT_CALL Retire(HV_InputLiveHandle){return HV_INPUT_OK;}
    static int HV_INPUT_CALL Retired(HV_InputLiveHandle h){return Get(h).held?HV_INPUT_BUSY:HV_INPUT_OK;}
    static int HV_INPUT_CALL Close(HV_InputLiveHandle h){Get(h).closed=true;return HV_INPUT_OK;}
    static int HV_INPUT_CALL Release(HV_InputLiveHandle h){auto& self=Get(h);if(!self.closed||self.held||self.callbacks)return HV_INPUT_BUSY;self.released=true;return HV_INPUT_OK;}
    static int HV_INPUT_CALL Progress(HV_InputLiveHandle,HV_InputSourceProgressV1*){return HV_INPUT_OK;}
    static int HV_INPUT_CALL Error(HV_InputLiveHandle,char*,uint32_t){return HV_INPUT_OK;}
    HV_InputLiveApiV1 Api(){last=this;return {sizeof(HV_InputLiveApiV1),1,Attach,Configure,Geometry,Bind,Prepare,Event,Poll,ReleaseSlot,Retire,Retired,Close,Release,Progress,Error};}
    int Create(HV_RuntimeLiveBindingV1& binding,std::shared_ptr<void> module={}){
        HV_RuntimeLiveConfigV1 config{sizeof(config),1,1920,1080,0,0,1,0};
        return AttachNativeLiveBridge(Api(),reinterpret_cast<HV_InputHandle>(this),config,77,9,1000,42,std::move(module),binding);
    }
};
LiveInput* LiveInput::last=nullptr;
}
TEST(NativeLiveBridge, RetainsTheSameSourceAndModuleUntilQueuedCallbacksAndHeldTargetsReallyRetire) {
    LiveInput input;bool unloaded=false;
    auto module=std::shared_ptr<void>(new int(1),[&](void* p){EXPECT_TRUE(input.released);unloaded=true;delete static_cast<int*>(p);});
    HV_RuntimeLiveBindingV1 binding{sizeof(binding),1};ASSERT_EQ(input.Create(binding,module),HV_CONTROL_OK);module.reset();
    EXPECT_FALSE(unloaded);EXPECT_EQ(input.attached,1);EXPECT_EQ(binding.source_id,77u);EXPECT_EQ(binding.config_epoch,9u);
    EXPECT_EQ(binding.clock_offset_us,1000);EXPECT_EQ(binding.clock_id,42u);
    auto& api=*binding.api;ASSERT_EQ(api.PrepareRenderEvent(binding.consumer),HV_INPUT_OK);
    HV_RuntimeLiveFrameV1 frame{sizeof(frame),1};ASSERT_EQ(api.Poll(binding.consumer,0,&frame),HV_INPUT_OK);
    ASSERT_EQ(api.Close(binding.consumer),HV_INPUT_OK);EXPECT_EQ(api.TryRelease(binding.consumer),HV_INPUT_BUSY);EXPECT_FALSE(unloaded);
    api.GetRenderEventFunc()(HV_RUNTIME_LIVE_RENDER_EVENT_ID,binding.consumer);
    EXPECT_EQ(api.TryRelease(binding.consumer),HV_INPUT_BUSY)<<"A completed callback does not release a sampled Unity texture";
    EXPECT_EQ(api.ReleaseSlot(binding.consumer,frame.slot,frame.frame.generation,frame.frame.sequence),HV_INPUT_INVALID);
    ASSERT_EQ(api.ReleaseSlot(binding.consumer,frame.slot,frame.target_generation,frame.frame.sequence),HV_INPUT_OK);
    ASSERT_EQ(api.TryRelease(binding.consumer),HV_INPUT_OK);EXPECT_TRUE(unloaded);
}
TEST(NativeLiveBridge, FrameIdentityUsesTheDecodeEnvelopeRatherThanLivePublicationCount) {
    LiveInput input;HV_RuntimeLiveBindingV1 binding{sizeof(binding),1};ASSERT_EQ(input.Create(binding),HV_CONTROL_OK);
    HV_RuntimeLiveGeometryV1 geometry{sizeof(geometry),1};ASSERT_EQ(binding.api->GetGeometry(binding.consumer,&geometry),0);
    EXPECT_EQ(geometry.generation,3u);
    HV_RuntimeLiveFrameV1 frame{sizeof(frame),1};ASSERT_EQ(binding.api->Poll(binding.consumer,0,&frame),0);
    EXPECT_EQ(frame.frame.sequence,9000u);EXPECT_EQ(frame.frame.generation,3u);EXPECT_EQ(frame.target_generation,101u);
    EXPECT_EQ(frame.frame.width,1920u);EXPECT_EQ(frame.frame.height,1080u);
    ASSERT_EQ(binding.api->ReleaseSlot(binding.consumer,frame.slot,frame.target_generation,frame.frame.sequence),0);
    ASSERT_EQ(binding.api->Close(binding.consumer),0);ASSERT_EQ(binding.api->TryRelease(binding.consumer),0);
}
