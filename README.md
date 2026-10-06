# Human Vision SDK

Current integration preview: **0.4.0-preview.4** (2026-10-05).
中文接入文档（基于正式 Sensory 游戏项目，2026-10-07核查）：
[第一次安装使用引导](docs/user-guide/FIRST_INSTALL.md) ·
[技术栈](docs/user-guide/TECH_STACK.md) ·
[平台测试效果](docs/user-guide/PLATFORM_TEST_RESULTS.md) ·
[API调用与说明](docs/user-guide/API_REFERENCE.md)。

User-tested Windows PC and Android NCNN Vulkan body skeleton demos, unified
Camera/Video/RTSP input, shared numbered regions, Low/Medium/High model inputs,
UGUI settings and configurable object/line skeleton drawing.

Install **both packages**, Input first, in Unity Package Manager:

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.4
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.4
```

[Installation and game integration](docs/UPM_INSTALLATION.md) ·
[Download Release](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.4).
The two local unitypackages are an alternative to UPM. Import Input first, then
SDK; choose one installation method per project.
Android requires ARM64/API26/Vulkan. The current bundled profiles do not enable
real hand endpoint inference; **30 fresh complete Android observation FPS remains
unmet**. User acceptance of the visible demo is recorded separately from that goal.
Model evaluation/provenance markers and third-party terms remain in the package.

The following sections describe the older preview.3 baseline.

Independent Unity camera skeleton SDK for Windows x64 and Android ARM64.

Current preview: **0.4.0-preview.3**. Semantic Runtime Host and C plugin ABI,
data-only ModelPacks/profiles, common tracking/regions, independent hand jobs and
batched canonical rendering. Hand endpoints are palm, index fingertip and thumb tip.
Existing V1 API and script GUIDs remain compatible. No AzureKinectExamples dependency.

## Install with Unity Package Manager

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.3
```

The repository is private; Git credentials need repository access.
Alternative local `.unitypackage` and UPM `.tgz` assets: [Releases](https://github.com/blaze-tc/Human-Vision-SDK/releases).
Choose one installation method, avoiding duplicate SDK/plugin copies.

[Installation](docs/UPM_INSTALLATION.md) | [0.4 guide](docs/SDK_040_USER_GUIDE.md) | [Maintenance](docs/maintenance/START_HERE.md) | [Development status](docs/DEVELOPMENT_STATUS.md)

Use **HumanVision > Create Live Camera Demo** to create the camera and independent
settings scenes. UPM installs model files to StreamingAssets automatically in Editor.

Android preview is decoupled from inference, readback has backpressure, and CPU
thread pools are bounded. Preview.3 adds isolated CPU/XNNPACK/NNAPI no-hands
profiles, adaptive skeleton presentation and bottleneck diagnostics. Phone
performance and hand quality still require user validation. This preview does not
certify eight-person 30 FPS or metric 3D depth.
