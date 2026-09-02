#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <set>
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
    config.pose_model_path_utf8 = nullptr;
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

    config = humanvision::test::MakeConfig();
    config.pose_model_path_utf8 = "missing-pose.onnx";
    EXPECT_EQ(HV_Create(&config, &handle), HV_ERR_MODEL_LOAD);
    ASSERT_EQ(handle, nullptr);
    error = HV_GetLastError(nullptr);
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("missing-pose.onnx"), std::string::npos);
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
    EXPECT_GT(bodies[0].track_id, 0);
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
    for (int joint_index = 0; joint_index < HV_JOINT_COUNT; ++joint_index) {
        EXPECT_TRUE(bodies[0].joints[joint_index].valid);
        EXPECT_NEAR(
            bodies[0].joints[joint_index].x_px,
            humanvision::test::fixture::kPoseJoints[joint_index].x_px,
            2.0F);
        EXPECT_NEAR(
            bodies[0].joints[joint_index].y_px,
            humanvision::test::fixture::kPoseJoints[joint_index].y_px,
            2.0F);
        EXPECT_NEAR(
            bodies[0].joints[joint_index].confidence,
            humanvision::test::fixture::kPoseJoints[joint_index].confidence,
            0.02F);
    }

    HV_Stats stats{};
    stats.struct_size = sizeof(HV_Stats);
    ASSERT_EQ(HV_GetStats(handle, &stats), HV_OK);
    EXPECT_EQ(stats.submitted_frames, 1);
    EXPECT_EQ(stats.processed_frames, 1);
    EXPECT_EQ(stats.dropped_frames, 0);
    EXPECT_GT(stats.detection_ms, 0.0F);
    EXPECT_GT(stats.pose_ms, 0.0F);
    EXPECT_GE(stats.tracking_ms, 0.0F);
    EXPECT_GT(stats.total_ms, stats.detection_ms);
    HV_Destroy(handle);
}

TEST(CApi, RealTwoPersonPipelineHonorsMaxBodiesAndProducesUniqueTrackedPoses) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH) ||
        !std::filesystem::is_regular_file(HV_TEST_POSE_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first";
    }
    const auto pixels = humanvision::test::ReadBytes(HV_TEST_MULTI_RAW_IMAGE_PATH);
    for (const int max_bodies : {1, 2, 4}) {
        auto config = humanvision::test::MakeConfig(max_bodies);
        HV_Handle handle = nullptr;
        ASSERT_EQ(HV_Create(&config, &handle), HV_OK) << HV_GetLastError(nullptr);
        const auto frame = humanvision::test::MakeBgrFrame(
            pixels,
            humanvision::test::fixture::kMultiRawWidth,
            humanvision::test::fixture::kMultiRawHeight,
            9000 + max_bodies,
            1000000);
        ASSERT_EQ(HV_SubmitFrame(handle, &frame), HV_OK) << HV_GetLastError(handle);

        HV_ResultMeta meta{};
        meta.struct_size = sizeof(HV_ResultMeta);
        HV_Result result = HV_NO_NEW_RESULT;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            result = HV_GetLatestResultMeta(handle, &meta);
            if (result != HV_NO_NEW_RESULT) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_EQ(result, HV_OK) << HV_GetLastError(handle);
        const int expected_count = std::min(max_bodies, 2);
        ASSERT_EQ(meta.body_count, expected_count);

        std::vector<HV_Body> bodies(static_cast<std::size_t>(expected_count));
        int written = 0;
        ASSERT_EQ(HV_GetBodies(handle, bodies.data(), expected_count, &written), HV_OK);
        ASSERT_EQ(written, expected_count);
        std::set<int> track_ids;
        for (const auto& body : bodies) {
            EXPECT_GT(body.track_id, 0);
            track_ids.insert(body.track_id);
            int valid_joints = 0;
            for (const auto& joint : body.joints) {
                valid_joints += joint.valid != 0 ? 1 : 0;
            }
            EXPECT_EQ(valid_joints, HV_JOINT_COUNT);
        }
        EXPECT_EQ(track_ids.size(), bodies.size());
        HV_Destroy(handle);
    }
}

TEST(CApi, DetectionIntervalUsesTrackerPredictionBetweenDetectorRuns) {
    if (!std::filesystem::is_regular_file(HV_TEST_DETECTOR_MODEL_PATH) ||
        !std::filesystem::is_regular_file(HV_TEST_POSE_MODEL_PATH)) {
        GTEST_SKIP() << "Run D0.1 export first";
    }
    const auto pixels = humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    auto config = humanvision::test::MakeConfig(1);
    config.detection_interval = 2;
    HV_Handle handle = nullptr;
    ASSERT_EQ(HV_Create(&config, &handle), HV_OK) << HV_GetLastError(nullptr);

    int stable_track_id = -1;
    for (int frame_index = 0; frame_index < 3; ++frame_index) {
        const auto frame = humanvision::test::MakeBgrFrame(
            pixels,
            humanvision::test::fixture::kRawWidth,
            humanvision::test::fixture::kRawHeight,
            12000 + frame_index,
            frame_index * 33333LL);
        ASSERT_EQ(HV_SubmitFrame(handle, &frame), HV_OK) << HV_GetLastError(handle);
        HV_ResultMeta meta{};
        meta.struct_size = sizeof(HV_ResultMeta);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            if (HV_GetLatestResultMeta(handle, &meta) == HV_OK &&
                meta.source_frame_id == frame.frame_id) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_EQ(meta.source_frame_id, frame.frame_id) << HV_GetLastError(handle);
        ASSERT_EQ(meta.body_count, 1);
        HV_Body body{};
        int written = 0;
        ASSERT_EQ(HV_GetBodies(handle, &body, 1, &written), HV_OK);
        ASSERT_EQ(written, 1);
        if (frame_index == 0) {
            stable_track_id = body.track_id;
        } else {
            EXPECT_EQ(body.track_id, stable_track_id);
        }
        HV_Stats stats{};
        stats.struct_size = sizeof(HV_Stats);
        ASSERT_EQ(HV_GetStats(handle, &stats), HV_OK);
        if (frame_index == 1) {
            EXPECT_FLOAT_EQ(stats.detection_ms, 0.0F);
        } else {
            EXPECT_GT(stats.detection_ms, 0.0F);
        }
        EXPECT_GT(stats.pose_ms, 0.0F);
    }
    HV_Destroy(handle);
}

}  // namespace
