# Human Vision SDK

当前版本：SDK **0.4.0-preview.12** / Input **0.1.0-preview.6**，Unity **2021.3+**。
这是预发布评估版。Unity 游戏通过稳定 API 使用 Camera、RTSP、Video 图像，读取人体、
骨骼、人数和区域结果；默认配置使用 ScriptableObject，输入地址可在初始化代码中传入。

[下载 preview.12](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.12) ·
[安装](docs/UPM_INSTALLATION.md) · [配置与 API](docs/user-guide/SDK_CONFIGURATION.md) ·
[中文文档](docs/user-guide/README.md) · [本次验证与录屏](docs/reports/2026-11-08-preview12-windows-yolo.md)

## 安装

先安装 Input，再安装 SDK。Git UPM、tgz 和 unitypackage 选择一种安装方式，避免重复组件和原生库。

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.12
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.12
```

离线顺序：`HumanVisionInput-0.1.0-preview.6.unitypackage` →
`HumanVisionSDK-0.4.0-preview.12.unitypackage`。两个 tgz 为等价的 UPM 安装方式。
附件提供 `SHA256SUMS.txt`、资产索引和源提交记录。

**Android YOLO CPU 与 RK3588 NPU 的完整资源仍使用匹配的本地补充包**，安装方法见
[本地 Android 资源部署](docs/user-guide/LOCAL_ANDROID_CPU_YOLO.md)。公共附件不含该补充包、
Android CPU 独立 ONNX 权重、RKNN 模型、`librknnrt.so`、用户视频或设备测试 APK。
Windows CPU 与 DirectML 直接复用随包 YOLOv8n-pose 权重。

## 最小接入

用 **HumanVision → Create Default SDK Configuration** 创建项目启动资产。它保存在 `Assets/Resources/HumanVisionSdkConfiguration.asset`。
菜单 **HumanVision → Create SDK** 创建总控并绑定配置；或给空物体挂载 `HumanVisionSdk`。
直接调用 `Initialize()` 读取默认参数。游戏也可通过 `InitializeFromPreset(configuration, kind, address)` 指定本次输入。
[完整初始化代码](docs/user-guide/SDK_CONFIGURATION.md) 包括输入源、结果回调、2D/世界平面坐标、
按 index/稳定身份取骨骼、区域占用、启动/停止/重启和配置更新。

从 Package Manager 的 **Samples** 导入 **SDK Settings** 或 **Runtime Prefab Acceptance**。
Runtime Prefab 样例展示实时预览、同帧图像与骨骼以及游戏在 Update 中取数的方式。
[常驻 Prefab 接入](docs/user-guide/UNITY_RUNTIME_PREFAB.md)。

## 模型和计算模式

| 模式 | 模型输入与支持范围 |
| --- | --- |
| Android CPU | YOLOv8n-pose FP32，512×288，1–8 人；需要本地补充包 |
| Android GPU / Vulkan | YOLOv8n-pose：低 512×288、中 640×384、高 960×576 |
| RK3588 NPU | 保留原低档 512×288 模型与驱动；需要完整本地补充包和设备支持 |
| Windows CPU / DirectML | YOLOv8n-pose，低 512×288、中 640×384、高 960×576 |

Android CPU 界面“固定模型”表示目前只提供一个模型输入档位，人数仍可调整。
图像输出分辨率与模型输入尺寸互相独立。缺模型、驱动或设备能力时会报出原因，不静默切换后端。

## 本次验证

Windows 六档组合和 OnePlus 9 Pro 均以 DancingTest 验证实际模型与同帧读取。数据和录屏见[验证报告](docs/reports/2026-11-08-preview12-windows-yolo.md)。RK3588 NPU 的帧率与热机效果由设备验收。

世界坐标为图像到世界平面的映射；当前随包姿态模型未启用真实手掌/指尖/拇指推理。
保留模型来源、上游许可证和评估/分发资格标记；本次发布没有授予新的模型商用许可。

[维护入口](docs/maintenance/START_HERE.md) · [开发状态](docs/DEVELOPMENT_STATUS.md)
