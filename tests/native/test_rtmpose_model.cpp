#include "test_support.h"

#include "backend/onnx/onnx_runtime_backend.h"
#include "core/frame_buffer.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/rtmpose_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace {

humanvision::FrameBuffer ReferenceFrame() {
    humanvision::FrameBuffer frame;
    frame.width = humanvision::test::fixture::kRawWidth;
    frame.height = humanvision::test::fixture::kRawHeight;
    frame.stride_bytes = humanvision::test::fixture::kRawStrideBytes;
    frame.pixel_format = HV_PIXEL_BGR24;
    frame.bytes = humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    return frame;
}

humanvision::Detection ReferenceDetection() {
    const auto& bbox = humanvision::test::fixture::kPoseBboxXyxy;
    return humanvision::Detection{bbox[0], bbox[1], bbox[2], bbox[3], 1.0F};
}

TEST(RtmposeModel, ReportsMissingModelWithActionablePath) {
    humanvision::RtmposeModel model(
        std::make_unique<humanvision::OnnxRuntimeBackend>(humanvision::test::kUseGpu));
    std::string error;
    EXPECT_FALSE(model.Load("missing-rtmpose.onnx", error));
    EXPECT_NE(error.find("missing-rtmpose.onnx"), std::string::npos);
}

TEST(RtmposeModel, MatchesOfficialPytorchGoldenJoints) {
    if (!std::filesystem::is_regular_file(HV_TEST_POSE_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first: " << HV_TEST_POSE_MODEL_PATH;
    }
    humanvision::RtmposeModel model(
        std::make_unique<humanvision::OnnxRuntimeBackend>(humanvision::test::kUseGpu));
    std::string error;
    ASSERT_TRUE(model.Load(HV_TEST_POSE_MODEL_PATH, error)) << error;
    std::array<HV_Joint, HV_JOINT_COUNT> joints{};
    float inference_ms = 0.0F;
    ASSERT_TRUE(model.Estimate(
        ReferenceFrame(),
        ReferenceDetection(),
        0.30F,
        joints,
        inference_ms,
        error)) << error;
    EXPECT_GT(inference_ms, 0.0F);
    float coordinate_max_abs_px = 0.0F;
    float score_max_abs = 0.0F;
    for (int index = 0; index < HV_JOINT_COUNT; ++index) {
        const auto& expected = humanvision::test::fixture::kPoseJoints[index];
        coordinate_max_abs_px = std::max(
            coordinate_max_abs_px,
            std::max(
                std::abs(joints[index].x_px - expected.x_px),
                std::abs(joints[index].y_px - expected.y_px)));
        score_max_abs = std::max(
            score_max_abs,
            std::abs(joints[index].confidence - expected.confidence));
        EXPECT_TRUE(joints[index].valid);
        EXPECT_NEAR(joints[index].x_px, expected.x_px, 1.5F) << index;
        EXPECT_NEAR(joints[index].y_px, expected.y_px, 1.5F) << index;
        EXPECT_NEAR(joints[index].confidence, expected.confidence, 0.02F) << index;
        EXPECT_NEAR(
            joints[index].x_norm,
            joints[index].x_px / humanvision::test::fixture::kRawWidth,
            1.0e-6F);
        EXPECT_NEAR(
            joints[index].y_norm,
            joints[index].y_px / humanvision::test::fixture::kRawHeight,
            1.0e-6F);
    }
    RecordProperty("coordinate_max_abs_px", coordinate_max_abs_px);
    RecordProperty("score_max_abs", score_max_abs);
    RecordProperty("inference_ms", inference_ms);
}

}  // namespace
