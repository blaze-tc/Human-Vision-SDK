# SDK_API.md

# HumanVision D0 Public C ABI

## 1. ABI principles

- C ABI only across Unity/native boundary.
- No STL types in public headers.
- Body count is dynamic.
- COCO-17 joint count is a schema constant.
- `HV_SubmitFrame()` is asynchronous and returns after the frame is accepted/copied or rejected.
- Unity polls the latest completed result.
- D0 uses caller-provided buffers for bodies.

## 2. Result/error codes

```c
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
```

D0 does not add RKNN/TensorRT enum values until those backends exist.

## 3. Config

```c
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
```

Rules:

- `max_bodies >= 1`.
- Default Demo value is 4, not a hard maximum.
- Detector and pose model paths are required for the D0 detector/tracker/pose pipeline.
- `detection_interval >= 1`. With tracking enabled, intermediate frames use tracker velocity prediction and still run pose on the predicted ROIs; a value of 1 runs detection on every processed frame.
- With tracking disabled, detection runs on every processed frame and returned `track_id` values are `-1`.
- `struct_size` supports future backward-compatible extension.

## 4. Frame input

```c
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
```

D0 contract: `HV_SubmitFrame` copies the required bytes into a reusable internal latest-frame slot before returning. The caller may reuse its source buffer after `HV_SubmitFrame` returns.

## 5. COCO-17 joint schema

```c
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
```

## 6. Body

```c
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
```

`joints[17]` is valid because the skeleton schema is fixed. There must be no public `HV_Body bodies[4]` container.

## 7. Result metadata and stats

```c
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
```

`detection_ms`, `pose_ms`, and `tracking_ms` are the latest completed frame's
full stage times. A deliberately skipped detector stage reports `0` for
`detection_ms`; `pose_ms` is the total across all selected bodies.

## 8. Functions

```c
HV_Result HV_Create(const HV_Config* config, HV_Handle* out_handle);
HV_Result HV_Reconfigure(HV_Handle handle, const HV_Config* config);

HV_Result HV_SubmitFrame(HV_Handle handle, const HV_VideoFrame* frame);

HV_Result HV_GetLatestResultMeta(HV_Handle handle, HV_ResultMeta* out_meta);
int32_t   HV_GetBodyCount(HV_Handle handle);
HV_Result HV_GetBodies(
    HV_Handle handle,
    HV_Body* out_bodies,
    int32_t capacity,
    int32_t* written);
HV_Result HV_GetStats(HV_Handle handle, HV_Stats* out_stats);

const char* HV_GetLastError(HV_Handle handle);
void HV_Destroy(HV_Handle handle);
```

`HV_GetBodies` never performs a partial copy. If `capacity` is smaller than the completed snapshot's body count, it returns `HV_ERR_INVALID_ARGUMENT`, writes the required count to `written`, and leaves `out_bodies` unchanged. A caller can then reuse or grow its buffer outside the per-frame hot path.

## 9. Snapshot consistency rule

`HV_GetLatestResultMeta`, `HV_GetBodyCount`, and `HV_GetBodies` must read from a completed result snapshot. The implementation must prevent the worker from partially mutating the snapshot while Unity copies it.

Recommended implementation: double-buffer result snapshots plus a sequence number.

## 10. Unity managed API target

```csharp
public sealed class HumanVisionConfig
{
    [Min(1)] public int MaxBodies = 4;
    public float DetectionThreshold = 0.35f;
    public float PoseThreshold = 0.30f;
    [Min(1)] public int DetectionInterval = 1;
}

public readonly struct HumanVisionJoint
{
    public readonly Vector2 Pixel;
    public readonly Vector2 Normalized;
    public readonly float Confidence;
    public readonly bool Valid;
}

public sealed class HumanVisionBody
{
    public int TrackId;
    public Rect BoundingBoxPixels;
    public float DetectionConfidence;
    public HumanVisionJoint[] Joints; // allocated/reused outside per-frame hot path
}
```

The Unity public layer must expose HumanVision concepts, not model/runtime implementation names.

## 11. Future ABI hooks

Segmentation, per-body masks, foreground RGBA, RKNN and native RTSP control will be added as separate APIs after the D0/D1 interface is stable. Do not add dummy mask functions to D0 just to “reserve” them.
