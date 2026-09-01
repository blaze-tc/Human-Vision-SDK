# DEMO_SCOPE.md

# HumanVision Demo Phase D0/D1 Scope

## 1. Objective

The first goal is not “finish the entire cross-platform SDK.” The first goal is to obtain a real, measurable Demo that can answer whether ordinary RGB cameras and IPC streams are stable enough for the target multi-person Unity games.

## 2. Success image

Unity displays the source video and, for every detected person up to `MaxBodies`:

- bounding box
- persistent `track_id`
- COCO-17 skeleton
- joint confidence

The HUD displays:

- source resolution
- input FPS
- inference FPS
- detection ms
- pose total ms
- tracking ms
- total processing ms
- current body count
- configured MaxBodies
- dropped frames
- backend name

## 3. D0 - Local Video Vertical Slice

### Input

Primary Demo input:

- Unity `VideoPlayer` -> `RenderTexture` -> AsyncGPUReadback -> RGBA32 -> `HV_SubmitFrame`

Fallback for unsupported GPU readback:

- CPU texture readback may be used for correctness verification only.

### Native processing

```text
RGBA32 frame
 -> color convert / resize
 -> RTMDet-tiny 640x640, person only
 -> score/NMS/top runtime MaxBodies
 -> tracker
 -> ROI affine
 -> RTMPose-s 256x192
 -> SimCC decode
 -> restore source coordinates
 -> latest result store
```

### Output

- latest result metadata
- dynamic body list
- body `track_id`
- bbox/confidence
- 17 joints in pixel and normalized coordinates
- per-stage stats

### Default settings

- `MaxBodies = 4`
- detection threshold = 0.35
- pose threshold = 0.30
- detector interval = 1 for correctness baseline
- source target = 1280x720 / 30fps when practical

Thresholds are runtime Demo configuration, not ABI constants.

## 4. D1 - RTSP IPC

After D0 is accepted, add native RTSP input with FFmpeg-compatible abstractions.

Requirements:

- H.264 first; H.265 if available without delaying H.264 validation
- TCP default; UDP optional
- read/decode occurs off Unity main thread
- latest decoded frame only
- reconnect state and retry interval
- Unity can switch Local Video / RTSP without changing body-processing code
- RTSP disconnect does not crash Unity

## 5. MaxBodies

`MaxBodies` is runtime configurable. Demo test points: 1, 2, 4, 6 when hardware permits.

The SDK must never expose `bodies[4]`. The public API uses caller-provided body buffers.

If the scene contains more people than configured, detector candidates are ranked and only the selected `MaxBodies` are processed by pose.

## 6. Skeleton schema

D0/D1 native schema is COCO-17:

0 Nose  
1 LeftEye  
2 RightEye  
3 LeftEar  
4 RightEar  
5 LeftShoulder  
6 RightShoulder  
7 LeftElbow  
8 RightElbow  
9 LeftWrist  
10 RightWrist  
11 LeftHip  
12 RightHip  
13 LeftKnee  
14 RightKnee  
15 LeftAnkle  
16 RightAnkle

Unity/game-specific mappings belong in adapters above the native SDK.

## 7. Tracker baseline

D0 uses a small deterministic tracker: center distance + IoU + short velocity prediction + lost-frame retention.

The tracker must be behind `IBodyTracker` so it can be replaced by ByteTrack after baseline visual quality is established.

D0 acceptance does not require perfect re-identification. It requires that normal continuous motion does not cause frequent ID changes.

## 8. Explicit exclusions

Not part of D0/D1:

- segmentation / alpha mask / foreground RGBA
- Android/iOS
- RKNN
- model quantization
- advanced re-identification
- action recognition
- production installer

## 9. Acceptance scenarios

### D0

- Single-person MP4: stable box and 17-point skeleton.
- Multi-person MP4: independent boxes and IDs.
- `MaxBodies=1/2/4`: result count respects configuration without recompiling native code.
- Unity remains responsive while inference is slower than render FPS.
- Dropped-frame count increases instead of latency growing without bound.

### D1

- IPC RTSP connects and streams into the same Unity visualization.
- 1~4 real participants can be visually evaluated.
- Disconnect does not crash; state is visible; reconnect can resume.
- Performance HUD provides enough evidence to decide whether model/hardware optimization is needed.
