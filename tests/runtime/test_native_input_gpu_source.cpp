#include "composition/native_input_gpu_source.h"
#include <gtest/gtest.h>
#include <algorithm>
using namespace humanvision::runtime;
namespace {
struct Input {
 uint64_t sequence=0;int releases=0;bool available=true;
 static Input& Get(HV_InputHandle handle){return *reinterpret_cast<Input*>(handle);}
 static int HV_INPUT_CALL Device(HV_InputHandle,HV_InputNativeDeviceV1* out){out->ahb_enabled=out->sync_fd_enabled=1;std::fill_n(out->device_uuid,16,1);std::fill_n(out->driver_uuid,16,2);return 0;}
 static int HV_INPUT_CALL Pool(HV_InputHandle,HV_InputNativePoolV1* out){out->width=16;out->height=8;out->format=1;out->slot_count=3;for(int i=0;i<3;++i)out->hardware_buffers[i]=reinterpret_cast<void*>(uintptr_t(i+1));return 0;}
 static int HV_INPUT_CALL Acquire(HV_InputHandle handle,uint64_t after,HV_InputNativeFrameV1* out){auto& in=Get(handle);if(!in.available||in.sequence<=after)return HV_INPUT_NO_FRAME;out->frame.sequence=in.sequence;out->frame.generation=7;out->frame.width=16;out->frame.height=8;out->frame.decoded_timestamp_us=100;out->slot=0;out->hardware_buffer=reinterpret_cast<void*>(1);out->producer_sync_fd=-1;return 0;}
 static int HV_INPUT_CALL Release(HV_InputHandle handle,uint32_t,uint64_t,uint64_t){++Get(handle).releases;return 0;}
 NativeInputGpuApi Api(){return {Device,Pool,Acquire,Release};}
};
}
TEST(NativeGpuProducer, IdentityAndLeaseAreIndependentOfUnity) {
 Input input;NativeInputGpuSource source(reinterpret_cast<HV_InputHandle>(&input),input.Api(),900,42,3);
 std::string error;ASSERT_TRUE(source.Initialize(error))<<error;input.sequence=1;
 humanvision::gpu::ConsumerFrame frame;ASSERT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Ok);
 EXPECT_EQ(frame.metadata.timestamp_us,1000);EXPECT_EQ(frame.metadata.source_id,42);EXPECT_EQ(frame.metadata.config_epoch,3);
 EXPECT_EQ(frame.producer,&source);EXPECT_EQ(source.Generation(),7);EXPECT_EQ(source.Width(),16);
 EXPECT_EQ(source.DrainUnsubmitted(frame),true);EXPECT_EQ(input.releases,1);
 EXPECT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::NoReady);
}
TEST(NativeGpuProducer, QuarantineKeepsUnprovedInputLease) {
 Input input;NativeInputGpuSource source(reinterpret_cast<HV_InputHandle>(&input),input.Api(),0,42,3);
 std::string error;ASSERT_TRUE(source.Initialize(error));input.sequence=1;
 humanvision::gpu::ConsumerFrame frame;ASSERT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Ok);
 source.Quarantine(frame);EXPECT_TRUE(source.IsQuarantined());EXPECT_EQ(input.releases,0);
 EXPECT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Closed);
}
TEST(NativeGpuProducer, AChangedInputClockNeverUsesThePreviousOffset) {
 Input input;NativeInputGpuSource source(reinterpret_cast<HV_InputHandle>(&input),input.Api(),900,42,3,nullptr,nullptr,123);
 std::string error;ASSERT_TRUE(source.Initialize(error));input.sequence=1;
 humanvision::gpu::ConsumerFrame frame;
 EXPECT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Invalid);
 EXPECT_FALSE(frame.claimed);EXPECT_EQ(input.releases,1);
 EXPECT_EQ(source.ErrorCount(),1);EXPECT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Closed);EXPECT_EQ(source.ErrorCount(),1);
}
