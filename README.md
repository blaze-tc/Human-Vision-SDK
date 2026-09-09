# Human Vision SDK

Independent Unity camera skeleton SDK for Windows x64 and Android ARM64.

Current preview: **0.3.0-preview.4**. Camera/RTSP input, numbered recognition regions,
separate camera/settings scenes, independent line/sphere skeleton rendering and real
hand-model endpoints (palm, middle fingertip, thumb tip). No AzureKinectExamples dependency.

## Install with Unity Package Manager

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.4
```

The repository is private; Git credentials need repository access.
Alternative local `.unitypackage` and UPM `.tgz` assets: [Releases](https://github.com/blaze-tc/Human-Vision-SDK/releases).
Choose one installation method, avoiding duplicate SDK/plugin copies.

[Installation](docs/UPM_INSTALLATION.md) | [Camera/API guide](docs/SDK_LIVE_CAMERA_GUIDE.md) | [Development status](docs/DEVELOPMENT_STATUS.md)

Use **HumanVision > Create Live Camera Demo** to create the camera and independent
settings scenes. UPM installs model files to StreamingAssets automatically in Editor.

Android preview is decoupled from inference, readback has backpressure, and CPU
thread pools are bounded. Phone performance and hand quality still require user
validation. This preview does not certify eight-person 30 FPS or metric 3D depth.
