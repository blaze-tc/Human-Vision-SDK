#include "humanvision_image_service_v1.h"
#include "common/cpu_image_service.h"
#include "host/plugin_registry.h"
#include "plugins/pipeline/yolo/yolo_tensor_pipeline.h"
#include <gtest/gtest.h>
#include <cstddef>

using namespace humanvision::runtime;
TEST(ImageServiceAbi, IndependentTableRejectsUnknownVersionAndShortOutput) {
 CpuImageService service;
 HV_ImageServiceApiV1 api{};api.struct_size=sizeof(api);api.api_version=1;
 EXPECT_EQ(service.Query(2,&api),HV_ERR_INVALID_ARGUMENT);
 api.struct_size=8;EXPECT_EQ(service.Query(1,&api),HV_ERR_INVALID_ARGUMENT);
 api.struct_size=sizeof(api);EXPECT_EQ(service.Query(1,&api),HV_OK);
 EXPECT_NE(api.QuerySupport,nullptr);EXPECT_NE(api.SubmitTransform,nullptr);
 EXPECT_NE(api.WaitTransform,nullptr);EXPECT_NE(api.CancelTransform,nullptr);EXPECT_NE(api.ReleasePrepared,nullptr);
 EXPECT_EQ(offsetof(HV_ImageServiceApiV1,context),8u);
}
TEST(ImageServiceAbi, StaticExtensionRegistrationIsExplicitAndDoesNotChangeOldPluginTables) {
 PluginRegistry registry;std::string error;
 ASSERT_TRUE(registry.Register(HV_QueryYoloCpuPipelineV1,error))<<error;
 EXPECT_FALSE(registry.RegisterImageExtension("missing",HV_QueryYoloCpuImageExtensionV1,error));
 EXPECT_FALSE(registry.RegisterImageExtension("pipeline.yolo.cpu",HV_QueryYoloTensorImageExtensionV1,error));
 ASSERT_TRUE(registry.RegisterImageExtension("pipeline.yolo.cpu",HV_QueryYoloCpuImageExtensionV1,error))<<error;
 auto module=registry.Find("pipeline.yolo.cpu",HV_CAP_BODY_POSE,error);ASSERT_NE(module,nullptr);
 EXPECT_TRUE(module->has_image_extension);EXPECT_STREQ(module->image_extension.plugin_id,"pipeline.yolo.cpu");
 EXPECT_EQ(module->api.struct_size,sizeof(HV_PluginApiV1));
 // A running reader may not have its supposedly immutable plugin mutated.
 EXPECT_FALSE(registry.RegisterImageExtension("pipeline.yolo.cpu",HV_QueryYoloCpuImageExtensionV1,error));
}
