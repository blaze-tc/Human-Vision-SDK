#include "host/backend_factory.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "test_support.h"
#include <gtest/gtest.h>

namespace {
HV_Result HV_CALL Reject(const HV_BackendConfigV1*,void** out,HV_ErrorBufferV1*){*out=nullptr;return HV_ERR_MODEL_LOAD;}
HV_Result HV_CALL FailingQuery(uint32_t v,HV_PluginApiV1* out){
 auto result=HV_QueryOrtCpuPlugin(v,out);
 static HV_BackendApiV1 api=*out->backend;api.create=Reject;
 out->plugin_id="fixture.unavailable";out->backend=&api;return result;
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
 }
 HV_BackendSessionInfoV1 info{};info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
 EXPECT_EQ(api->session_info(session,&info),HV_OK);EXPECT_STREQ(info.actual,"CPU");
 EXPECT_NE(std::string(info.fallback_reason).find("fixture.unavailable"),std::string::npos);
 services.release_backend(nullptr,api,session);
}
