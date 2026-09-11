#include "plugins/pipeline/simcc/simcc_pipeline.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "host/backend_factory.h"
#include "host/model_pack_manager.h"
#include "test_support.h"
#include <gtest/gtest.h>
TEST(SimccPlugin, RealBodyAndHandModelsUseIndependentSessions) {
 using namespace humanvision::runtime;PluginRegistry registry;std::string error;
 ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error));BackendFactory factory({registry.Find("backend.ort.cpu",0,error)});auto services=factory.Services();
 ModelPackManager packs(std::filesystem::path(HV_TEST_PROJECT_ROOT)/"modelpacks");
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,12,12345);
 for(bool hand:{false,true}){
  auto pack=packs.Resolve(hand?"hand21":"precision-t-26",error);ASSERT_TRUE(pack)<<error;auto root=pack->root.u8string();
  HV_PluginApiV1 plugin{};plugin.struct_size=sizeof(plugin);ASSERT_EQ((hand?HV_QueryHandPipeline:HV_QueryTopDownPipeline)(HV_PLUGIN_API_V1,&plugin),HV_OK);
  HV_PipelineConfigV1 config{sizeof(config),HV_PLUGIN_API_V1,2,0,pack->manifest_json.c_str(),root.c_str(),nullptr};void* instance=nullptr;
  char message[512]{};HV_ErrorBufferV1 e{sizeof(e),HV_PLUGIN_API_V1,message,sizeof(message)};
  ASSERT_EQ(plugin.pipeline->create(&config,&services,&instance,&e),HV_OK)<<message;
  struct Guard{const HV_PipelineApiV1* api;void* p;~Guard(){api->destroy(p);}} guard{plugin.pipeline,instance};
  HV_RegionOfInterestV1 roi{sizeof(roi),HV_PLUGIN_API_V1,99,{0,105,70,70},0,0};
  HV_PipelineInputV1 input{sizeof(input),HV_PLUGIN_API_V1,frame,hand?&roi:nullptr,hand?1u:0u,0};
  HV_BodyObservationV1 bodies[2]{};HV_HandObservationV1 hands[1]{};
  HV_PipelineOutputV1 output{sizeof(output),HV_PLUGIN_API_V1,bodies,2,0,hands,1,0,0,0,0};
  ASSERT_EQ(plugin.pipeline->process(instance,&input,&output,&e),HV_OK)<<message;
  if(hand){ASSERT_EQ(output.hand_count,1u);EXPECT_EQ(hands[0].request_id,99);EXPECT_EQ(hands[0].thumb.observation_timestamp_us,12345);EXPECT_TRUE(std::isfinite(hands[0].fingertip.x_px));}
  else{ASSERT_GE(output.body_count,1u);EXPECT_TRUE(bodies[0].joints[HV_CANONICAL_NOSE].valid);EXPECT_EQ(bodies[0].joints[HV_CANONICAL_HEAD].observation_timestamp_us,12345);}
 }
}
