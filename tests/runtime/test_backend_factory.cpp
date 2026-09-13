#include "host/backend_factory.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "test_support.h"
#include <gtest/gtest.h>

namespace {
std::string requested_provider;
HV_Result HV_CALL Reject(const HV_BackendConfigV1*,void** out,HV_ErrorBufferV1*){*out=nullptr;return HV_ERR_MODEL_LOAD;}
HV_Result HV_CALL CaptureAndReject(const HV_BackendConfigV1* config,void** out,HV_ErrorBufferV1*){
 requested_provider=config&&config->requested_provider_utf8?config->requested_provider_utf8:"";*out=nullptr;return HV_ERR_MODEL_LOAD;
}
HV_Result HV_CALL FailingQuery(uint32_t v,HV_PluginApiV1* out){
 auto result=HV_QueryOrtCpuPlugin(v,out);
 static HV_BackendApiV1 api=*out->backend;api.create=Reject;
 out->plugin_id="fixture.unavailable";out->backend=&api;return result;
}
HV_Result HV_CALL StrictQuery(uint32_t v,HV_PluginApiV1* out){
 auto result=HV_QueryOrtCpuPlugin(v,out);
 static HV_BackendApiV1 api=*out->backend;api.create=CaptureAndReject;
 out->plugin_id="fixture.strict";out->backend=&api;return result;
}
}
TEST(BackendFactory, FallsBackAndKeepsModuleAliveAndReason) {
 using namespace humanvision::runtime;
 const HV_BackendApiV1* api=nullptr;void* session=nullptr;
 HV_HostServicesV1 services{};
 {
  PluginRegistry registry;std::string error;
  ASSERT_TRUE(registry.Register(FailingQuery,error));ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error));
  BackendFactory factory({registry.Find("fixture.unavailable",0,error),registry.Find("backend.ort.cpu",0,error)});
  services=factory.Services();HV_BackendConfigV1 config{};config.struct_size=sizeof(config);config.api_version=HV_PLUGIN_API_V1;config.model_path_utf8=HV_TEST_BACKEND_MODEL_PATH;
  ASSERT_EQ(services.create_backend(services.context,&config,&api,&session,nullptr),HV_OK);
  EXPECT_NE(factory.Diagnostics().find(std::filesystem::path(HV_TEST_BACKEND_MODEL_PATH).filename().u8string()),std::string::npos);
 }
 HV_BackendSessionInfoV1 info{};info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
 EXPECT_EQ(api->session_info(session,&info),HV_OK);EXPECT_STREQ(info.actual,"CPU");
 EXPECT_NE(std::string(info.fallback_reason).find("fixture.unavailable"),std::string::npos);
 services.release_backend(nullptr,api,session);
}
TEST(BackendFactory, StrictSelectionPassesPluginIdentityAndNeverTriesFallback) {
 using namespace humanvision::runtime;PluginRegistry registry;std::string error;
 ASSERT_TRUE(registry.Register(StrictQuery,error));ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error));
 BackendFactory factory({registry.Find("fixture.strict",0,error),registry.Find("backend.ort.cpu",0,error)},false);
 auto services=factory.Services();const HV_BackendApiV1* api=nullptr;void* session=nullptr;
 HV_BackendConfigV1 config{};config.struct_size=sizeof(config);config.api_version=HV_PLUGIN_API_V1;config.model_path_utf8=HV_TEST_BACKEND_MODEL_PATH;
 requested_provider.clear();EXPECT_EQ(services.create_backend(services.context,&config,&api,&session,nullptr),HV_ERR_MODEL_LOAD);
 EXPECT_EQ(session,nullptr);EXPECT_EQ(api,nullptr);EXPECT_EQ(requested_provider,"fixture.strict");
}
