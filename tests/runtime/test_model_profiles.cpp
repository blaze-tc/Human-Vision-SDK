#include "host/model_pack_manager.h"
#include "host/profile_manager.h"
#include <gtest/gtest.h>
#include <fstream>
#include <atomic>
#include <chrono>
using namespace humanvision::runtime;
namespace {
// Metadata fixtures are never instantiated or included in production builds.
HV_Result HV_CALL CreatePose(const HV_PipelineConfigV1*, const HV_HostServicesV1*, void**, HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
void HV_CALL DestroyFixture(void*) {}
HV_Result HV_CALL ProcessPose(void*, const HV_PipelineInputV1*, HV_PipelineOutputV1*, HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
const HV_PipelineApiV1 pose{sizeof(pose), HV_PLUGIN_API_V1, CreatePose, DestroyFixture, ProcessPose};
HV_Result HV_CALL QueryPose(uint32_t, HV_PluginApiV1* out) {
 *out={sizeof(*out),HV_PLUGIN_API_V1,"fixture.pose","1.0.0",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE | HV_CAP_TENSOR_INFERENCE,8,&pose,nullptr,100};return HV_OK;
}
HV_Result HV_CALL CreateBackend(const HV_BackendConfigV1*, void**, HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
HV_Result HV_CALL RunBackend(void*,const HV_TensorViewV1*,uint32_t,HV_TensorViewV1*,uint32_t,uint32_t*,HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
HV_Result HV_CALL BackendInfo(void*,HV_BackendSessionInfoV1*) { return HV_ERR_INVALID_ARGUMENT; }
const HV_BackendApiV1 backend{sizeof(backend),HV_PLUGIN_API_V1,CreateBackend,DestroyFixture,RunBackend,BackendInfo};
HV_Result HV_CALL QueryBackend(uint32_t,HV_PluginApiV1* out) {
 *out={sizeof(*out),HV_PLUGIN_API_V1,"fixture.cpu","1.0.0",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE,0,nullptr,&backend,10};return HV_OK;
}
class PackTest : public testing::Test {
protected:
 std::filesystem::path root;
 void SetUp() override {
  static std::atomic<int> index{0};
  root=std::filesystem::temp_directory_path()/("hv040-pack-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++index));
  std::filesystem::create_directories(root/"packs"/"fixture");std::filesystem::create_directories(root/"profiles");
  std::ofstream(root/"packs"/"fixture"/"weights.dat",std::ios::binary)<<"abc";
 }
 void TearDown() override { std::filesystem::remove_all(root); }
 std::string Manifest(const std::string& asset="weights.dat") {
  return "{\"schema_version\":1,\"pack_id\":\"fixture\",\"pack_version\":\"1.0.0\",\"pipeline_id\":\"fixture.pose\",\"capabilities\":[\"body_pose\"],\"max_people\":8,\"models\":[{\"role\":\"body\",\"format\":\"test\",\"decoder_id\":\"semantic\",\"asset_path\":\""+asset+"\",\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"input_contract\":{},\"output_contract\":{},\"source\":\"fixture\",\"license\":\"test-only\"}]}";
 }
 void Save(const std::string& value) { std::ofstream(root/"packs"/"fixture"/"manifest.json")<<value; }
};
TEST_F(PackTest, ValidatesHashAndRejectsTraversalOrChangedAssets) {
 Save(Manifest());ModelPackManager packs(root/"packs");std::string error;
 auto pack=packs.Resolve("fixture",error);ASSERT_TRUE(pack)<<error;
 EXPECT_EQ(pack->models.size(),1);EXPECT_EQ(pack->pipeline_id,"fixture.pose");
 EXPECT_FALSE(packs.Resolve("../fixture",error));
 Save(Manifest("../weights.dat"));EXPECT_FALSE(packs.Resolve("fixture",error));
 Save(Manifest());std::ofstream(root/"packs"/"fixture"/"weights.dat")<<"changed";
 EXPECT_FALSE(packs.Resolve("fixture",error));EXPECT_NE(error.find("SHA-256"),std::string::npos);
}
TEST_F(PackTest, RejectsUnknownSchemaMissingHashAndDuplicateRole) {
 ModelPackManager packs(root/"packs");std::string error;auto text=Manifest();
 text.replace(text.find("schema_version\":1"),17,"schema_version\":9");Save(text);EXPECT_FALSE(packs.Resolve("fixture",error));
 text=Manifest();auto start=text.find("ba7816");text.replace(start,64,"bad");Save(text);EXPECT_FALSE(packs.Resolve("fixture",error));
 text=Manifest();auto object=text.substr(text.find("[{" )+1);object=object.substr(0,object.size()-2);
 text.insert(text.size()-2,","+object);Save(text);EXPECT_FALSE(packs.Resolve("fixture",error));
}
TEST_F(PackTest, MissingProfileComponentsFailWithActionableReason) {
 Save(Manifest());std::ofstream(root/"profiles"/"desktop.json")<<R"({"schema_version":1,"profile":"desktop","body":{"pipeline":"missing.pose","modelPack":"fixture"},"backend":{"preference":"missing.backend"}})";
 ModelPackManager packs(root/"packs");PluginRegistry registry;ProfileManager profiles(root/"profiles");std::string error;
 EXPECT_FALSE(profiles.Resolve("desktop",4,registry,packs,error));EXPECT_NE(error.find("missing.pose"),std::string::npos);
 EXPECT_FALSE(profiles.Resolve("desktop",9,registry,packs,error));
}
TEST_F(PackTest, AutoFallbackSelectsOnlyBackendsAndKeepsDiagnostic) {
 Save(Manifest());
 std::ofstream(root/"profiles"/"desktop.json")<<R"({"schema_version":1,"profile":"desktop","body_by_capacity":[{"max_people":2,"pipeline":"fixture.pose","modelPack":"fixture"},{"max_people":8,"pipeline":"fixture.pose","modelPack":"fixture"}],"backend":{"preference":["missing.gpu","auto","fixture.cpu"]}})";
 ModelPackManager packs(root/"packs"); PluginRegistry registry; ProfileManager profiles(root/"profiles"); std::string error;
 ASSERT_TRUE(registry.Register(QueryPose,error)); ASSERT_TRUE(registry.Register(QueryBackend,error));
 auto profile=profiles.Resolve("desktop",8,registry,packs,error); ASSERT_TRUE(profile)<<error;
 ASSERT_EQ(profile->backends.size(),1u); EXPECT_STREQ(profile->backends[0]->api.plugin_id,"fixture.cpu");
 EXPECT_NE(profile->fallback_reason.find("missing.gpu"),std::string::npos);
 EXPECT_EQ(profile->max_people,8); EXPECT_EQ(profile->output_hz,60); EXPECT_FALSE(profile->hands_enabled);
}
}
