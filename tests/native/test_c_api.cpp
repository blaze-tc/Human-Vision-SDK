#include "test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

TEST(CApi, RejectsInvalidConfigurationAndMissingModel) {
    HV_Handle handle = nullptr;
    EXPECT_EQ(HV_Create(nullptr, &handle), HV_ERR_INVALID_ARGUMENT);
    auto config = humanvision::test::MakeConfig();
    EXPECT_EQ(HV_Create(&config, nullptr), HV_ERR_INVALID_ARGUMENT);

    config.struct_size = sizeof(HV_Config) - 1;
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_INVALID_ARGUMENT);
    config = humanvision::test::MakeConfig(0);
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_INVALID_ARGUMENT);
    config = humanvision::test::MakeConfig();
    config.detection_threshold = -0.1F;
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_INVALID_ARGUMENT);
    config = humanvision::test::MakeConfig();
    config.detection_interval = 0;
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_INVALID_ARGUMENT);
    config = humanvision::test::MakeConfig();
    config.backend = static_cast<HV_Backend>(999);
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_INVALID_ARGUMENT);

    config = humanvision::test::MakeConfig();
    config.detector_model_path_utf8 = "missing-detector.onnx";
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_MODEL_LOAD);
    ASSERT_EQ(handle, nullptr);
    const char* error = HV_GetLastError(nullptr);
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("missing-detector.onnx"), std::string::npos);
}

TEST(CApi, AcceptsRuntimeMaxBodiesAndCreateDestroyCycles) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first: " << HV_TEST_DETECTOR_MODEL_PATH;
    }
    for (const int max_bodies : {1, 2, 4, 6, 8}) {
        auto config = humanvision::test::MakeConfig(max_bodies);
        HV_Handle handle = nullptr;
        ASSERT_EQ(HV_Create(&config, &handle), HV_OK) << HV_GetLastError(nullptr);
        ASSERT_NE(handle, nullptr);
        config.max_bodies = max_bodies + 1;
        EXPECT_EQ(HV_Reconfigure(handle, &config), HV_OK);
        HV_Destroy(handle);
    }

    for (int cycle = 0; cycle < 3; ++cycle) {
        auto config = humanvision::test::MakeConfig();
        HV_Handle handle = nullptr;
        ASSERT_EQ(HV_Create(&config, &handle), HV_OK) << HV_GetLastError(nullptr);
        HV_Destroy(handle);
    }
}

TEST(CApi, RejectsUnsupportedPixelFormatWithoutCrashing) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first: " << HV_TEST_DETECTOR_MODEL_PATH;
    }
    auto config = humanvision::test::MakeConfig();
    HV_Handle handle = nullptr;
    ASSERT_EQ(HV_Create(&config, &handle), HV_OK) << HV_GetLastError(nullptr);
    std::vector<std::uint8_t> pixels(12, 0);
    auto frame = humanvision::test::MakeBgrFrame(pixels, 2, 2, 1);
    frame.pixel_format = static_cast<HV_PixelFormat>(999);

    EXPECT_EQ(HV_SubmitFrame(handle, &frame), HV_ERR_UNSUPPORTED_FORMAT);
    HV_Destroy(handle);
}

TEST(CApi, AsynchronouslyPublishesRealDetectorResultAndHonorsCapacity) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first: " << HV_TEST_DETECTOR_MODEL_PATH;
    }
    const auto pixels = humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    auto config = humanvision::test::MakeConfig(4);
    HV_Handle handle = nullptr;
    ASSERT_EQ(HV_Create(&config, &handle), HV_OK) << HV_GetLastError(nullptr);

    const auto frame = humanvision::test::MakeBgrFrame(
        pixels,
        humanvision::test::fixture::kRawWidth,
        humanvision::test::fixture::kRawHeight,
        4242,
        987654);
    ASSERT_EQ(HV_SubmitFrame(handle, &frame), HV_OK) << HV_GetLastError(handle);

    HV_ResultMeta meta{};
    meta.struct_size = sizeof(HV_ResultMeta);
    HV_Result result = HV_NO_NEW_RESULT;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        result = HV_GetLatestResultMeta(handle, &meta);
        if (result != HV_NO_NEW_RESULT) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(result, HV_OK) << HV_GetLastError(handle);
    EXPECT_EQ(meta.source_frame_id, 4242);
    EXPECT_EQ(meta.source_timestamp_us, 987654);
    ASSERT_EQ(meta.body_count, 1);
    EXPECT_EQ(HV_GetBodyCount(handle), 1);

    int written = -1;
    EXPECT_EQ(HV_GetBodies(handle, nullptr, 0, &written), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(written, 1);
    std::array<HV_Body, 1> bodies{};
    ASSERT_EQ(HV_GetBodies(handle, bodies.data(), 1, &written), HV_OK);
    ASSERT_EQ(written, 1);
    EXPECT_EQ(bodies[0].struct_size, sizeof(HV_Body));
    EXPECT_EQ(bodies[0].track_id, -1);
    EXPECT_NEAR(
        bodies[0].bbox_px.x,
        humanvision::test::fixture::kDetectorBboxXyxy[0],
        1.5F);
    EXPECT_NEAR(
        bodies[0].bbox_px.y,
        humanvision::test::fixture::kDetectorBboxXyxy[1],
        1.5F);
    EXPECT_NEAR(
        bodies[0].bbox_px.width,
        humanvision::test::fixture::kDetectorBboxXyxy[2] -
            humanvision::test::fixture::kDetectorBboxXyxy[0],
        1.5F);
    EXPECT_NEAR(
        bodies[0].bbox_px.height,
        humanvision::test::fixture::kDetectorBboxXyxy[3] -
            humanvision::test::fixture::kDetectorBboxXyxy[1],
        1.5F);
    EXPECT_NEAR(
        bodies[0].detection_confidence,
        humanvision::test::fixture::kDetectorScore,
        0.01F);

    HV_Stats stats{};
    stats.struct_size = sizeof(HV_Stats);
    ASSERT_EQ(HV_GetStats(handle, &stats), HV_OK);
    EXPECT_EQ(stats.submitted_frames, 1);
    EXPECT_EQ(stats.processed_frames, 1);
    EXPECT_EQ(stats.dropped_frames, 0);
    EXPECT_GT(stats.detection_ms, 0.0F);
    HV_Destroy(handle);
}

}  // namespace
