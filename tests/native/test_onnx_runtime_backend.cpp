#include "backend/onnx/onnx_runtime_backend.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

TEST(OnnxRuntimeBackend, MissingModelReturnsActionableError) {
    humanvision::OnnxRuntimeBackend backend;
    std::string error;

    EXPECT_FALSE(backend.Load(std::filesystem::path("missing-fixture.onnx"), error));
    EXPECT_NE(error.find("missing-fixture.onnx"), std::string::npos);
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
