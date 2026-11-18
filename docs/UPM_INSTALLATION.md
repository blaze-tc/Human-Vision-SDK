# Human Vision SDK 安装与接入

SDK **0.4.0-preview.16** / Input **0.1.0-preview.9**，Unity **2021.3+**，Windows x64 / Android ARM64。
官方 Samples 包含 **SDK Settings** 和 **Runtime Prefab Acceptance**。源码和两个 Git UPM 子目录位于同一个 GitHub 仓库。

## 安装顺序

先安装 Input，等编译完成，再安装 SDK。Input 负责 Camera/Video/RTSP 采集与预览，
SDK 负责推理、骨骼、跟踪、人数与区域。选择 Git UPM、tgz 或 unitypackage 中的一种方式。
已有项目先备份配置，更换安装方式时移除原 SDK/Input 副本，避免重复脚本、GUID 与 native 库。
已有内嵌包的项目在 Unity 关闭后更新 `Packages/com.blazetc.humanvision`，同时更新 Input 到 0.1.0-preview.9，并保留项目配置。重新打开 Unity 后，SDK 自动安装桌面 YOLO 配置和图结构。切换安装方式前先移除旧副本。

Git：Window → Package Manager → + → Add package from git URL，依次添加：

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.16
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.16
```

必须显式添加两个 Git 子目录；SDK 的版本依赖不能自动发现另一个 Git 包。
两包使用同一个发布标签；Input 的实际包版本为 0.1.0-preview.9。

离线：先导入 `HumanVisionInput-0.1.0-preview.9.unitypackage`，再导入
`HumanVisionSDK-0.4.0-preview.16.unitypackage`。每步等待导入与编译完成。
离线包将随附资源导入 `Assets/HumanVision/RuntimeData`。Unity 会自动更新运行目录，保留通过校验的本地 Android 资源及其索引。也可通过 **HumanVision → Install Packaged Models** 手动重试。
UPM tarball：依次安装 `com.blazetc.humanvision.input-0.1.0-preview.9.tgz` 和
`com.blazetc.humanvision-0.4.0-preview.16.tgz`。用附件 `SHA256SUMS.txt` 校验下载内容。

## Android 完整 CPU / NPU 资源

preview.16 的 SDK 支持 RK3588 NPU 低、中、高三档。已安装本地三档资源的项目升级时，备份项目中的资源和构建设置，并核对配套交付的 Input 原生库哈希。
新接入 Git UPM 项目时，安装上述两个子目录，再导入与 preview.16 及 Input preview.9 匹配的本地 RK3588 NPU 三档资源包，执行 **HumanVision → Enable Local Android NPU Qualities**。
使用离线包的项目可使用与主包和安装布局配套的本地资源。若仍在使用 preview.12 的独立 CPU/NPU 补充包，按该版本的配套说明安装。
详细步骤见[本地 Android 资源部署](user-guide/LOCAL_ANDROID_CPU_YOLO.md)。

公共包提供 SDK、Input、原生依赖和 Android Vulkan 模型。Android CPU 独立 YOLO ONNX、RKNN 模型和 `librknnrt.so` 通过匹配的本地补充包提供。
带完整本地模型的测试 APK、补充包和用户视频保存在本地交付目录。缺少所选模式的资源时，SDK 会报告初始化错误。

Android：ARM64、IL2CPP、API 26+；Vulkan 模式需要兼容的 GPU。RK3588 NPU 需要匹配设备和驱动。
Android 的 GPU、CPU 和 RK3588 NPU 路径均使用 YOLOv8n-pose，并共用本版 Input 的 RTSP 解码和纹理发布优化。要在同一应用切换 GPU 与 NPU，请使用 `android-dual-vulkan-npu` 构建并导入配套的本地资源。
Windows CPU 与 DirectML 均使用 YOLOv8n-pose，支持低、中、高三档。模型上游许可证及评估/分发标记保持原样。

## 配置、API 与 Settings

用 **HumanVision → Create Default SDK Configuration** 创建 `Assets/Resources/HumanVisionSdkConfiguration.asset`。也可从 Project 的 Create 菜单创建多个预设并绑定到不同场景。
可设置图像尺寸、计算模式、模型等级、人数、区域、镜像、置信度及结果时效。
**HumanVision → Create SDK** 创建[常驻 Prefab](user-guide/UNITY_RUNTIME_PREFAB.md)并绑定配置资产。Camera 名称、RTSP 地址和视频路径可通过
`InitializeFromPreset(configuration, kind, address)` 传入。示例见[统一 API](user-guide/SDK_CONFIGURATION.md)。

Android CPU 固定 512×288，人数 1–8。Android GPU 和装齐本地资源的 RK3588 NPU 支持
低档 512×288、中档 640×384、高档 960×576。
图像尺寸与模型尺寸独立。`Initialized` 在配置成功提交后触发，回调可读取 `ActiveConfiguration`。
在 Package Manager 的 SDK Samples 中导入 **SDK Settings** 或 **Runtime Prefab Acceptance**。
Settings 可切换输入并复制日志目录。Runtime Prefab 展示同帧图像、骨骼、实际模型尺寸、
Native 后端、SDK 新识别 FPS 和骨骼同帧 FPS。在 Editor 可通过验收组件的
**Editor Video Path** 配置本地视频，进入 Play Mode 后自动播放。
也可以用 **HumanVision → Create SDK settings demo assets** 创建 Settings 场景。
启动优先级为显式初始化参数、组件绑定资产、项目默认资产、组件内嵌参数。Settings 保存数据由 Settings 场景读取。

## 验证记录

一加 9 Pro 已验证 1080p RTSP 高清预览、512×288 识别输入及断流后切换到 720p 的同帧恢复。RK3588 的 NPU 后端、持续识别性能和业务动作响应仍待对应设备验收。见[验证记录](../upm/com.blazetc.humanvision/Documentation/reports/2026-11-18-preview16-rtsp-hd-preview.md)。
