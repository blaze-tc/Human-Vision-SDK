#include "composition/native_input_gpu_source.h"
#include "composition/native_frame_geometry.h"
#include <gtest/gtest.h>
#include <algorithm>
using namespace humanvision::runtime;
namespace {
struct Input {
 uint64_t sequence=0;int releases=0,wait_calls=0;uint32_t wait_timeout=0;bool available=true,asymmetric=false;
 static Input& Get(HV_InputHandle handle){return *reinterpret_cast<Input*>(handle);}
 static int HV_INPUT_CALL Device(HV_InputHandle,HV_InputNativeDeviceV1* out){out->ahb_enabled=out->sync_fd_enabled=1;std::fill_n(out->device_uuid,16,1);std::fill_n(out->driver_uuid,16,2);return 0;}
 static int HV_INPUT_CALL Pool(HV_InputHandle,HV_InputNativePoolV1* out){out->width=16;out->height=8;out->format=1;out->slot_count=3;for(int i=0;i<3;++i)out->hardware_buffers[i]=reinterpret_cast<void*>(uintptr_t(i+1));return 0;}
 static int HV_INPUT_CALL Acquire(HV_InputHandle handle,uint64_t after,HV_InputNativeFrameV1* out){auto& in=Get(handle);if(!in.available||in.sequence<=after)return HV_INPUT_NO_FRAME;out->frame.sequence=in.sequence;out->frame.generation=7;out->frame.width=16;out->frame.height=8;out->frame.decoded_timestamp_us=100;out->slot=0;out->hardware_buffer=reinterpret_cast<void*>(1);out->producer_sync_fd=-1;out->source_width=16;out->source_height=8;out->crop_right=16;out->crop_bottom=8;
 if(in.asymmetric){out->source_width=10;out->crop_left=2;out->crop_top=1;out->crop_right=8;out->crop_bottom=5;out->rotation_degrees=90;out->mirror_x=1;}
 return 0;}
 static int HV_INPUT_CALL Release(HV_InputHandle handle,uint32_t,uint64_t,uint64_t){++Get(handle).releases;return 0;}
 static int HV_INPUT_CALL Wait(HV_InputHandle handle,uint64_t after,uint32_t timeout,HV_InputNativeFrameV1* out){++Get(handle).wait_calls;Get(handle).wait_timeout=timeout;return Acquire(handle,after,out);}
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
TEST(NativeGpuProducer, KeepsPhysicalCropRotationAndMirrorMapping) {
 Input input;input.asymmetric=true;
 NativeInputGpuSource source(reinterpret_cast<HV_InputHandle>(&input),input.Api(),900,42,3);
 std::string error;ASSERT_TRUE(source.Initialize(error));input.sequence=1;
 humanvision::gpu::ConsumerFrame frame;ASSERT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Ok);
 const auto& m=frame.source_envelope.source_to_analysis;
 EXPECT_TRUE(frame.source_envelope.source_geometry_valid);
 EXPECT_EQ(frame.source_envelope.source_width,10);EXPECT_EQ(frame.source_envelope.source_height,8);
 EXPECT_EQ(frame.source_envelope.source_crop.x,2);EXPECT_EQ(frame.source_envelope.source_crop.y,1);
 EXPECT_EQ(frame.source_envelope.applied_rotation_degrees,90);EXPECT_TRUE(frame.source_envelope.applied_mirror_x);
 EXPECT_EQ(frame.source_envelope.rotation_degrees,0);EXPECT_FALSE(frame.source_envelope.mirrored);
 EXPECT_FLOAT_EQ(m[0]*2+m[1]*1+m[2],0);EXPECT_FLOAT_EQ(m[3]*2+m[4]*1+m[5],0);
 EXPECT_FLOAT_EQ(m[0]*8+m[1]*1+m[2],0);EXPECT_FLOAT_EQ(m[3]*8+m[4]*1+m[5],8);
 EXPECT_FLOAT_EQ(m[0]*2+m[1]*5+m[2],16);EXPECT_FLOAT_EQ(m[3]*2+m[4]*5+m[5],0);
 EXPECT_TRUE(source.DrainUnsubmitted(frame));
}
TEST(NativeGpuProducer, CpuAndGpuMappingUseTheSameNativeContract) {
 for(uint32_t rotation:{0u,90u,180u,270u})for(uint32_t mirror:{0u,1u}){
  HV_InputNativeFrameV1 native{};native.source_width=10;native.source_height=8;native.crop_left=2;native.crop_top=1;native.crop_right=8;native.crop_bottom=5;
  native.frame.width=16;native.frame.height=8;native.rotation_degrees=rotation;native.mirror_x=mirror;
  FrameEnvelope envelope;ASSERT_TRUE(BindNativeImageGeometry(native,envelope));
  const auto& m=envelope.source_to_analysis;
  for(const auto& p:std::array<std::array<float,2>,4>{{{2,1},{8,1},{2,5},{8,5}}}){
   const float ox=m[0]*p[0]+m[1]*p[1]+m[2],oy=m[3]*p[0]+m[4]*p[1]+m[5];
   float u=ox/16,v=oy/8;if(mirror)u=1-u;
   if(rotation==90){float a=u;u=v;v=1-a;}else if(rotation==180){u=1-u;v=1-v;}else if(rotation==270){float a=u;u=1-v;v=a;}
   EXPECT_NEAR(2+u*6,p[0],1e-5F);EXPECT_NEAR(1+v*4,p[1],1e-5F);
  }
 }
 HV_InputNativeFrameV1 invalid{};FrameEnvelope envelope;
 EXPECT_FALSE(BindNativeImageGeometry(invalid,envelope));EXPECT_FALSE(envelope.source_geometry_valid);
}
TEST(NativeGpuProducer, NativeInputWaitsOnItsProducerEvent) {
 Input input;auto api=input.Api();api.wait=Input::Wait;
 NativeInputGpuSource source(reinterpret_cast<HV_InputHandle>(&input),api,0,42,3);
 std::string error;ASSERT_TRUE(source.Initialize(error));input.sequence=1;
 humanvision::gpu::ConsumerFrame frame;ASSERT_EQ(source.Claim(frame),humanvision::gpu::SlotResult::Ok);
 EXPECT_EQ(input.wait_calls,1);EXPECT_EQ(input.wait_timeout,50);EXPECT_TRUE(source.DrainUnsubmitted(frame));
}
