# AGENTS.md

## Project Goal

### Active user-approved plan (2026-09-07)

The user has prioritized independent SDK skeleton acceptance before
AzureKinectExamples integration, targeting Windows and RK3588 Android, 1-8
people at 30 fresh complete skeleton FPS per person, including real Hand,
Handtip and Thumb points for both hands. Read `docs/SDK_SKELETON_REQUIREMENTS.md`
and `docs/SDK_SKELETON_EXECUTION_PLAN.md` in addition to the mandatory documents
below. `docs/DEVELOPMENT_STATUS.md` identifies the sole active milestone.

The historical D0/D1 scope below remains the regression baseline. The active
S-series plan supersedes its RTSP-first ordering and permits model/schema and
platform acceleration work only in the corresponding S milestone. It does not
authorize segmentation, vendor sensor rewrites, or early Azure integration.

Build HumanVisionSDK in two layers:

1. **Immediate goal:** a testable Windows x64 Unity vertical-slice Demo for multi-person RGB-camera vision.
2. **Long-term goal:** a reusable cross-platform Unity Human Vision SDK.

The immediate goal always has priority until `docs/DEVELOPMENT_STATUS.md` explicitly advances to the productization stage.

## Mandatory read order

Before changing code, read:

1. `docs/DEVELOPMENT_STATUS.md`
2. `docs/DEMO_SCOPE.md`
3. `docs/ARCHITECTURE.md`
4. `docs/MODEL_MANIFEST.md`
5. `docs/SDK_API.md`
6. `docs/TOOLCHAIN.md`
7. `docs/CODEX_DEMO_EXECUTION_PLAN.md`

`docs/LONG_TERM_ROADMAP.md` is background only. Do not implement later-stage features early.

## Current Demo target

Windows x64 + Unity 2022.3 LTS:

- local MP4 / Unity VideoPlayer frame input
- RTMDet-tiny person detection
- runtime-configurable `MaxBodies`, default 4
- COCO-17 RTMPose-s pose
- lightweight tracker with stable `track_id`
- latest-frame asynchronous processing
- Unity overlay: video, boxes, IDs, skeleton
- debug HUD: input/inference FPS and per-stage timing
- RTSP IPC input after local-video Demo is verified

## Architecture rules

- C++17 core; C ABI across Unity/native boundary.
- HumanVisionCore must not depend on Unity.
- The public Unity API must not expose RTMDet, RTMPose, ONNX Runtime, FFmpeg, RKNN, or model-specific types.
- ONNX is the canonical deployable model format for the generic backend.
- `MaxBodies` is runtime configurable. Never hard-code a body capacity of 4.
- A single body may contain a fixed COCO-17 joint array because that is a skeleton schema, not a body-count limit.
- Use asynchronous `HV_SubmitFrame()` semantics. Never block the Unity main thread waiting for detector + pose inference.
- Latest frame wins. Old unprocessed frames may be dropped to keep latency bounded.
- No per-frame JSON in the native/managed hot path.
- Avoid per-frame heap allocations after warm-up; reuse frame/body/result buffers.
- Track ID and display index are separate concepts.
- Unity objects are updated on the Unity main thread only.

## Demo scope guard

Until `DEVELOPMENT_STATUS.md` changes stage, do **not** implement:

- Android/iOS/Linux/macOS packaging
- RKNN/RK3588
- TensorRT/CUDA/DirectML optimization
- segmentation/matting/cutout
- interaction/action recognition
- polished UPM release packaging

If a future feature would require a small interface hook now, define the narrow interface only; do not implement the feature.

## Milestone discipline

Implement only the current milestone from `docs/DEVELOPMENT_STATUS.md`.

For each milestone:

1. write or update the acceptance test first
2. run it and confirm the expected failure when applicable
3. implement the minimum change
4. build all affected targets
5. run unit tests
6. run the relevant integration/golden test
7. run the milestone benchmark if applicable
8. update `docs/DEVELOPMENT_STATUS.md` with exact commands/results
9. commit only verified changes

Do not use fake fixed boxes, fake joints, forced success codes, or hard-coded sample results to satisfy acceptance tests.

## Error handling

- Missing model: fail initialization with an actionable error string.
- Unsupported pixel format: reject the frame; do not crash.
- Frame queue pressure: increment dropped-frame stats and keep latest frame.
- RTSP disconnect: enter reconnect/error state without crashing Unity.
- Inference exception/native error: preserve previous valid result only if the result metadata clearly shows its source frame; never mislabel stale data as current.
