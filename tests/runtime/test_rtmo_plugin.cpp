#include "plugins/pipeline/rtmo/rtmo_pipeline.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "host/backend_factory.h"
#include "host/runtime_host.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <thread>
TEST(RtmoPlugin, RealModelMapsCenteredLetterboxToSource) {
 using namespace humanvision::runtime;PluginRegistry registry;std::string error;
 ASSERT_TRUE(registry.Register(HV_QueryRtmoPipeline,error));ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error));
 BackendFactory factory({registry.Find("backend.ort.cpu",0,error)});auto services=factory.Services();
 const std::string manifest=R"({"models":[{"role":"body","asset_path":"modelpacks/rtmo-t-416/body.onnx","input_contract":{"width":416,"height":416}}]})";
 HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,8,0,manifest.c_str(),HV_TEST_PROJECT_ROOT,nullptr};
 RuntimeHost host;ASSERT_TRUE(host.Start(registry.Find("pipeline.rtmo",0,error),config,services,error))<<error;
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,123,456000);
 ASSERT_TRUE(host.Submit(frame,error));HV_ObservationFrameV1 result{};
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
 while(!host.CopyLatest(result)&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));
 ASSERT_EQ(result.source_frame_id,123)<<host.LastError();ASSERT_EQ(result.body_count,1u);
 auto& nose=result.bodies[0].joints[HV_CANONICAL_NOSE];
 EXPECT_TRUE(nose.valid);EXPECT_NEAR(nose.x_px,(181.35884F-208)*346/416+109,1.5F);
 EXPECT_NEAR(nose.y_px,43.2293F*346/416,1.5F);EXPECT_EQ(nose.observation_timestamp_us,456000);
}
