#include "plugins/legacy/legacy_pipeline.h"
#include "host/runtime_host.h"
#include "host/backend_factory.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

TEST(LegacyPlugin, RealModelsPublishSemanticObservationsThroughHost) {
    using namespace humanvision::runtime;
    PluginRegistry registry; std::string error;
    ASSERT_TRUE(registry.Register(HV_QueryLegacyPipeline,error))<<error;
    const std::string manifest=R"({"models":[{"role":"detector","asset_path":"models/detector/rtmdet_tiny_640.onnx"},{"role":"body","asset_path":"models/pose/rtmpose_s_256x192.onnx"}]})";
    HV_PipelineConfigV1 config{};config.struct_size=sizeof(config);config.api_version=HV_PLUGIN_API_V1;
    config.max_bodies=2; config.asset_root_utf8=HV_TEST_PROJECT_ROOT;config.model_manifest_utf8=manifest.c_str();
    ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error));
    BackendFactory factory({registry.Find("backend.ort.cpu",HV_CAP_TENSOR_INFERENCE,error)});
    auto services=factory.Services();
    RuntimeHost host;
    ASSERT_TRUE(host.Start(registry.Find("pipeline.legacy",HV_CAP_BODY_POSE,error),config,services,error))<<error;
    auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    auto frame=humanvision::test::MakeBgrFrame(pixels,humanvision::test::fixture::kRawWidth,humanvision::test::fixture::kRawHeight,42,123456);
    ASSERT_TRUE(host.Submit(frame,error))<<error;
    HV_ObservationFrameV1 output{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!host.CopyLatest(output)&&std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_EQ(output.source_frame_id,42)<<host.LastError();
    ASSERT_GT(output.body_count,0u); ASSERT_LE(output.body_count,2u);
    const auto& nose=output.bodies[0].joints[HV_CANONICAL_NOSE];
    EXPECT_TRUE(nose.valid);EXPECT_EQ(nose.observation_timestamp_us,123456);
    EXPECT_NEAR(nose.x_px,humanvision::test::fixture::kPoseJoints[0].x_px,1.5F);
    EXPECT_NEAR(nose.y_px,humanvision::test::fixture::kPoseJoints[0].y_px,1.5F);
    EXPECT_FALSE(output.bodies[0].joints[HV_CANONICAL_HANDTIP_LEFT].valid);
}
