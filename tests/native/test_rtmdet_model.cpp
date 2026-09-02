#include "backend/onnx/onnx_runtime_backend.h"
#include "models/rtmdet/rtmdet_model.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

namespace {

humanvision::FrameBuffer LoadReferenceFrame() {
    humanvision::FrameBuffer frame;
    frame.width = humanvision::test::fixture::kRawWidth;
    frame.height = humanvision::test::fixture::kRawHeight;
    frame.stride_bytes = humanvision::test::fixture::kRawStrideBytes;
    frame.pixel_format = HV_PIXEL_BGR24;
    frame.frame_id = 9;
    frame.timestamp_us = 1234;
    frame.bytes = humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    return frame;
}

humanvision::FrameBuffer LoadMultiPersonFrame() {
    humanvision::FrameBuffer frame;
    frame.width = humanvision::test::fixture::kMultiRawWidth;
    frame.height = humanvision::test::fixture::kMultiRawHeight;
    frame.stride_bytes = humanvision::test::fixture::kMultiRawStrideBytes;
    frame.pixel_format = HV_PIXEL_BGR24;
    frame.frame_id = 10;
    frame.timestamp_us = 2345;
    frame.bytes = humanvision::test::ReadBytes(HV_TEST_MULTI_RAW_IMAGE_PATH);
    return frame;
}

double IntersectionOverUnion(
    const humanvision::Detection& actual,
    const std::array<double, 4>& expected) {
    const double x1 = std::max<double>(actual.x1, expected[0]);
    const double y1 = std::max<double>(actual.y1, expected[1]);
    const double x2 = std::min<double>(actual.x2, expected[2]);
    const double y2 = std::min<double>(actual.y2, expected[3]);
    const double intersection = std::max(0.0, x2 - x1) * std::max(0.0, y2 - y1);
    const double actual_area =
        std::max(0.0F, actual.x2 - actual.x1) * std::max(0.0F, actual.y2 - actual.y1);
    const double expected_area =
        std::max(0.0, expected[2] - expected[0]) * std::max(0.0, expected[3] - expected[1]);
    const double union_area = actual_area + expected_area - intersection;
    return union_area > 0.0 ? intersection / union_area : 0.0;
}

TEST(RtmdetModel, PreprocessMatchesD01GoldenSamples) {
    humanvision::RtmdetModel model(std::make_unique<humanvision::OnnxRuntimeBackend>());
    humanvision::DetectorInput input;
    std::string error;

    ASSERT_TRUE(model.Preprocess(LoadReferenceFrame(), input, error)) << error;
    EXPECT_EQ(input.resized_width, humanvision::test::fixture::kDetectorResizedWidth);
    EXPECT_EQ(input.resized_height, humanvision::test::fixture::kDetectorResizedHeight);
    EXPECT_NEAR(input.scale_x, humanvision::test::fixture::kDetectorScaleX, 1.0e-6F);
    EXPECT_NEAR(input.scale_y, humanvision::test::fixture::kDetectorScaleY, 1.0e-6F);
    ASSERT_EQ(input.normalized_chw.size(), 3U * 640U * 640U);

    for (const auto& sample : humanvision::test::fixture::kPreprocessSamples) {
        const int x = sample.x;
        const int y = sample.y;
        for (int channel = 0; channel < 3; ++channel) {
            const std::size_t index =
                static_cast<std::size_t>(channel) * 640U * 640U +
                static_cast<std::size_t>(y) * 640U + static_cast<std::size_t>(x);
            EXPECT_NEAR(
                input.normalized_chw[index],
                sample.normalized_bgr[static_cast<std::size_t>(channel)],
                1.0e-5F)
                << "sample x=" << x << " y=" << y << " channel=" << channel;
        }
    }
}

TEST(RtmdetModel, PreprocessSupportsAllPublicColorFormats) {
    struct Case {
        HV_PixelFormat format;
        std::vector<std::uint8_t> bytes;
    };
    const std::vector<Case> cases = {
        {HV_PIXEL_BGR24, {30, 20, 10}},
        {HV_PIXEL_RGB24, {10, 20, 30}},
        {HV_PIXEL_BGRA32, {30, 20, 10, 255}},
        {HV_PIXEL_RGBA32, {10, 20, 30, 255}},
    };
    humanvision::RtmdetModel model(std::make_unique<humanvision::OnnxRuntimeBackend>());
    for (const auto& test_case : cases) {
        humanvision::FrameBuffer frame;
        frame.width = 1;
        frame.height = 1;
        frame.stride_bytes = static_cast<int>(test_case.bytes.size());
        frame.pixel_format = test_case.format;
        frame.bytes = test_case.bytes;
        humanvision::DetectorInput input;
        std::string error;
        ASSERT_TRUE(model.Preprocess(frame, input, error)) << error;
        const std::size_t plane = 640U * 640U;
        EXPECT_NEAR(input.normalized_chw[0], (30.0F - 103.53F) / 57.375F, 1.0e-6F);
        EXPECT_NEAR(input.normalized_chw[plane], (20.0F - 116.28F) / 57.12F, 1.0e-6F);
        EXPECT_NEAR(input.normalized_chw[plane * 2U], (10.0F - 123.675F) / 58.395F, 1.0e-6F);
    }
}

TEST(RtmdetModel, RealOnnxOutputMatchesOfficialPyTorchGolden) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first: " << HV_TEST_DETECTOR_MODEL_PATH;
    }
    humanvision::RtmdetModel model(std::make_unique<humanvision::OnnxRuntimeBackend>());
    std::string error;
    ASSERT_TRUE(model.Load(HV_TEST_DETECTOR_MODEL_PATH, error)) << error;

    std::vector<humanvision::Detection> detections;
    float inference_ms = 0.0F;
    ASSERT_TRUE(model.Detect(
        LoadReferenceFrame(), 0.35F, 4, detections, inference_ms, error))
        << error;
    ASSERT_EQ(detections.size(), 1U);

    const std::array<double, 4> expected = {
        humanvision::test::fixture::kDetectorBboxXyxy[0],
        humanvision::test::fixture::kDetectorBboxXyxy[1],
        humanvision::test::fixture::kDetectorBboxXyxy[2],
        humanvision::test::fixture::kDetectorBboxXyxy[3]};
    const auto& actual = detections[0];
    const double max_abs = std::max(
        {std::abs(actual.x1 - expected[0]), std::abs(actual.y1 - expected[1]),
         std::abs(actual.x2 - expected[2]), std::abs(actual.y2 - expected[3])});
    const double iou = IntersectionOverUnion(actual, expected);
    RecordProperty("bbox_max_abs_px", max_abs);
    RecordProperty("bbox_iou", iou);
    RecordProperty("score", actual.score);
    RecordProperty("inference_ms", inference_ms);
    EXPECT_LE(max_abs, 1.5);
    EXPECT_GE(iou, 0.99);
    EXPECT_NEAR(
        actual.score, humanvision::test::fixture::kDetectorScore, 0.01F);
    EXPECT_GT(inference_ms, 0.0F);
}

TEST(RtmdetModel, RuntimeMaxBodiesCapsRealMultiPersonSelection) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first: " << HV_TEST_DETECTOR_MODEL_PATH;
    }
    humanvision::RtmdetModel model(std::make_unique<humanvision::OnnxRuntimeBackend>());
    std::string error;
    ASSERT_TRUE(model.Load(HV_TEST_DETECTOR_MODEL_PATH, error)) << error;
    const humanvision::FrameBuffer frame = LoadMultiPersonFrame();
    std::vector<humanvision::Detection> detections;
    float inference_ms = 0.0F;

    ASSERT_TRUE(model.Detect(frame, 0.35F, 1, detections, inference_ms, error))
        << error;
    EXPECT_EQ(detections.size(), 1U);

    ASSERT_TRUE(model.Detect(frame, 0.35F, 2, detections, inference_ms, error))
        << error;
    EXPECT_EQ(detections.size(), 2U);
    EXPECT_GE(detections[0].score, detections[1].score);
}

}  // namespace
