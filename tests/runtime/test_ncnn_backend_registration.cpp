#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "host/backend_factory.h"
#include <gtest/gtest.h>

using namespace humanvision::runtime;

TEST(NcnnBackendRegistration, ExposesOnlyStrictGpuProviderAndCapabilities) {
    BackendFactory factory({}, false);
    std::string error;
    ASSERT_TRUE(factory.RegisterV2(HV_QueryNcnnVulkanPluginV2, error)) << error;
    constexpr uint64_t required = HV_CAP_GPU_INPUT | HV_CAP_TENSOR_INFERENCE |
        HV_CAP_VULKAN | HV_CAP_FP16_STORAGE | HV_CAP_FP16_ARITHMETIC |
        HV_CAP_ANDROID_HARDWARE_BUFFER | HV_CAP_EXTERNAL_SYNC_FD;
    auto selected = factory.FindV2("backend.ncnn.vulkan", required, error);
    ASSERT_NE(selected, nullptr) << error;
    EXPECT_EQ(selected->api.v1.capabilities & required, required);
    EXPECT_EQ(selected->api.v1.type, HV_PLUGIN_BACKEND);
    EXPECT_EQ(selected->api.v1.backend, nullptr);
    EXPECT_EQ(selected->api.gpu_pipeline, nullptr);
    EXPECT_NE(selected->api.gpu_backend, nullptr);
    EXPECT_EQ(factory.FindV2("backend.ort.cpu", required, error), nullptr);
}

TEST(NcnnBackendRegistration, RefusesMissingPhysicalIdentityWithoutFallback) {
    BackendFactory factory({}, false);
    std::string detail;
    ASSERT_TRUE(factory.RegisterV2(HV_QueryNcnnVulkanPluginV2, detail));
    HV_GpuBackendConfigV1 config{sizeof(config), HV_GPU_FRAME_API_V1,
        "fixture-manifest", "fixture-assets", "backend.ncnn.vulkan"};
    HV_GpuDeviceContextV1 device{sizeof(device), HV_GPU_FRAME_API_V1};
    const HV_GpuBackendApiV1* api = nullptr;
    void* instance = nullptr;
    char message[512]{};
    HV_ErrorBufferV1 error{sizeof(error), HV_PLUGIN_API_V1, message, sizeof(message)};
    EXPECT_NE(factory.CreateGpuBackend(&config, &device, &api, &instance, &error), HV_OK);
    EXPECT_EQ(api, nullptr);
    EXPECT_EQ(instance, nullptr);
    EXPECT_NE(std::string(message).find("UUID"), std::string::npos);
}

TEST(NcnnBackendRegistration, V3AdvertisesDetachedDetectorWithoutChangingV2) {
    BackendFactory factory({}, false);
    std::string error;
    ASSERT_TRUE(factory.RegisterV2(HV_QueryNcnnVulkanPluginV2, error)) << error;
    ASSERT_TRUE(factory.RegisterV3(HV_QueryNcnnVulkanPluginV3, error)) << error;
    auto old_api = factory.FindV2("backend.ncnn.vulkan", HV_CAP_GPU_INPUT, error);
    auto next_api = factory.FindV3("backend.ncnn.vulkan", HV_CAP_GPU_INPUT, error);
    ASSERT_NE(old_api, nullptr);
    ASSERT_NE(next_api, nullptr);
    EXPECT_EQ(old_api->api.gpu_backend->struct_size, sizeof(HV_GpuBackendApiV1));
    ASSERT_NE(next_api->api.gpu_backend->prepared, nullptr);
    EXPECT_EQ(next_api->api.gpu_backend->v1.struct_size, sizeof(HV_GpuBackendApiV2));
    EXPECT_EQ(next_api->api.gpu_backend->prepared->api_version, HV_GPU_PREPARED_API_V1);
    EXPECT_EQ(next_api->api.gpu_pipeline, nullptr);
}
