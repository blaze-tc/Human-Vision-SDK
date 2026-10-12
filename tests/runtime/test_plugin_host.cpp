#include "host/plugin_registry.h"
#include "host/runtime_host.h"
#include "common/frame_rate_credit.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <condition_variable>
#include <mutex>

using namespace humanvision::runtime;
TEST(FrameRateCredit, KeepsThirtyFreshFramesDespiteAlternatingCaptureJitter) {
    FrameRateCredit gate;int accepted=0;int64_t time=1000000;
    for(int i=0;i<300;++i){time+=i%2?33334:33332;if(gate.Admit(time,30)){++accepted;}}
    EXPECT_GE(accepted,298);
}
TEST(FrameRateCredit, BoundsBurstAndRejectsRepeatedOrBackwardSourceTime) {
    FrameRateCredit gate;
    EXPECT_TRUE(gate.Admit(1000000,30));
    EXPECT_FALSE(gate.Admit(1000000,30));EXPECT_FALSE(gate.Admit(999999,30));
    EXPECT_TRUE(gate.Admit(2000000,30));EXPECT_TRUE(gate.Admit(2000001,30));
    EXPECT_FALSE(gate.Admit(2000002,30));
}
namespace {
std::atomic<int> destroyed{0};
bool throw_after_create = false;
HV_Result HV_CALL Create(const HV_PipelineConfigV1*, const HV_HostServicesV1*, void** out, HV_ErrorBufferV1*) {
    *out = new int(1);
    if (throw_after_create) throw std::runtime_error("fixture exception");
    return HV_OK;
}
void HV_CALL Destroy(void* p) { delete static_cast<int*>(p); ++destroyed; }
HV_Result HV_CALL Process(void*, const HV_PipelineInputV1* in, HV_PipelineOutputV1* out, HV_ErrorBufferV1*) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    out->body_count = 1;
    out->bodies[0] = {};
    out->bodies[0].struct_size = sizeof(HV_BodyObservationV1);
    out->bodies[0].api_version = HV_PLUGIN_API_V1;
    out->bodies[0].confidence = .9F;
    auto& joint = out->bodies[0].joints[HV_CANONICAL_HEAD];
    joint.struct_size = sizeof(joint); joint.api_version = HV_API_VERSION_040;
    joint.valid = 1; joint.x_norm = .5F; joint.observation_timestamp_us = in->frame.timestamp_us;
    return HV_OK;
}
const HV_PipelineApiV1 pipeline{sizeof(HV_PipelineApiV1), HV_PLUGIN_API_V1, Create, Destroy, Process};
HV_Result HV_CALL Query(uint32_t version, HV_PluginApiV1* out) {
    if (version != HV_PLUGIN_API_V1 || !out || out->struct_size < sizeof(*out)) return HV_ERR_INVALID_ARGUMENT;
    *out = {sizeof(*out), HV_PLUGIN_API_V1, "fixture.pose", "1.0.0", HV_PLUGIN_PIPELINE,
        HV_CAP_BODY_POSE | HV_CAP_MULTI_PERSON, 8, &pipeline, nullptr};
    return HV_OK;
}
HV_Result HV_CALL BadVersion(uint32_t v, HV_PluginApiV1* out) { Query(v,out); out->api_version=99; return HV_OK; }
HV_Result HV_CALL BadCallback(uint32_t v, HV_PluginApiV1* out) { Query(v,out); out->pipeline=nullptr; return HV_OK; }
}
TEST(PluginRegistry, RejectsInvalidAndDuplicateWithoutReplacingGoodEntry) {
    PluginRegistry registry; std::string error;
    EXPECT_FALSE(registry.Register(nullptr,error));
    EXPECT_FALSE(registry.Register(BadVersion,error));
    EXPECT_FALSE(registry.Register(BadCallback,error));
    ASSERT_TRUE(registry.Register(Query,error)) << error;
    EXPECT_FALSE(registry.Register(Query,error));
    EXPECT_TRUE(registry.Find("fixture.pose",HV_CAP_BODY_POSE,error));
    EXPECT_FALSE(registry.Find("fixture.pose",HV_CAP_HAND_POSE,error));
    EXPECT_FALSE(registry.Find("missing",0,error));
}
#if defined(HV_TEST_PLUGIN_PATH)
TEST(PluginRegistry, DynamicLibraryLifetimeFollowsBorrowedModule) {
    std::string error;
    std::shared_ptr<const PluginModule> retained;
    {
        PluginRegistry registry;
        ASSERT_TRUE(registry.Load(HV_TEST_PLUGIN_PATH,error)) << error;
        retained=registry.Find("fixture.dynamic",HV_CAP_BODY_POSE,error);
        ASSERT_TRUE(retained);
        EXPECT_FALSE(registry.Load(HV_TEST_PLUGIN_PATH,error));
    }
    HV_PluginApiV1 copy=retained->api;
    EXPECT_STREQ(copy.plugin_id,"fixture.dynamic");
    EXPECT_NE(copy.pipeline->process,nullptr);
}
#endif
TEST(RuntimeHost, ReplacesPendingInputAndPreservesObservationTime) {
    PluginRegistry registry; std::string error; ASSERT_TRUE(registry.Register(Query,error));
    RuntimeHost host;
    HV_PipelineConfigV1 config{}; config.struct_size=sizeof(config); config.api_version=HV_PLUGIN_API_V1; config.max_bodies=8;
    HV_HostServicesV1 services{}; services.struct_size=sizeof(services); services.api_version=HV_PLUGIN_API_V1;
    ASSERT_TRUE(host.Start(registry.Find("fixture.pose",HV_CAP_BODY_POSE,error),config,services,error)) << error;
    uint8_t pixels[16]{};
    HV_VideoFrame frame{};frame.struct_size=sizeof(frame);frame.width=2;frame.height=2;frame.stride_bytes=8;
    frame.pixel_format=HV_PIXEL_RGBA32;frame.data=pixels;frame.data_bytes=16;
    for (int i=1;i<=20;++i) { frame.frame_id=i;frame.timestamp_us=i*1000; ASSERT_TRUE(host.Submit(frame,error)); }
    HV_ObservationFrameV1 result{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (std::chrono::steady_clock::now()<deadline) {
        if (host.CopyLatest(result) && result.source_frame_id==20) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(result.source_frame_id,20);
    EXPECT_EQ(result.source_timestamp_us,20000);
    EXPECT_EQ(result.bodies[0].joints[HV_CANONICAL_HEAD].observation_timestamp_us,20000);
    EXPECT_GT(host.DroppedFrames(),0);
    EXPECT_LT(result.sequence,20);
    host.Stop();
    EXPECT_FALSE(host.Submit(frame,error));
    EXPECT_FALSE(host.CopyLatest(result));
    EXPECT_GT(destroyed.load(),0);
}
TEST(RuntimeHost, ReleasesPartialInstanceWhenCreateViolatesExceptionContract) {
    PluginRegistry registry;std::string error;ASSERT_TRUE(registry.Register(Query,error));
    RuntimeHost host;
    HV_PipelineConfigV1 config{};config.struct_size=sizeof(config);config.api_version=HV_PLUGIN_API_V1;config.max_bodies=1;
    HV_HostServicesV1 services{};services.struct_size=sizeof(services);services.api_version=HV_PLUGIN_API_V1;
    const int before=destroyed.load();throw_after_create=true;
    EXPECT_FALSE(host.Start(registry.Find("fixture.pose",0,error),config,services,error));
    throw_after_create=false;
    EXPECT_EQ(destroyed.load(),before+1);
    EXPECT_NE(error.find("exception"),std::string::npos);
}

namespace {
std::mutex pool_mutex;std::condition_variable pool_cv;
bool pool_release=false;int pool_entered=0,pool_active=0,pool_max=0;
HV_Result HV_CALL PoolProcess(void*,const HV_PipelineInputV1* in,HV_PipelineOutputV1* out,HV_ErrorBufferV1*) {
    std::unique_lock<std::mutex> lock(pool_mutex);
    ++pool_entered;++pool_active;pool_max=std::max(pool_max,pool_active);pool_cv.notify_all();
    if(in->frame.frame_id==1)pool_cv.wait(lock,[]{return pool_release;});
    --pool_active;out->body_count=0;out->hand_count=0;return HV_OK;
}
HV_Result HV_CALL PoolCreate(const HV_PipelineConfigV1* config,const HV_HostServicesV1* services,void** out,HV_ErrorBufferV1* error) {
    auto status=Create(config,services,out,error);
    return config->options_utf8&&std::string(config->options_utf8)=="fail"?HV_ERR_INTERNAL:status;
}
const HV_PipelineApiV1 pool_pipeline{sizeof(HV_PipelineApiV1),HV_PLUGIN_API_V1,PoolCreate,Destroy,PoolProcess};
HV_Result HV_CALL PoolQuery(uint32_t v,HV_PluginApiV1* out){auto result=Query(v,out);out->pipeline=&pool_pipeline;return result;}
}
TEST(RuntimeHost, IdleInstanceProcessesNewFrameAndLateCompletionCannotMoveResultBackward) {
    {std::lock_guard<std::mutex> lock(pool_mutex);pool_release=false;pool_entered=pool_active=pool_max=0;}
    PluginRegistry registry;std::string error;ASSERT_TRUE(registry.Register(PoolQuery,error));RuntimeHost host;
    HV_PipelineConfigV1 config{};config.struct_size=sizeof(config);config.api_version=1;config.max_bodies=1;
    HV_HostServicesV1 services{};services.struct_size=sizeof(services);services.api_version=1;
    ASSERT_TRUE(host.StartPool(registry.Find("fixture.pose",0,error),{config,config},services,error))<<error;
    EXPECT_EQ(host.WorkerCount(),2u);EXPECT_EQ(host.ActiveWorkers(),0u);
    // Unblock the fixture before host destruction even after an assertion fails.
    struct Release {~Release(){std::lock_guard<std::mutex> lock(pool_mutex);pool_release=true;pool_cv.notify_all();}} release;
    uint8_t pixels[16]{};HV_VideoFrame frame{sizeof(frame),2,2,8,HV_PIXEL_RGBA32,1,1000,pixels,16};
    ASSERT_TRUE(host.Submit(frame,nullptr,0,101,error));
    {std::unique_lock<std::mutex> lock(pool_mutex);ASSERT_TRUE(pool_cv.wait_for(lock,std::chrono::seconds(2),[]{return pool_entered>=1;}));}
    EXPECT_EQ(host.ActiveWorkers(),1u);
    frame.frame_id=2;frame.timestamp_us=2000;ASSERT_TRUE(host.Submit(frame,nullptr,0,102,error));
    HV_ObservationFrameV1 result{};const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(std::chrono::steady_clock::now()<deadline&&(!host.CopyLatest(result)||result.source_frame_id!=2))std::this_thread::yield();
    ASSERT_EQ(result.source_frame_id,2);EXPECT_EQ(result.source_timestamp_us,2000);
    int64_t revision=0;ASSERT_TRUE(host.CopyLatest(result,revision));EXPECT_EQ(revision,102);
    {std::lock_guard<std::mutex> lock(pool_mutex);EXPECT_EQ(pool_max,2);pool_release=true;pool_cv.notify_all();}
    while(host.ActiveWorkers()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    ASSERT_TRUE(host.CopyLatest(result,revision));EXPECT_EQ(result.source_frame_id,2);EXPECT_EQ(result.sequence,1);EXPECT_EQ(revision,102);
    EXPECT_GE(host.DroppedFrames(),1);
    EXPECT_FALSE(host.Submit(frame,error)); // duplicate must never be inferred twice
    host.Stop();EXPECT_EQ(host.ActiveWorkers(),0u);
}
TEST(RuntimeHost, PoolRejectsUnboundedCapacityBeforeCreatingInstances) {
    PluginRegistry registry;std::string error;ASSERT_TRUE(registry.Register(Query,error));RuntimeHost host;
    HV_PipelineConfigV1 config{};config.struct_size=sizeof(config);config.api_version=1;config.max_bodies=1;
    HV_HostServicesV1 services{};services.struct_size=sizeof(services);services.api_version=1;
    EXPECT_FALSE(host.StartPool(registry.Find("fixture.pose",0,error),{config,config,config,config},services,error));
}
TEST(RuntimeHost, FailedAdditionalInstanceReleasesEntireCandidatePoolAndPreservesRunningHost) {
    PluginRegistry registry;std::string error;ASSERT_TRUE(registry.Register(PoolQuery,error));RuntimeHost host;
    HV_PipelineConfigV1 config{};config.struct_size=sizeof(config);config.api_version=1;config.max_bodies=1;
    HV_HostServicesV1 services{};services.struct_size=sizeof(services);services.api_version=1;
    auto module=registry.Find("fixture.pose",0,error);
    ASSERT_TRUE(host.Start(module,config,services,error));
    const int before=destroyed.load();auto bad=config;bad.options_utf8="fail";
    EXPECT_FALSE(host.StartPool(module,{config,bad},services,error));
    EXPECT_EQ(destroyed.load(),before+2);EXPECT_EQ(host.WorkerCount(),1u);
    uint8_t pixels[16]{};HV_VideoFrame frame{sizeof(frame),2,2,8,HV_PIXEL_RGBA32,2,2000,pixels,16};
    ASSERT_TRUE(host.Submit(frame,error));HV_ObservationFrameV1 result{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(std::chrono::steady_clock::now()<deadline&&!host.CopyLatest(result))std::this_thread::yield();
    EXPECT_EQ(result.source_frame_id,2);host.Stop();EXPECT_EQ(destroyed.load(),before+3);
}
