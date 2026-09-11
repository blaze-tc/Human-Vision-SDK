#include "plugins/backend/ort/ort_plugin.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <cstring>
#include <limits>

TEST(BackendPlugin, CpuRunsTensorAndRejectsInvalidDimensions) {
 HV_PluginApiV1 plugin{};plugin.struct_size=sizeof(plugin);
 ASSERT_EQ(HV_QueryOrtCpuPlugin(HV_PLUGIN_API_V1,&plugin),HV_OK);
 HV_BackendConfigV1 config{};config.struct_size=sizeof(config);config.api_version=HV_PLUGIN_API_V1;
 config.model_path_utf8=HV_TEST_BACKEND_MODEL_PATH;config.requested_provider_utf8="cpu";
 void* instance=nullptr;ASSERT_EQ(plugin.backend->create(&config,&instance,nullptr),HV_OK);
 struct Guard { const HV_BackendApiV1* api;void* p;~Guard(){api->destroy(p);} } guard{plugin.backend,instance};
 HV_BackendSessionInfoV1 info{};info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
 ASSERT_EQ(plugin.backend->session_info(instance,&info),HV_OK);EXPECT_STREQ(info.actual,"CPU");EXPECT_EQ(info.accelerated,0u);
 float values[]{2.5F,-1.0F};HV_TensorViewV1 input{};input.struct_size=sizeof(input);input.api_version=HV_PLUGIN_API_V1;
 input.name="input";input.element_type=1;input.rank=2;input.dimensions[0]=1;input.dimensions[1]=2;input.data=values;input.byte_count=sizeof(values);
 HV_TensorViewV1 output[1]{};uint32_t count=0;
 ASSERT_EQ(plugin.backend->run(instance,&input,1,output,1,&count,nullptr),HV_OK);
 ASSERT_EQ(count,1u);EXPECT_FLOAT_EQ(static_cast<const float*>(output[0].data)[0],3.5F);
 EXPECT_FLOAT_EQ(static_cast<const float*>(output[0].data)[1],0.0F);
 input.dimensions[0]=std::numeric_limits<int64_t>::max();
 EXPECT_EQ(plugin.backend->run(instance,&input,1,output,1,&count,nullptr),HV_ERR_INVALID_ARGUMENT);EXPECT_EQ(count,0u);
}
TEST(BackendPlugin, CpuRejectsUnavailableProviderWithoutCreatingSession) {
 HV_PluginApiV1 plugin{};plugin.struct_size=sizeof(plugin);
 ASSERT_EQ(HV_QueryOrtCpuPlugin(HV_PLUGIN_API_V1,&plugin),HV_OK);
 HV_BackendConfigV1 config{};config.struct_size=sizeof(config);config.api_version=HV_PLUGIN_API_V1;
 config.model_path_utf8=HV_TEST_BACKEND_MODEL_PATH;config.requested_provider_utf8="gpu";
 char message[256]{};HV_ErrorBufferV1 error{sizeof(error),HV_PLUGIN_API_V1,message,sizeof(message)};
 void* instance=nullptr;
 EXPECT_EQ(plugin.backend->create(&config,&instance,&error),HV_ERR_INVALID_ARGUMENT);
 EXPECT_EQ(instance,nullptr);EXPECT_NE(std::string(message).find("requested provider"),std::string::npos);
}
TEST(BackendPlugin, AcceleratedQueryMatchesCompiledCapabilities) {
 HV_PluginApiV1 plugin{};plugin.struct_size=sizeof(plugin);
#if defined(HV_USE_DIRECTML) || defined(__ANDROID__)
 EXPECT_EQ(HV_QueryOrtAcceleratedPlugin(HV_PLUGIN_API_V1,&plugin),HV_OK);
 EXPECT_EQ(plugin.type,HV_PLUGIN_BACKEND);EXPECT_GT(plugin.priority,0);
#else
 EXPECT_EQ(HV_QueryOrtAcceleratedPlugin(HV_PLUGIN_API_V1,&plugin),HV_ERR_NOT_INITIALIZED);
 EXPECT_EQ(plugin.backend,nullptr);
#endif
}
TEST(BackendPlugin, QnnIsExplicitlyUnavailableWithoutOptionalBuild) {
 HV_PluginApiV1 plugin{};plugin.struct_size=sizeof(plugin);
#ifndef HV_USE_QNN
 EXPECT_EQ(HV_QueryOrtQnnPlugin(HV_PLUGIN_API_V1,&plugin),HV_ERR_NOT_INITIALIZED);
 EXPECT_EQ(plugin.backend,nullptr);
#endif
}
