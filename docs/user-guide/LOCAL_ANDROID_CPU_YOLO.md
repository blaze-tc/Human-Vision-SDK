# 0.4.1 正式版的本地 Android 配套

先安装 Git 标签 `v0.4.1` 的 Input **0.1.1**。公共 SDK **0.4.1** 采用 RKNN-OFF 构建，完整 Android CPU 或 RK3588 NPU 使用配套的私有替代 SDK。该替代包同为 `com.blazetc.humanvision` 0.4.1。

UPM 项目用完整 SDK 的 tgz 替换原 SDK 依赖，离线项目使用对应的完整 SDK unitypackage。保持 SDK 和 Input 各一份。根据本地交付清单核对 Input、SDK、ORT、vendor、模型及索引 SHA-256。需重新构建 AAR 和宿主 APK 的项目，还应核对最终 APK 内生效的 ARM64 库与 StreamingAssets。旧版本部署历史保留在下方，使用当前版本时应选匹配的 0.4.1 / 0.1.1 资源。

普通 RTSP 颜色 Texture 保持当前地址的可见原尺寸，分析图按模型准备。原生 RTSP 的执行方式使用 `ExecutionPolicy` 配置，业务在 `Update` 读取 SDK 有效关节。

## RK3588 启动排查

1. 在目标设备先跑只包含 SDK 验收场景的包。确认请求 Neural 和实际 `backend.rknn`，记录设备、图形 API、模型档位以及 SDK/Input/厂商库的 SHA-256 和 ELF BuildId。
2. 同一台设备、同一 RTSP 地址、相同模型与传输配置冷启动三组。A 为独立输入且启用预览，B 为独立输入且关闭预览，C 为兼容输入且启用预览。程序启动即退出时，从启动前采集 logcat，保存 crash buffer 和可读取的 tombstone。
3. A/C 核对持续原图、有效骨骼与重启。B 核对分析和骨骼持续，预览关闭时没有画面。若只有 B 正常，优先保留预览链路证据，从 `PreviewError` 和设备代次继续排查。用对照定位范围，根因仍看失败设备的 native backtrace。

仓库自带启动参数、命令模板、日志字段与证据清单。测试包和私有模型通过本地交付。一加 9 Pro 的 Vulkan GPU 数据对应独立 Demo 的 RTSP 路线，RK3588 启动和长时运行需在 RK3588 复测。[A/B/C 启动诊断](../../tools/test/NPU_STARTUP_DIAGNOSTICS.md) · [启动保护验证](../reports/2026-11-24-rk3588-npu-startup-regression.md)

## 历史版本部署记录

以下操作仅适用于各节列出的旧版 SDK/Input 及其原配套资源。0.4.1 项目使用上方的替代 SDK 安装步骤。

### preview.17 RK3588 NPU 三档本地资源

该版本使用 Git 标签 `v0.4.0-preview.17`，对应 Input `0.1.0-preview.10` 和 SDK `0.4.0-preview.17`。导入相应本地 RK3588 NPU 三档资源包后，运行 **HumanVision → Enable Local Android NPU Qualities**，核对实际加载的原生库和模型。

### preview.14 RK3588 NPU 三档本地资源

先安装 Git 标签 `v0.4.0-preview.14` 对应的 Input `0.1.0-preview.7` 与 SDK `0.4.0-preview.14`。导入匹配新版 Input 原生库的 `RK3588-NPU-Three-Qualities-Resources-preview.14.unitypackage`，运行 **HumanVision → Enable Local Android NPU Qualities**。保留项目的预设、运行配置和已校验的模型资源。确认工程只有一份有效的 Input、SDK 和 `librknnrt.so`。

在 `HumanVisionSdkOptions` 启用 `AutoSelectRtspStream` 和 `UseModelSizedAnalysisInput`，以 `1920×1080` 预览上限配合 RKNN 低档 `512×288`。完整参数见[配置与 API](SDK_CONFIGURATION.md)。

### preview.13 RK3588 NPU 三档本地包

从 preview.13 安装两个 Git UPM 子目录后，导入匹配的本地 `RK3588-NPU-Three-Qualities-Resources.unitypackage`，
执行 **HumanVision → Enable Local Android NPU Qualities**。已有三档安装可保留项目中的本地资源和运行库。
离线安装使用与主包及 Assets 布局配套的本地补充包，保持 SDK、Input 与原生库各一份。

在 SDK Settings 选择 **RK3588 NPU**，再选低档 512×288、中档 640×384 或高档 960×576 并应用。
代码使用 `HumanVisionAccelerationMode.Neural` 与相应的 `ModelInputQuality`。
三档使用非量化 RKNN，独立手部模型关闭，持续识别性能和动作响应由 RK3588 设备验收。

### preview.12 本地资源包

preview.8 首次接入 YOLOv8n-pose CPU，当时配套 Input 0.1.0-preview.5。下表记录后续 preview.12 本地包，配套 Input 0.1.0-preview.6。CPU 提供固定 512×288 档位，人数可设置 1–8。

| 平台/模式 | 该版模型和输入 | 部署 |
| --- | --- | --- |
| Android CPU | YOLOv8n-pose，ORT FP32，512×288，1–8 人 | 本次完整本地补充包 |
| Android Vulkan | 原 YOLOv8n-pose，低 512×288、中 640×384、高 960×576 | SDK 加完整本地补充包 |
| RK3588 NPU | 原低档 512×288 RKNN、原驱动库和输入内存资格检查 | 完整本地补充包；需要 RK3588 硬件 |
| Windows CPU/DirectML | 恢复版原模型与 Windows 原生库 | SDK 主包 |

先导入 Input 0.1.0-preview.6，再导入 SDK 0.4.0-preview.12，最后导入匹配的私有
`HumanVision-LocalAndroidCpuResources-0.4.0-preview.12.unitypackage`。补充包安装器会核对 SDK 和 Input 原生库。已有有效本地 Android 资源可保留，重新执行本地资源启用菜单时使用配套版本。
不要混装 Assets 和 UPM 两套 SDK。旧版本项目先备份，并用本次对应 SDK 替换主包。
执行 **HumanVision → Enable Local Android CPU Resources**：工具验证 52 个 Runtime 文件，
以及 SDK/Input/ORT/Windows/RKNN 原生库的准确哈希，成功后启用原 CPU/GPU/NPU 构建模式。
不要同时保留第二份 `librknnrt.so`。Android 保持 ARM64、IL2CPP、API 26+、Vulkan 优先。

设置界面仍分别选择计算模式与模型等级。CPU 为固定 512×288，等级控件不可选；
GPU 的低/中/高可选，NPU 仅低档。相机/RTSP 图像尺寸与模型尺寸互相独立。
四档显示尺寸、镜像和来源身份由原输入与坐标变换链路处理；CPU 模型内部采用 114 填充的 letterbox。

资源补充包中保留旧 CPU 的 TopDown/RTMO 文件，作为兼容和同安装包比较夹具。
正常 preview.8 Android CPU 选择只有新 YOLO 配置，旧夹具不会自动替换它。
模型校验拒绝错哈希、重复索引、损坏模型、链接逃逸、错误 CPU 配置和含混模型清单。

使用 `Initialized` 时，成功回调已经提交本次配置；读取 `ActiveConfiguration` 可以获得实际值。
`OpenInputOnInitialize=false` 为 Ready，打开输入成功为 Running，输入失败不发送成功事件。
坐标、数据借用与复制、世界平面映射及生命周期合同仍见 [统一 API](SDK_CONFIGURATION.md)。

本地补充包包含模型和 RKNN 库，仅作为本地评估资料；未上传公共 Release，分发资格标记保持 false。
新版 Android Input 的解码顺序、释放节奏和纹理读取保护由 Vulkan 与 NPU 共用。RK3588 实测时检查实际后端为 backend.rknn，并分段记录图像发布、异步读回、新识别与区域配对。
历史 CPU 路线的构建及实测见[验证报告](../reports/2026-10-14-main-cpu-yolo-api.md)。
