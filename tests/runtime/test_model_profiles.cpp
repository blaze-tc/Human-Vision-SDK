#include "host/model_pack_manager.h"
#include "host/profile_manager.h"
#include <gtest/gtest.h>
#include <fstream>
#include <atomic>
#include <chrono>
#include "json/json.hpp"
using namespace humanvision::runtime;
namespace {
constexpr uint64_t kVulkan = 1ull << 7;
constexpr uint64_t kFp16Storage = 1ull << 8;
constexpr uint64_t kFp16Arithmetic = 1ull << 9;
constexpr uint64_t kAndroidHardwareBuffer = 1ull << 10;
constexpr uint64_t kExternalSyncFd = 1ull << 11;

// Metadata fixtures are never instantiated or included in production builds.
HV_Result HV_CALL CreatePose(const HV_PipelineConfigV1*, const HV_HostServicesV1*, void**, HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
void HV_CALL DestroyFixture(void*) {}
HV_Result HV_CALL ProcessPose(void*, const HV_PipelineInputV1*, HV_PipelineOutputV1*, HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
const HV_PipelineApiV1 pose{sizeof(pose), HV_PLUGIN_API_V1, CreatePose, DestroyFixture, ProcessPose};
HV_Result HV_CALL QueryPose(uint32_t, HV_PluginApiV1* out) {
 *out={sizeof(*out),HV_PLUGIN_API_V1,"fixture.pose","1.0.0",HV_PLUGIN_PIPELINE,HV_CAP_BODY_POSE | HV_CAP_MULTI_PERSON | HV_CAP_GPU_INPUT,8,&pose,nullptr,100};return HV_OK;
}
HV_Result HV_CALL CreateBackend(const HV_BackendConfigV1*, void**, HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
HV_Result HV_CALL RunBackend(void*,const HV_TensorViewV1*,uint32_t,HV_TensorViewV1*,uint32_t,uint32_t*,HV_ErrorBufferV1*) { return HV_ERR_INVALID_ARGUMENT; }
HV_Result HV_CALL BackendInfo(void*,HV_BackendSessionInfoV1*) { return HV_ERR_INVALID_ARGUMENT; }
const HV_BackendApiV1 backend{sizeof(backend),HV_PLUGIN_API_V1,CreateBackend,DestroyFixture,RunBackend,BackendInfo};
HV_Result HV_CALL QueryBackend(uint32_t,HV_PluginApiV1* out) {
 *out={sizeof(*out),HV_PLUGIN_API_V1,"fixture.cpu","1.0.0",HV_PLUGIN_BACKEND,HV_CAP_TENSOR_INFERENCE | HV_CAP_GPU_INPUT | kVulkan | kFp16Storage | kFp16Arithmetic | kAndroidHardwareBuffer,0,nullptr,&backend,10};return HV_OK;
}
class ProfileManagerTest : public testing::Test {
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
 std::string Schema2Manifest(const std::string& capabilities = R"(["body_pose","multi_person","gpu_input","vulkan","fp16-storage","fp16-arithmetic","android-hardware-buffer","external-sync-fd"])") {
  return R"({"schema_version":2,"pack_id":"fixture","pack_version":"2.0.0","pipeline_id":"fixture.pose","capabilities":)" + capabilities +
   R"(,"max_people":2,"models":[{"role":"detector","format":"ncnn","decoder_id":"rtmdet_nano_raw_v1","param_path":"detector.param","param_sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","bin_path":"detector.bin","bin_sha256":"cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34","input_contract":{"image_format":"rgba8-unorm","color_order":"rgb","normalization":{"mean":[0.0,0.0,0.0],"norm":[0.0039215686,0.0039215686,0.0039215686]},"tensor_dtype":"fp16","elempack":4,"width":320,"height":320,"input_blob":"in0"},"output_contract":{"decoder":"rtmdet_nano_raw_v1","output_blobs":["cls","bbox"]},"source":"fixture source","license":"test-only","conversion_recipe":"fixture conversion"}]})";
 }
 void Save(const std::string& value) { std::ofstream(root/"packs"/"fixture"/"manifest.json")<<value; }
};
TEST_F(ProfileManagerTest, Schema2ParsesNamedNcnnFilesAndCompleteTensorContract) {
 std::ofstream(root/"packs"/"fixture"/"detector.param",std::ios::binary)<<"abc";
 std::ofstream(root/"packs"/"fixture"/"detector.bin",std::ios::binary)<<"def";
 Save(Schema2Manifest());ModelPackManager packs(root/"packs");std::string error;
 auto pack=packs.Resolve("fixture",error);ASSERT_TRUE(pack)<<error;
 ASSERT_EQ(pack->models.size(),1u);
 ASSERT_EQ(pack->models[0].files.size(),2u);
 EXPECT_EQ(pack->models[0].files[0].name,"param");EXPECT_EQ(pack->models[0].files[0].path.filename(),"detector.param");
 EXPECT_EQ(pack->models[0].files[1].name,"bin");EXPECT_EQ(pack->models[0].files[1].path.filename(),"detector.bin");
 auto input=nlohmann::json::parse(pack->models[0].input_contract);
 auto output=nlohmann::json::parse(pack->models[0].output_contract);
 EXPECT_EQ(input.at("image_format"),"rgba8-unorm");EXPECT_EQ(input.at("color_order"),"rgb");
 EXPECT_EQ(input.at("normalization").at("mean").size(),3u);EXPECT_EQ(input.at("normalization").at("norm").size(),3u);
 EXPECT_EQ(input.at("tensor_dtype"),"fp16");EXPECT_EQ(input.at("elempack"),4);
 EXPECT_EQ(input.at("width"),320);EXPECT_EQ(input.at("height"),320);EXPECT_EQ(input.at("input_blob"),"in0");
 ASSERT_EQ(output.at("output_blobs").size(),2u);EXPECT_EQ(output.at("output_blobs")[0],"cls");EXPECT_EQ(output.at("output_blobs")[1],"bbox");
 EXPECT_EQ(pack->capabilities & (kVulkan | kFp16Storage | kFp16Arithmetic | kAndroidHardwareBuffer | kExternalSyncFd),
           kVulkan | kFp16Storage | kFp16Arithmetic | kAndroidHardwareBuffer | kExternalSyncFd);
}
TEST_F(ProfileManagerTest, Schema2AcceptsRevisionTwoModelpackFilename) {
 std::ofstream(root/"packs"/"fixture"/"detector.param",std::ios::binary)<<"abc";
 std::ofstream(root/"packs"/"fixture"/"detector.bin",std::ios::binary)<<"def";
 std::ofstream(root/"packs"/"fixture"/"modelpack.json")<<Schema2Manifest();
 ModelPackManager packs(root/"packs");std::string error;
 auto pack=packs.Resolve("fixture",error);ASSERT_TRUE(pack)<<error;EXPECT_EQ(pack->version,"2.0.0");
}
TEST_F(ProfileManagerTest, Schema2NamesMissingTensorContractField) {
 std::ofstream(root/"packs"/"fixture"/"detector.param",std::ios::binary)<<"abc";
 std::ofstream(root/"packs"/"fixture"/"detector.bin",std::ios::binary)<<"def";
 auto manifest=nlohmann::json::parse(Schema2Manifest());manifest["models"][0]["input_contract"].erase("tensor_dtype");
 Save(manifest.dump());ModelPackManager packs(root/"packs");std::string error;
 EXPECT_FALSE(packs.Resolve("fixture",error));EXPECT_NE(error.find("tensor_dtype"),std::string::npos)<<error;
}
TEST_F(ProfileManagerTest, Schema2NamesMissingRequiredFp16Capability) {
 std::ofstream(root/"packs"/"fixture"/"detector.param",std::ios::binary)<<"abc";
 std::ofstream(root/"packs"/"fixture"/"detector.bin",std::ios::binary)<<"def";
 Save(Schema2Manifest(R"(["body_pose","multi_person","gpu_input","vulkan","fp16-storage"])") );
 ModelPackManager packs(root/"packs");std::string error;
 EXPECT_FALSE(packs.Resolve("fixture",error));EXPECT_NE(error.find("fp16-arithmetic"),std::string::npos)<<error;
}
TEST(ProfileManagerCapabilities, AddNewBitsWithoutChangingExistingValues) {
 EXPECT_EQ(CapabilityBit("body_pose"),1ull<<0);EXPECT_EQ(CapabilityBit("hand_pose"),1ull<<1);
 EXPECT_EQ(CapabilityBit("multi_person"),1ull<<2);EXPECT_EQ(CapabilityBit("tensor_inference"),1ull<<3);
 EXPECT_EQ(CapabilityBit("dynamic_input"),1ull<<4);EXPECT_EQ(CapabilityBit("batch"),1ull<<5);EXPECT_EQ(CapabilityBit("gpu_input"),1ull<<6);
 EXPECT_EQ(CapabilityBit("vulkan"),kVulkan);EXPECT_EQ(CapabilityBit("fp16-storage"),kFp16Storage);
 EXPECT_EQ(CapabilityBit("fp16-arithmetic"),kFp16Arithmetic);EXPECT_EQ(CapabilityBit("android-hardware-buffer"),kAndroidHardwareBuffer);
 EXPECT_EQ(CapabilityBit("external-sync-fd"),kExternalSyncFd);
}
TEST_F(ProfileManagerTest, ValidatesHashAndRejectsTraversalOrChangedAssets) {
 Save(Manifest());ModelPackManager packs(root/"packs");std::string error;
 auto pack=packs.Resolve("fixture",error);ASSERT_TRUE(pack)<<error;
 EXPECT_EQ(pack->models.size(),1);EXPECT_EQ(pack->pipeline_id,"fixture.pose");
 EXPECT_FALSE(packs.Resolve("../fixture",error));
 Save(Manifest("../weights.dat"));EXPECT_FALSE(packs.Resolve("fixture",error));
 Save(Manifest());std::ofstream(root/"packs"/"fixture"/"weights.dat")<<"changed";
 EXPECT_FALSE(packs.Resolve("fixture",error));EXPECT_NE(error.find("SHA-256"),std::string::npos);
}
TEST_F(ProfileManagerTest, RejectsUnknownSchemaMissingHashAndDuplicateRole) {
 ModelPackManager packs(root/"packs");std::string error;auto text=Manifest();
 text.replace(text.find("schema_version\":1"),17,"schema_version\":9");Save(text);EXPECT_FALSE(packs.Resolve("fixture",error));
 text=Manifest();auto start=text.find("ba7816");text.replace(start,64,"bad");Save(text);EXPECT_FALSE(packs.Resolve("fixture",error));
 text=Manifest();auto object=text.substr(text.find("[{" )+1);object=object.substr(0,object.size()-2);
 text.insert(text.size()-2,","+object);Save(text);EXPECT_FALSE(packs.Resolve("fixture",error));
}
TEST_F(ProfileManagerTest, MissingProfileComponentsFailWithActionableReason) {
 Save(Manifest());std::ofstream(root/"profiles"/"desktop.json")<<R"({"schema_version":1,"profile":"desktop","body":{"pipeline":"missing.pose","modelPack":"fixture"},"backend":{"preference":"missing.backend"}})";
 ModelPackManager packs(root/"packs");PluginRegistry registry;ProfileManager profiles(root/"profiles");std::string error;
 EXPECT_FALSE(profiles.Resolve("desktop",4,registry,packs,error));EXPECT_NE(error.find("missing.pose"),std::string::npos);
 EXPECT_FALSE(profiles.Resolve("desktop",9,registry,packs,error));
}
TEST_F(ProfileManagerTest, AutoFallbackSelectsOnlyBackendsAndKeepsDiagnostic) {
 Save(Manifest());
 std::ofstream(root/"profiles"/"desktop.json")<<R"({"schema_version":1,"profile":"desktop","body_by_capacity":[{"max_people":2,"pipeline":"fixture.pose","modelPack":"fixture"},{"max_people":8,"pipeline":"fixture.pose","modelPack":"fixture"}],"backend":{"preference":["missing.gpu","auto","fixture.cpu"],"allow_fallback":false}})";
 ModelPackManager packs(root/"packs"); PluginRegistry registry; ProfileManager profiles(root/"profiles"); std::string error;
 ASSERT_TRUE(registry.Register(QueryPose,error)); ASSERT_TRUE(registry.Register(QueryBackend,error));
 auto profile=profiles.Resolve("desktop",8,registry,packs,error); ASSERT_TRUE(profile)<<error;
 ASSERT_EQ(profile->backends.size(),1u); EXPECT_STREQ(profile->backends[0]->api.plugin_id,"fixture.cpu");
 EXPECT_NE(profile->fallback_reason.find("missing.gpu"),std::string::npos);
 EXPECT_EQ(profile->max_people,8); EXPECT_EQ(profile->output_hz,60); EXPECT_FALSE(profile->hands_enabled);
 EXPECT_FALSE(profile->allow_backend_fallback);
}

TEST_F(ProfileManagerTest, StrictProfileNamesAnUnsatisfiedCapability) {
 std::ofstream(root/"packs"/"fixture"/"detector.param",std::ios::binary)<<"abc";
 std::ofstream(root/"packs"/"fixture"/"detector.bin",std::ios::binary)<<"def";
 Save(Schema2Manifest());
 std::ofstream(root/"profiles"/"strict.json")<<R"({"schema_version":1,"profile":"strict","body":{"pipeline":"fixture.pose","modelPack":"fixture"},"required_capabilities":["body_pose","multi_person","gpu_input","vulkan","fp16-storage","fp16-arithmetic","android-hardware-buffer","external-sync-fd"],"backend":{"preference":["fixture.cpu"],"allow_fallback":false}})";
 ModelPackManager packs(root/"packs");PluginRegistry registry;ProfileManager profiles(root/"profiles");std::string error;
 ASSERT_TRUE(registry.Register(QueryPose,error));ASSERT_TRUE(registry.Register(QueryBackend,error));
 EXPECT_FALSE(profiles.Resolve("strict",2,registry,packs,error));EXPECT_NE(error.find("external-sync-fd"),std::string::npos)<<error;
}

TEST(ProfileManagerProductionProfiles, AreStrictSingleCompositionContracts) {
 struct Expected { const char* id; const char* pipeline; const char* pack; const char* backend; std::vector<std::string> requirements; };
 const std::vector<Expected> expected{
  {"android-ncnn-vulkan","pipeline.topdown","precision-t-26-ncnn-fp16","backend.ncnn.vulkan",{"body_pose","multi_person","gpu_input","vulkan","fp16-storage","fp16-arithmetic","android-hardware-buffer","external-sync-fd"}},
  {"android-ort-xnnpack","pipeline.topdown","precision-t-26","backend.ort.xnnpack",{"body_pose","multi_person","tensor_inference"}},
  {"android-ort-cpu","pipeline.topdown","precision-t-26","backend.ort.cpu",{"body_pose","multi_person","tensor_inference"}}
 };
 for(const auto& item:expected){
  std::ifstream stream(std::filesystem::path(HV_TEST_PROJECT_ROOT)/"profiles"/(std::string(item.id)+".json"));ASSERT_TRUE(stream.good())<<item.id;
  nlohmann::json profile;stream>>profile;EXPECT_EQ(profile.at("schema_version"),1);EXPECT_EQ(profile.at("profile"),item.id);
  ASSERT_TRUE(profile.contains("body"));EXPECT_FALSE(profile.contains("body_by_capacity"));
  EXPECT_EQ(profile.at("body").at("pipeline"),item.pipeline);EXPECT_EQ(profile.at("body").at("modelPack"),item.pack);
  EXPECT_FALSE(profile.at("hands").at("enabled").get<bool>());EXPECT_EQ(profile.at("body_fps"),30);EXPECT_EQ(profile.at("output").at("hz"),60);
  const auto& preference=profile.at("backend").at("preference");ASSERT_TRUE(preference.is_array());ASSERT_EQ(preference.size(),1u);EXPECT_EQ(preference[0],item.backend);
  EXPECT_FALSE(profile.at("backend").at("allow_fallback").get<bool>());
  EXPECT_EQ(profile.at("required_capabilities").get<std::vector<std::string>>(),item.requirements);
 }
}

TEST(AndroidBenchmarkProfiles, SelectOneProviderDisableHandsAndKeepBodyPolicy) {
 const std::pair<const char*,const char*> profiles[]{
  {"android-cpu-nohands","backend.ort.cpu"},
  {"android-nnapi-nohands","backend.ort.nnapi"},
  {"android-xnnpack-nohands","backend.ort.xnnpack"}
 };
 for(const auto& expected:profiles){
  std::ifstream stream(std::filesystem::path(HV_TEST_PROJECT_ROOT)/"profiles"/(std::string(expected.first)+".json"));
  ASSERT_TRUE(stream.good())<<expected.first;
  nlohmann::json profile;stream>>profile;
  EXPECT_EQ(profile.at("schema_version"),1);
  EXPECT_EQ(profile.at("profile"),expected.first);
  ASSERT_TRUE(profile.contains("body_by_capacity"));
  const auto& choices=profile.at("body_by_capacity");ASSERT_EQ(choices.size(),2u);
  EXPECT_EQ(choices[0].at("max_people"),2);EXPECT_EQ(choices[0].at("pipeline"),"pipeline.topdown");
  EXPECT_EQ(choices[0].at("modelPack"),"precision-t-26");
  EXPECT_EQ(choices[1].at("max_people"),8);EXPECT_EQ(choices[1].at("pipeline"),"pipeline.rtmo");
  EXPECT_EQ(choices[1].at("modelPack"),"rtmo-t-416");
  EXPECT_FALSE(profile.at("hands").at("enabled").get<bool>());
  EXPECT_EQ(profile.at("body_fps"),30);EXPECT_EQ(profile.at("output").at("hz"),60);
  const auto& preference=profile.at("backend").at("preference");
  ASSERT_TRUE(preference.is_array());ASSERT_EQ(preference.size(),1u);
  EXPECT_EQ(preference[0],expected.second);
  EXPECT_FALSE(profile.at("backend").at("allow_fallback").get<bool>());
 }
}
}
