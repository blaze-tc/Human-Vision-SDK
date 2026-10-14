# Human Vision SDK

当前版本：SDK **0.4.0-preview.8** / Input **0.1.0-preview.5**，Unity **2021.3+**。
这是预发布评估版。Unity 游戏通过稳定 API 使用 Camera、RTSP、Video 图像，读取人体、
骨骼、人数和区域结果；默认配置使用 ScriptableObject，输入地址可在初始化代码中传入。

[下载 preview.8](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.8) ·
[安装](docs/UPM_INSTALLATION.md) · [配置与 API](docs/user-guide/SDK_CONFIGURATION.md) ·
[中文文档](docs/user-guide/README.md) · [本次验证与限制](docs/reports/2026-10-14-preview8-release.md)

## 安装

先安装 Input，再安装 SDK。Git UPM、tgz 和 unitypackage 选择一种安装方式，避免重复组件和原生库。

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.8
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.8
```

离线顺序：`HumanVisionInput-0.1.0-preview.5.unitypackage` →
`HumanVisionSDK-0.4.0-preview.8.unitypackage`。两个 tgz 为等价的 UPM 安装方式。
附件提供 `SHA256SUMS.txt`、资产索引和源提交记录。

**Android YOLO CPU 与 RK3588 NPU 的完整资源仍使用匹配的本地补充包**，安装方法见
[本地 Android 资源部署](docs/user-guide/LOCAL_ANDROID_CPU_YOLO.md)。公共附件不含该补充包、
CPU YOLO ONNX、RKNN 模型、`librknnrt.so`、用户视频或设备测试 APK。
Windows CPU/DirectML 与随包 Android Vulkan 路线保留原有资源。

## 最小接入

创建 `HumanVisionSdkPreset`：Project 右键 **Create → Human Vision → SDK Configuration**。
菜单 **HumanVision → Create SDK** 创建总控并绑定配置；或给空物体挂载 `HumanVisionSdk`。
在 `Start()` 中使用 `InitializeFromPreset(configuration, kind, address)` 初始化。
[完整初始化代码](docs/user-guide/SDK_CONFIGURATION.md) 包括输入源、结果回调、2D/世界平面坐标、
按 index/稳定身份取骨骼、区域占用、启动/停止/重启和配置更新。

官方样例保留 **HumanVisionSettingsDemo**。执行 **HumanVision → Create SDK settings demo assets**，
在同一界面切换输入源、计算模式、人数、区域和模型档位，复制日志文件夹路径。

## 模型和计算模式

| 模式 | 模型输入与支持范围 |
| --- | --- |
| Android CPU | YOLOv8n-pose FP32，512×288，1–8 人；需要本地补充包 |
| Android GPU / Vulkan | YOLOv8n-pose：低 512×288、中 640×384、高 960×576 |
| RK3588 NPU | 保留原低档 512×288 模型与驱动；需要完整本地补充包和设备支持 |
| Windows CPU / GPU | 保留原 CPU / DirectML 模型与 Windows x64 库 |

CPU 界面“固定模型”表示目前只提供一个模型输入档位，人数仍可调整。
图像输出分辨率与模型输入尺寸互相独立。缺模型、驱动或设备能力时会报出原因，不静默切换后端。

## 本次验证

修复初始化成功事件的配置提交顺序，并让 Android CPU 在公开人数/区域筛选前保留八个检测候选。
Unity/V1 API 保持兼容。原 Vulkan 三档、NPU 模型/驱动、Windows 库和 Input 未替换。

OnePlus（骁龙 888）同包截图短测：旧 CPU 新结果 **7.16 FPS**，新 CPU **14.23 FPS**，
Vulkan 低档 **24.60 FPS**；主要四人完整 COCO-17 骨骼分别为新 CPU **11.97–12.60**、
Vulkan **21.16–21.90 FPS**。每阶段仅 30 秒；RK3588 NPU 与长期稳定帧率尚未验收。
详细测试口径、失败/跳过项见[发布核验](docs/reports/2026-10-14-preview8-release.md)和
[实机报告](docs/reports/2026-10-14-main-cpu-yolo-api.md)。

世界坐标为图像到世界平面的映射；当前随包姿态模型未启用真实手掌/指尖/拇指推理。
保留模型来源、上游许可证和评估/分发资格标记；本次发布没有授予新的模型商用许可。

[维护入口](docs/maintenance/START_HERE.md) · [开发状态](docs/DEVELOPMENT_STATUS.md)
