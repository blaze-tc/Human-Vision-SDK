# Human Vision SDK 安装与接入

SDK **0.4.0-preview.11** / Input **0.1.0-preview.6**，Unity **2021.3+**，Windows x64 / Android ARM64。
官方 Samples 包含 **SDK Settings** 和 **Runtime Prefab Acceptance**。源码和两个 Git UPM 子目录位于同一个 GitHub 仓库。

## 安装顺序

先安装 Input，等编译完成，再安装 SDK。Input 负责 Camera/Video/RTSP 采集与预览，
SDK 负责推理、骨骼、跟踪、人数与区域。选择 Git UPM、tgz 或 unitypackage 中的一种方式。
已有项目先备份配置，更换安装方式时移除原 SDK/Input 副本，避免重复脚本、GUID 与 native 库。

Git：Window → Package Manager → + → Add package from git URL，依次添加：

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.11
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.11
```

必须显式添加两个 Git 子目录；SDK 的版本依赖不能自动发现另一个 Git 包。
两包使用同一个发布标签；Input 的实际包版本仍为 0.1.0-preview.6。

离线：先导入 `HumanVisionInput-0.1.0-preview.6.unitypackage`，再导入
`HumanVisionSDK-0.4.0-preview.11.unitypackage`。每步等待导入与编译完成。
UPM tarball：依次安装 `com.blazetc.humanvision.input-0.1.0-preview.6.tgz` 和
`com.blazetc.humanvision-0.4.0-preview.11.tgz`。用附件 `SHA256SUMS.txt` 校验下载内容。

## Android 完整 CPU / NPU 资源

公共包提供 SDK、Input、Windows 原生依赖和原 Android Vulkan 资源。
**Android CPU YOLO ONNX、RKNN 模型和 librknnrt.so 不在公共附件中。**
本地完整测试需要继续导入匹配的
`HumanVision-LocalAndroidCpuResources-0.4.0-preview.8.unitypackage`，然后执行
**HumanVision → Enable Local Android CPU Resources**。工具验证 52 项 Runtime 与原生库哈希。
详细步骤及导出工具见[本地资源部署](user-guide/LOCAL_ANDROID_CPU_YOLO.md)。
该补充包、含完整资源的 APK 和用户视频保存在本地测试交付目录，不作为公共 Release 附件。
缺少资源时初始化明确失败；选择 CPU/GPU/NPU 不代表对应资源自动下载或设备已经支持。

Android：ARM64、IL2CPP、API 26+；Vulkan 模式需要兼容的 GPU。RK3588 NPU 需要匹配设备和驱动。
Windows 保留原 CPU/DirectML 路线。模型上游许可证及评估/分发标记保持原样。

## 配置、API 与 Settings

用 **HumanVision → Create Default SDK Configuration** 创建 `Assets/Resources/HumanVisionSdkConfiguration.asset`。也可从 Project 的 Create 菜单创建多个预设并绑定到不同场景。
可设置图像尺寸、计算模式、模型等级、人数、区域、镜像、置信度及结果时效。
**HumanVision → Create SDK** 创建[常驻 Prefab](user-guide/UNITY_RUNTIME_PREFAB.md)并绑定配置资产。Camera 名称、RTSP 地址和视频路径可通过
`InitializeFromPreset(configuration, kind, address)` 传入。示例见[统一 API](user-guide/SDK_CONFIGURATION.md)。

Android CPU 目前固定 512×288，人数 1–8；GPU 保留低/中/高，RK3588 NPU 保留低档。
图像尺寸与模型尺寸独立。`Initialized` 在配置成功提交后触发，回调可读取 `ActiveConfiguration`。
在 Package Manager 的 SDK Samples 中导入 **SDK Settings** 或 **Runtime Prefab Acceptance**。
Settings 可切换输入并复制日志目录。Runtime Prefab 展示同帧图像、骨骼、游戏 Update 读取和性能指标。
也可以用 **HumanVision → Create SDK settings demo assets** 创建 Settings 场景。
启动优先级为显式初始化参数、组件绑定资产、项目默认资产、组件内嵌参数。Settings 保存数据由 Settings 场景读取。

## 验证记录

Editor 和 OnePlus 9 Pro 的本次验证见[开发状态](DEVELOPMENT_STATUS.md)。区域坐标来自画面平面，RK3588 NPU 的设备表现单独验收。
