# Human Vision SDK 安装与接入

SDK **0.4.1** / Input **0.1.1**，Unity **2021.3+**，Windows x64 / Android ARM64。
官方 Samples 包含 **SDK Settings** 和 **Runtime Prefab Acceptance**。源码和两个 Git UPM 子目录位于同一个 GitHub 仓库。

## 安装顺序

先安装 Input，等编译完成，再安装 SDK。Input 负责 Camera/Video/RTSP 采集与预览，
SDK 负责推理、骨骼、跟踪、人数与区域。选择 Git UPM、tgz 或 unitypackage 中的一种方式。
已有项目先备份配置，更换安装方式时移除原 SDK/Input 副本，避免重复脚本、GUID 与 native 库。
已有内嵌包的项目在 Unity 关闭后更新 `Packages/com.blazetc.humanvision` 至 0.4.1，同时更新 Input 至 0.1.1，并保留项目配置。Android Vulkan 原图桥修复位于 Input 原生库，两包须成套更新。重新打开 Unity 后，SDK 自动安装桌面 YOLO 配置和图结构。

Git：Window → Package Manager → + → Add package from git URL，依次添加：

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.1
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.1
```

必须显式添加两个 Git 子目录；SDK 的版本依赖不能自动发现另一个 Git 包。
两包使用同一个发布标签。Input 的实际包版本为 0.1.1。

离线先导入 `HumanVisionInput-0.1.1.unitypackage`，再导入
`HumanVisionSDK-0.4.1.unitypackage`。每步等待导入与编译完成。
离线包将随附资源导入 `Assets/HumanVision/RuntimeData`。Unity 会自动更新运行目录，保留通过校验的本地 Android 资源及其索引。也可通过 **HumanVision → Install Packaged Models** 手动重试。
UPM tarball 依次安装 `com.blazetc.humanvision.input-0.1.1.tgz` 和
`com.blazetc.humanvision-0.4.1.tgz`。用附件 `SHA256SUMS.txt` 校验下载内容。

## Android 完整 CPU / NPU 资源

完整本地 SDK 提供 RK3588 NPU 低、中、高三档模型配置。公共 SDK 的 Android 库采用 RKNN-OFF 构建。已安装本地三档资源的项目升级时，备份项目里的资源和构建设置，按本地交付清单核对 Input、SDK 和模型哈希。
Git UPM 项目接入完整 Android CPU 或 NPU 时，先安装 Input 0.1.1，再用匹配的本地 SDK 0.4.1 替代公共 SDK。安装和配置的具体步骤以本地交付清单为准。
本地 SDK 与公共 SDK 使用相同 package id 和 GUID，应替换原 SDK 副本，不能在 UPM SDK 旁再导入一份 Assets SDK 或原生库。使用离线包的项目使用对应布局的本地替代 SDK。若仍在使用 preview.12 的独立 CPU/NPU 补充包，按该版本的配套说明安装。
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
Settings 可切换输入并复制日志目录。Runtime Prefab 的「输入连接」在 RTSP 和本地相机之间切换。左侧控制区和日志显示源解码、Texture 更新、送入识别、识别完成、有效骨骼五项 FPS，支持一键采集日志。右侧一个实时原图预览叠加区域和每次 `Update` 读取的骨骼，精确同帧照片按需请求。场景已配好 UGUI、骨骼相机和点线节点。[验收场景操作说明](user-guide/UNITY_RUNTIME_PREFAB.md)
也可以用 **HumanVision → Create SDK settings demo assets** 创建 Settings 场景。
启动优先级为显式初始化参数、组件绑定资产、项目默认资产、组件内嵌参数。Settings 保存数据由 Settings 场景读取。

## 验证记录

一加 9 Pro 的 1080p30 RTSP 测试使用 Vulkan GPU 低档，源解码 29.99 FPS，Unity 纹理更新 23.81 FPS，有效骨骼交付 26.56 FPS。RK3588 NPU 启动、持续识别性能和业务联调仍待目标设备验收。[测量方法与变更说明](reports/2026-11-24-v041-release.md)
