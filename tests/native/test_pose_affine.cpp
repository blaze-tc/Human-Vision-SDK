#include "test_support.h"

#include "core/frame_buffer.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/pose_affine.h"
#include "models/rtmpose/rtmpose_preprocess.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace {

humanvision::Detection ReferenceDetection() {
    const auto& bbox = humanvision::test::fixture::kPoseBboxXyxy;
    return humanvision::Detection{bbox[0], bbox[1], bbox[2], bbox[3], 1.0F};
}

TEST(PoseAffine, MatchesOfficialMmposeReferenceRoiAndRoundTripsCoordinates) {
    humanvision::PoseAffineTransform transform{};
    std::string error;
    ASSERT_TRUE(humanvision::BuildPoseAffine(ReferenceDetection(), transform, error))
        << error;
    EXPECT_NEAR(
        transform.center_x,
        humanvision::test::fixture::kPoseCenterXy[0],
        1.0e-4F);
    EXPECT_NEAR(
        transform.center_y,
        humanvision::test::fixture::kPoseCenterXy[1],
        1.0e-4F);
    EXPECT_NEAR(
        transform.scale_width,
        humanvision::test::fixture::kPoseScaleWidthHeight[0],
        1.0e-4F);
    EXPECT_NEAR(
        transform.scale_height,
        humanvision::test::fixture::kPoseScaleWidthHeight[1],
        1.0e-4F);
    for (std::size_t index = 0; index < transform.source_to_input.size(); ++index) {
        EXPECT_NEAR(
            transform.source_to_input[index],
            humanvision::test::fixture::kPoseSourceToInputAffine[index],
            1.0e-5F);
    }

    const humanvision::Point2f source{86.7745514F, 34.5652008F};
    const auto input = humanvision::TransformPoint(transform.source_to_input, source);
    const auto restored = humanvision::TransformPoint(transform.input_to_source, input);
    EXPECT_NEAR(restored.x, source.x, 1.0e-4F);
    EXPECT_NEAR(restored.y, source.y, 1.0e-4F);
}

TEST(RtmposePreprocess, MatchesOfficialMmposeNormalizedTensorSamples) {
    humanvision::FrameBuffer frame;
    frame.width = humanvision::test::fixture::kRawWidth;
    frame.height = humanvision::test::fixture::kRawHeight;
    frame.stride_bytes = humanvision::test::fixture::kRawStrideBytes;
    frame.pixel_format = HV_PIXEL_BGR24;
    frame.bytes = humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);

    humanvision::PoseInput input;
    std::string error;
    ASSERT_TRUE(humanvision::PreprocessRtmpose(
        frame, ReferenceDetection(), input, error)) << error;
    ASSERT_EQ(input.normalized_chw.size(), 3U * 192U * 256U);
    constexpr std::size_t plane_size = 192U * 256U;
    for (const auto& sample : humanvision::test::fixture::kPosePreprocessSamples) {
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const std::size_t index =
                channel * plane_size + static_cast<std::size_t>(sample.y) * 192U +
                static_cast<std::size_t>(sample.x);
            EXPECT_NEAR(
                input.normalized_chw[index],
                sample.normalized_rgb[channel],
                0.02F) << "sample (" << sample.x << ", " << sample.y
                       << ") channel " << channel;
        }
    }
}

}  // namespace
