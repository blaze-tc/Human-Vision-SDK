# DEVELOPMENT_STATUS.md

# HumanVisionSDK Development Status

**Status date:** 2026-09-01  
**Current stage:** D0 - Windows Local Video Vertical Slice  
**Current milestone:** D0.0 Repository & Build Bootstrap  
**Current implementation state:** documentation package prepared; code not yet started.

## Immediate user-visible target

The next meaningful checkpoint is **D0.4 Unity Local Video Demo**:

```text
MP4 -> Unity VideoPlayer -> HV_SubmitFrame -> RTMDet -> Tracker -> RTMPose
    -> Unity video + BBox + TrackId + COCO17 skeleton + performance HUD
```

Do not start RTSP until this local-video path is visibly working.

## Milestone state

- [ ] D0.0 Repository & Build Bootstrap
- [ ] D0.1 Python/OpenMMLab Reference + ONNX Contract
- [ ] D0.2 Native ONNX Runtime + RTMDet
- [ ] D0.3 RTMPose + Tracker + Native Video Benchmark
- [ ] D0.4 Unity Local Video Demo
- [ ] D1.0 RTSP IPC Input
- [ ] D1.1 Real 1~4 Person Field Validation
- [ ] D1.2 Demo Stabilization & Decision Report

## Scope lock

Until D1.2 is accepted:

- no Android
- no RKNN
- no segmentation/matting
- no TensorRT/CUDA optimization
- no action recognition

## Latest verification

No code verification has been run yet.

When a milestone is completed, append:

- commit hash
- exact build command
- exact test command
- test result counts
- benchmark media and settings
- measured FPS/latency
- known issues
- next milestone

## Advancement rule

Only mark a milestone complete when its acceptance criteria in `CODEX_DEMO_EXECUTION_PLAN.md` are met with real outputs. Then update `Current milestone` to the next item before implementing it.
