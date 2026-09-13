#include "backend/onnx/onnx_runtime_backend.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

TEST(OnnxRuntimeBackend, ExplicitGpuRequestNeverSilentlyFallsBack) {
    humanvision::OnnxRuntimeBackend backend(true);
    std::string error;
#if defined(HV_USE_DIRECTML)
    ASSERT_TRUE(backend.Load(HV_TEST_BACKEND_MODEL_PATH, error)) << error;
    humanvision::Tensor input;
    input.name = "input";
    input.shape = {1, 2};
    input.values = {2.5F, -1.0F};
    std::vector<humanvision::Tensor> outputs;
    ASSERT_TRUE(backend.Run(input, outputs, error)) << error;
    ASSERT_EQ(outputs.size(), 1U);
    ASSERT_EQ(outputs[0].values.size(), 2U);
    EXPECT_FLOAT_EQ(outputs[0].values[0], 3.5F);
    EXPECT_FLOAT_EQ(outputs[0].values[1], 0.0F);
#else
    EXPECT_FALSE(backend.Load(HV_TEST_BACKEND_MODEL_PATH, error));
    EXPECT_NE(error.find("GPU support"), std::string::npos);
#endif
}

TEST(OnnxRuntimeBackend, MissingModelReturnsActionableError) {
    humanvision::OnnxRuntimeBackend backend;
    std::string error;

    EXPECT_FALSE(backend.Load(std::filesystem::path("missing-fixture.onnx"), error));
    EXPECT_NE(error.find("missing-fixture.onnx"), std::string::npos);
}

TEST(OnnxRuntimeBackend, XnnpackProviderIsExplicitlyUnavailableOffAndroid) {
    humanvision::OnnxRuntimeBackend backend(humanvision::OnnxRuntimeProvider::Xnnpack, false);
    std::string error;
#if defined(__ANDROID__)
    EXPECT_TRUE(backend.Load(HV_TEST_BACKEND_MODEL_PATH, error)) << error;
    EXPECT_EQ(backend.ActualProvider(), "XNNPACK");
#else
    EXPECT_FALSE(backend.Load(HV_TEST_BACKEND_MODEL_PATH, error));
    EXPECT_NE(error.find("Android"), std::string::npos);
#endif
}

TEST(OnnxRuntimeBackend, LoadsAndRunsTinyPublicFixture) {
    humanvision::OnnxRuntimeBackend backend;
    std::string error;
    ASSERT_TRUE(backend.Load(HV_TEST_BACKEND_MODEL_PATH, error)) << error;

    humanvision::Tensor input;
    input.name = "input";
    input.shape = {1, 2};
    input.values = {2.5F, -1.0F};
    std::vector<humanvision::Tensor> outputs;

    ASSERT_TRUE(backend.Run(input, outputs, error)) << error;
    ASSERT_EQ(outputs.size(), 1U);
    EXPECT_EQ(outputs[0].name, "output");
    EXPECT_EQ(outputs[0].shape, (std::vector<std::int64_t>{1, 2}));
    ASSERT_EQ(outputs[0].values.size(), 2U);
    EXPECT_FLOAT_EQ(outputs[0].values[0], 3.5F);
    EXPECT_FLOAT_EQ(outputs[0].values[1], 0.0F);
}

}  // namespace
TEST(OnnxBackendDiagnostics, CpuReportsOnlyInitializedProvider) {
 humanvision::OnnxRuntimeBackend backend;
 EXPECT_EQ(backend.ActualProvider(), "uninitialized");
 std::string error;
 ASSERT_TRUE(backend.Load(HV_TEST_BACKEND_MODEL_PATH,error))<<error;
 EXPECT_EQ(backend.ActualProvider(),"CPU");
 EXPECT_TRUE(backend.FallbackReason().empty());
}
