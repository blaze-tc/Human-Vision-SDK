#include "host/plugin_registry.h"
#include "host/runtime_host.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>

using namespace humanvision::runtime;
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
