#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* HV_Handle;

typedef enum HV_Result {
    HV_OK = 0,
    HV_NO_NEW_RESULT = 1,
    HV_ERR_INVALID_ARGUMENT = -1,
    HV_ERR_NOT_INITIALIZED = -2,
    HV_ERR_MODEL_LOAD = -3,
    HV_ERR_UNSUPPORTED_FORMAT = -4,
    HV_ERR_INTERNAL = -5
} HV_Result;

typedef enum HV_Backend {
    HV_BACKEND_AUTO = 0,
    HV_BACKEND_ONNX_CPU = 1
} HV_Backend;

typedef enum HV_PixelFormat {
    HV_PIXEL_RGBA32 = 1,
    HV_PIXEL_BGRA32 = 2,
    HV_PIXEL_RGB24 = 3,
    HV_PIXEL_BGR24 = 4
} HV_PixelFormat;

typedef struct HV_Config {
    int32_t struct_size;
    int32_t max_bodies;
    float detection_threshold;
    float pose_threshold;
    int32_t detection_interval;
    int32_t enable_tracking;
    HV_Backend backend;
    const char* detector_model_path_utf8;
    const char* pose_model_path_utf8;
} HV_Config;

typedef struct HV_VideoFrame {
    int32_t struct_size;
    int32_t width;
    int32_t height;
    int32_t stride_bytes;
    HV_PixelFormat pixel_format;
    int64_t frame_id;
    int64_t timestamp_us;
    const void* data;
    int32_t data_bytes;
} HV_VideoFrame;

typedef enum HV_JointType {
    HV_JOINT_NOSE = 0,
    HV_JOINT_LEFT_EYE = 1,
    HV_JOINT_RIGHT_EYE = 2,
    HV_JOINT_LEFT_EAR = 3,
    HV_JOINT_RIGHT_EAR = 4,
    HV_JOINT_LEFT_SHOULDER = 5,
    HV_JOINT_RIGHT_SHOULDER = 6,
    HV_JOINT_LEFT_ELBOW = 7,
    HV_JOINT_RIGHT_ELBOW = 8,
    HV_JOINT_LEFT_WRIST = 9,
    HV_JOINT_RIGHT_WRIST = 10,
    HV_JOINT_LEFT_HIP = 11,
    HV_JOINT_RIGHT_HIP = 12,
    HV_JOINT_LEFT_KNEE = 13,
    HV_JOINT_RIGHT_KNEE = 14,
    HV_JOINT_LEFT_ANKLE = 15,
    HV_JOINT_RIGHT_ANKLE = 16,
    HV_JOINT_COUNT = 17
} HV_JointType;

typedef struct HV_Joint {
    float x_px;
    float y_px;
    float x_norm;
    float y_norm;
    float confidence;
    uint8_t valid;
    uint8_t reserved[3];
} HV_Joint;

typedef struct HV_Rect {
    float x;
    float y;
    float width;
    float height;
} HV_Rect;

typedef struct HV_Body {
    int32_t struct_size;
    int32_t track_id;
    HV_Rect bbox_px;
    float detection_confidence;
    HV_Joint joints[HV_JOINT_COUNT];
} HV_Body;

typedef struct HV_ResultMeta {
    int32_t struct_size;
    int64_t result_sequence;
    int64_t source_frame_id;
    int64_t source_timestamp_us;
    int32_t body_count;
} HV_ResultMeta;

typedef struct HV_Stats {
    int32_t struct_size;
    float input_fps;
    float inference_fps;
    float detection_ms;
    float pose_ms;
    float tracking_ms;
    float total_ms;
    int64_t submitted_frames;
    int64_t processed_frames;
    int64_t dropped_frames;
} HV_Stats;

#ifdef __cplusplus
}
#endif
