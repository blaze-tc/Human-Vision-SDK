# LONG_TERM_ROADMAP.md

# HumanVisionSDK Long-Term Roadmap

This file preserves the original product direction without allowing it to delay the first Demo.

## Stage D0/D1 - Validate the core experience

- Windows x64
- ONNX Runtime
- RTMDet + RTMPose
- configurable MaxBodies
- local video + RTSP
- Unity visualization/HUD
- real IPC multi-person tests

Decision gate: Is accuracy/latency good enough to continue with this model family and target hardware?

## Stage A - SDK Alpha

After D1 succeeds:

- formalize Unity runtime package
- Unity WebCamTexture / USB camera path
- improve tracker (evaluate ByteTrack)
- temporal pose filters
- segmentation/matting POC
- combined human mask + foreground RGBA
- consider per-body instance masks if actual game requirements need them
- 30-minute stability tests

## Stage B - Windows SDK Beta

- robust error/status API
- packaging and model deployment layout
- optional GPU backend evaluation based on measured bottlenecks
- benchmark matrix MaxBodies 1/2/4/6/8
- device RecommendedMaxBodies reporting
- license/NOTICE inventory
- 2-hour soak test

## Stage C - Android ARM64

- preserve the same public HumanVision C#/C contract
- ONNX Runtime ARM64 first
- WebCamTexture/Android camera behavior validation
- RTSP Android validation
- packaging/permissions/Gradle integration

## Stage D - RK3588 acceleration

- ONNX remains canonical source
- RKNN conversion scripts + checksums
- RKNN backend behind `IInferenceBackend`
- result parity comparison against ONNX reference
- optional MPP/RGA decode/convert acceleration
- AUTO backend selection with explicit reporting

## Stage E - Additional platforms/backends

As demand requires:

- Linux
- macOS/iOS
- TensorRT
- CoreML
- QNN
- OpenVINO

No platform-specific runtime type should leak into the Unity game API.

## Segmentation architecture target

Public result model should be able to evolve toward:

```text
HumanVisionFrame
  Bodies[]
  CombinedHumanMask?      # optional
  ForegroundRGBA?         # optional
  PerBodyMask(trackId)?   # optional capability, not guaranteed by every backend/model
  Stats
```

Do not commit to per-body masks until a real Unity feature requires them and the chosen segmentation strategy supports them efficiently.
