#include "host/runtime_host.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
using namespace humanvision::runtime;
namespace {
struct Trace {
 std::atomic<int> legacy{0},prepared{0};
 HV_ImageIdentityV1 identity{};
 std::thread::id describe_thread,process_thread;
};
HV_Result HV_CALL Create(const HV_PipelineConfigV1*,const HV_HostServicesV1* host,void** out,HV_ErrorBufferV1*){*out=host->context;return HV_OK;}
void HV_CALL Destroy(void*){}
HV_Result HV_CALL Legacy(void* p,const HV_PipelineInputV1*,HV_PipelineOutputV1* out,HV_ErrorBufferV1*){
 ++static_cast<Trace*>(p)->legacy;out->body_count=out->hand_count=0;return HV_OK;
}
HV_Result HV_CALL Describe(void* p,const HV_ImageDescriptorV1* source,HV_ImageTransformPlanV1* out,HV_ErrorBufferV1*){
 auto& trace=*static_cast<Trace*>(p);trace.describe_thread=std::this_thread::get_id();*out=IdentityImagePlan(*source,123);return HV_OK;
}
HV_Result HV_CALL Prepared(void* p,const HV_PipelineInputV1*,const HV_ImageTransformPlanV1* plan,const HV_PreparedImageLeaseV1* lease,HV_PipelineOutputV1* out,HV_ErrorBufferV1*){
 if(ValidatePreparedImage(*plan,*lease)!=HV_OK)return HV_ERR_INVALID_ARGUMENT;
 auto& trace=*static_cast<Trace*>(p);trace.process_thread=std::this_thread::get_id();trace.identity=lease->image.identity;
 out->body_count=out->hand_count=0;++trace.prepared;return HV_OK;
}
const HV_PipelineApiV1 pipeline{sizeof(pipeline),1,Create,Destroy,Legacy};
HV_Result HV_CALL Query(uint32_t,HV_PluginApiV1* out){*out={sizeof(*out),1,"fixture.prepared","1",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE,8,&pipeline,nullptr,0};return HV_OK;}
HV_Result HV_CALL Extension(uint32_t,HV_PipelineImageExtensionV1* out){*out={sizeof(*out),1,"fixture.prepared",Describe,Prepared};return HV_OK;}
HV_VideoFrame Frame(uint8_t* pixels){return {sizeof(HV_VideoFrame),2,2,8,HV_PIXEL_RGBA32,42,1000,pixels,16};}
bool WaitResult(RuntimeHost& host){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);HV_ObservationFrameV1 result{};
 while(std::chrono::steady_clock::now()<until){if(host.CopyLatest(result))return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;}
struct DeferredService {
 CpuImageService cpu;HV_ImageServiceApiV1 inner{};
 std::atomic<bool> cancelled{false},complete{false};std::atomic<int> actual_releases{0};
 std::mutex mutex;std::condition_variable wake;
 DeferredService(){inner.struct_size=sizeof(inner);inner.api_version=1;cpu.Query(1,&inner);}
 HV_ImageServiceApiV1 Api(){return {sizeof(HV_ImageServiceApiV1),1,this,Support,Submit,Wait,Cancel,Release};}
 static int32_t HV_CALL Support(void* p,const HV_ImageDescriptorV1* s,const HV_ImageTransformPlanV1* plan,HV_ErrorBufferV1* e){auto& d=*static_cast<DeferredService*>(p);return d.inner.QuerySupport(d.inner.context,s,plan,e);}
 static int32_t HV_CALL Submit(void* p,const HV_ImageDescriptorV1* s,const HV_ImageTransformPlanV1* plan,HV_PreparedImageLeaseV1* out,HV_ErrorBufferV1* e){
  auto& d=*static_cast<DeferredService*>(p);const auto result=d.inner.SubmitTransform(d.inner.context,s,plan,out,e);
  if(result==HV_IMAGE_READY){out->completion=out->image.completion=HV_IMAGE_IN_FLIGHT;return HV_IMAGE_PENDING;}return result;
 }
 static int32_t HV_CALL Wait(void* p,HV_PreparedImageLeaseV1* out,uint32_t timeout,HV_ErrorBufferV1* e){
  auto& d=*static_cast<DeferredService*>(p);
  if(!d.cancelled)return HV_IMAGE_TIMEOUT;
  std::unique_lock<std::mutex> lock(d.mutex);d.wake.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return d.complete.load();});
  if(!d.complete)return HV_IMAGE_TIMEOUT;return d.inner.WaitTransform(d.inner.context,out,0,e);
 }
 static int32_t HV_CALL Cancel(void* p,HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*){static_cast<DeferredService*>(p)->cancelled=true;return HV_IMAGE_PENDING;}
 static int32_t HV_CALL Release(void* p,const HV_PreparedImageLeaseV1* out,HV_ErrorBufferV1* e){auto& d=*static_cast<DeferredService*>(p);
  if(!d.complete)return HV_IMAGE_BUSY;const auto result=d.inner.ReleasePrepared(d.inner.context,out,e);if(result==HV_IMAGE_READY)++d.actual_releases;return result;
 }
};
}
TEST(PreparedImageContract, HostUsesOneWorkerAndPreservesTheOriginalEnvelope) {
 Trace trace;PluginRegistry registry;RuntimeHost host;std::string error;
 ASSERT_TRUE(registry.Register(Query,error));ASSERT_TRUE(registry.RegisterImageExtension("fixture.prepared",Extension,error));
 HV_PipelineConfigV1 config{sizeof(config),1,1,0,"{}",".","{}"};HV_HostServicesV1 services{sizeof(services),1,&trace};
 ASSERT_TRUE(host.Start(registry.Find("fixture.prepared",HV_CAP_BODY_POSE,error),config,services,error))<<error;
 uint8_t pixels[16]{};auto frame=Frame(pixels);FrameEnvelope envelope;envelope.source_id=5;envelope.generation=8;envelope.config_epoch=13;envelope.region_revision=21;
 ASSERT_TRUE(host.Submit(frame,envelope,nullptr,0,error));ASSERT_TRUE(WaitResult(host))<<host.LastError();
 host.Stop();EXPECT_EQ(trace.prepared,1);EXPECT_EQ(trace.legacy,0);EXPECT_EQ(trace.identity.source_id,5);EXPECT_EQ(trace.identity.generation,8);
 EXPECT_EQ(trace.identity.config_epoch,13);EXPECT_EQ(trace.identity.region_revision,21);
 EXPECT_EQ(trace.describe_thread,trace.process_thread);EXPECT_NE(trace.process_thread,std::this_thread::get_id());
}
TEST(PreparedImageContract, APluginWithoutExtensionKeepsItsLegacyProcess) {
 Trace trace;PluginRegistry registry;RuntimeHost host;std::string error;
 ASSERT_TRUE(registry.Register(Query,error));HV_PipelineConfigV1 config{sizeof(config),1,1,0,"{}",".","{}"};HV_HostServicesV1 services{sizeof(services),1,&trace};
 HV_ImageServiceApiV1 deliberately_unavailable{};
 ASSERT_TRUE(host.Start(registry.Find("fixture.prepared",HV_CAP_BODY_POSE,error),config,services,error,&deliberately_unavailable));
 uint8_t pixels[16]{};ASSERT_TRUE(host.Submit(Frame(pixels),error));ASSERT_TRUE(WaitResult(host));host.Stop();
 EXPECT_EQ(trace.legacy,1);EXPECT_EQ(trace.prepared,0);
}
TEST(PreparedImageContract, HostCancelCannotRetireAnUncompletedImageOperation) {
 Trace trace;DeferredService deferred;auto image_api=deferred.Api();PluginRegistry registry;RuntimeHost host;std::string error;
 ASSERT_TRUE(registry.Register(Query,error));ASSERT_TRUE(registry.RegisterImageExtension("fixture.prepared",Extension,error));
 HV_PipelineConfigV1 config{sizeof(config),1,1,0,"{}",".","{}"};HV_HostServicesV1 services{sizeof(services),1,&trace};
 ASSERT_TRUE(host.Start(registry.Find("fixture.prepared",HV_CAP_BODY_POSE,error),config,services,error,&image_api));
 uint8_t pixels[16]{};ASSERT_TRUE(host.Submit(Frame(pixels),error));
 const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(!deferred.cancelled&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 EXPECT_TRUE(deferred.cancelled);std::atomic<bool> stopped{false};std::thread stop([&]{host.Stop();stopped=true;});
 std::this_thread::sleep_for(std::chrono::milliseconds(10));EXPECT_FALSE(stopped);EXPECT_EQ(deferred.actual_releases,0);EXPECT_EQ(trace.prepared,0);
 deferred.complete=true;deferred.wake.notify_all();stop.join();EXPECT_TRUE(stopped);EXPECT_EQ(deferred.actual_releases,1);EXPECT_EQ(host.ErrorCount(),1);
}
