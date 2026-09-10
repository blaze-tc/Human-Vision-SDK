#include "humanvision/humanvision_c.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

static_assert(sizeof(void*) == 8, "The recorded V1 ABI contract is Windows x64.");

static_assert(HV_OK == 0);
static_assert(HV_NO_NEW_RESULT == 1);
static_assert(HV_ERR_INVALID_ARGUMENT == -1);
static_assert(HV_ERR_NOT_INITIALIZED == -2);
static_assert(HV_ERR_MODEL_LOAD == -3);
static_assert(HV_ERR_UNSUPPORTED_FORMAT == -4);
static_assert(HV_ERR_INTERNAL == -5);

static_assert(HV_BACKEND_AUTO == 0);
static_assert(HV_BACKEND_ONNX_CPU == 1);

static_assert(HV_PIXEL_RGBA32 == 1);
static_assert(HV_PIXEL_BGRA32 == 2);
static_assert(HV_PIXEL_RGB24 == 3);
static_assert(HV_PIXEL_BGR24 == 4);

static_assert(HV_JOINT_NOSE == 0);
static_assert(HV_JOINT_LEFT_EYE == 1);
static_assert(HV_JOINT_RIGHT_EYE == 2);
static_assert(HV_JOINT_LEFT_EAR == 3);
static_assert(HV_JOINT_RIGHT_EAR == 4);
static_assert(HV_JOINT_LEFT_SHOULDER == 5);
static_assert(HV_JOINT_RIGHT_SHOULDER == 6);
static_assert(HV_JOINT_LEFT_ELBOW == 7);
static_assert(HV_JOINT_RIGHT_ELBOW == 8);
static_assert(HV_JOINT_LEFT_WRIST == 9);
static_assert(HV_JOINT_RIGHT_WRIST == 10);
static_assert(HV_JOINT_LEFT_HIP == 11);
static_assert(HV_JOINT_RIGHT_HIP == 12);
static_assert(HV_JOINT_LEFT_KNEE == 13);
static_assert(HV_JOINT_RIGHT_KNEE == 14);
static_assert(HV_JOINT_LEFT_ANKLE == 15);
static_assert(HV_JOINT_RIGHT_ANKLE == 16);
static_assert(HV_JOINT_COUNT == 17);

#define HV_EXPECT_LAYOUT(type, size, alignment) \
    static_assert(sizeof(type) == size);         \
    static_assert(alignof(type) == alignment);   \
    static_assert(std::is_standard_layout_v<type>)

HV_EXPECT_LAYOUT(HV_Config, 48, 8);
static_assert(offsetof(HV_Config, struct_size) == 0);
static_assert(offsetof(HV_Config, max_bodies) == 4);
static_assert(offsetof(HV_Config, detection_threshold) == 8);
static_assert(offsetof(HV_Config, pose_threshold) == 12);
static_assert(offsetof(HV_Config, detection_interval) == 16);
static_assert(offsetof(HV_Config, enable_tracking) == 20);
static_assert(offsetof(HV_Config, backend) == 24);
static_assert(offsetof(HV_Config, detector_model_path_utf8) == 32);
static_assert(offsetof(HV_Config, pose_model_path_utf8) == 40);

HV_EXPECT_LAYOUT(HV_VideoFrame, 56, 8);
static_assert(offsetof(HV_VideoFrame, struct_size) == 0);
static_assert(offsetof(HV_VideoFrame, width) == 4);
static_assert(offsetof(HV_VideoFrame, height) == 8);
static_assert(offsetof(HV_VideoFrame, stride_bytes) == 12);
static_assert(offsetof(HV_VideoFrame, pixel_format) == 16);
static_assert(offsetof(HV_VideoFrame, frame_id) == 24);
static_assert(offsetof(HV_VideoFrame, timestamp_us) == 32);
static_assert(offsetof(HV_VideoFrame, data) == 40);
static_assert(offsetof(HV_VideoFrame, data_bytes) == 48);

HV_EXPECT_LAYOUT(HV_Joint, 24, 4);
static_assert(offsetof(HV_Joint, x_px) == 0);
static_assert(offsetof(HV_Joint, y_px) == 4);
static_assert(offsetof(HV_Joint, x_norm) == 8);
static_assert(offsetof(HV_Joint, y_norm) == 12);
static_assert(offsetof(HV_Joint, confidence) == 16);
static_assert(offsetof(HV_Joint, valid) == 20);
static_assert(offsetof(HV_Joint, reserved) == 21);

HV_EXPECT_LAYOUT(HV_Rect, 16, 4);
static_assert(offsetof(HV_Rect, x) == 0);
static_assert(offsetof(HV_Rect, y) == 4);
static_assert(offsetof(HV_Rect, width) == 8);
static_assert(offsetof(HV_Rect, height) == 12);

HV_EXPECT_LAYOUT(HV_Body, 436, 4);
static_assert(offsetof(HV_Body, struct_size) == 0);
static_assert(offsetof(HV_Body, track_id) == 4);
static_assert(offsetof(HV_Body, bbox_px) == 8);
static_assert(offsetof(HV_Body, detection_confidence) == 24);
static_assert(offsetof(HV_Body, joints) == 28);

HV_EXPECT_LAYOUT(HV_ResultMeta, 40, 8);
static_assert(offsetof(HV_ResultMeta, struct_size) == 0);
static_assert(offsetof(HV_ResultMeta, result_sequence) == 8);
static_assert(offsetof(HV_ResultMeta, source_frame_id) == 16);
static_assert(offsetof(HV_ResultMeta, source_timestamp_us) == 24);
static_assert(offsetof(HV_ResultMeta, body_count) == 32);

HV_EXPECT_LAYOUT(HV_Stats, 56, 8);
static_assert(offsetof(HV_Stats, struct_size) == 0);
static_assert(offsetof(HV_Stats, input_fps) == 4);
static_assert(offsetof(HV_Stats, inference_fps) == 8);
static_assert(offsetof(HV_Stats, detection_ms) == 12);
static_assert(offsetof(HV_Stats, pose_ms) == 16);
static_assert(offsetof(HV_Stats, tracking_ms) == 20);
static_assert(offsetof(HV_Stats, total_ms) == 24);
static_assert(offsetof(HV_Stats, submitted_frames) == 32);
static_assert(offsetof(HV_Stats, processed_frames) == 40);
static_assert(offsetof(HV_Stats, dropped_frames) == 48);

#undef HV_EXPECT_LAYOUT

TEST(ApiContract, ExportsEveryV1SymbolUsedByManagedCode) {
    // Volatile loads retain import relocations in optimized Release builds.
    // Comparing a known function address directly with nullptr can fold to true.
#define HV_REQUIRE_SYMBOL(name) { auto volatile address = &name; EXPECT_NE(address, nullptr); }
    HV_REQUIRE_SYMBOL(HV_GetVersionString);
    HV_REQUIRE_SYMBOL(HV_Create);
    HV_REQUIRE_SYMBOL(HV_Reconfigure);
    HV_REQUIRE_SYMBOL(HV_SubmitFrame);
    HV_REQUIRE_SYMBOL(HV_GetLatestResultMeta);
    HV_REQUIRE_SYMBOL(HV_GetBodyCount);
    HV_REQUIRE_SYMBOL(HV_GetBodies);
    HV_REQUIRE_SYMBOL(HV_GetHandJoints);
    HV_REQUIRE_SYMBOL(HV_GetStats);
    HV_REQUIRE_SYMBOL(HV_SetRegions);
    HV_REQUIRE_SYMBOL(HV_GetRegionAssignments);
    HV_REQUIRE_SYMBOL(HV_GetLastError);
    HV_REQUIRE_SYMBOL(HV_Destroy);
#undef HV_REQUIRE_SYMBOL
    EXPECT_NE(HV_GetVersionString(), nullptr);
}

}  // namespace
