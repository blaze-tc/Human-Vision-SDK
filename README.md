# Human Vision SDK

Independent Unity camera skeleton SDK for Windows x64 and Android ARM64.

Current preview: **0.4.0-preview.2**. Semantic Runtime Host and C plugin ABI,
data-only ModelPacks/profiles, common tracking/regions, independent hand jobs and
batched canonical rendering. Hand endpoints are palm, index fingertip and thumb tip.
Existing V1 API and script GUIDs remain compatible. No AzureKinectExamples dependency.

## Install with Unity Package Manager

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.2
```

The repository is private; Git credentials need repository access.
Alternative local `.unitypackage` and UPM `.tgz` assets: [Releases](https://github.com/blaze-tc/Human-Vision-SDK/releases).
Choose one installation method, avoiding duplicate SDK/plugin copies.

[Installation](docs/UPM_INSTALLATION.md) | [0.4 guide](docs/SDK_040_USER_GUIDE.md) | [Maintenance](docs/maintenance/START_HERE.md) | [Development status](docs/DEVELOPMENT_STATUS.md)

Use **HumanVision > Create Live Camera Demo** to create the camera and independent
settings scenes. UPM installs model files to StreamingAssets automatically in Editor.

Android preview is decoupled from inference, readback has backpressure, and CPU
thread pools are bounded. Phone performance and hand quality still require user
validation. This preview does not certify eight-person 30 FPS or metric 3D depth.
