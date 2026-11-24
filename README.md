# Human Vision SDK

当前版本为 SDK **0.4.1** 和 Input **0.1.1**，支持 Unity **2021.3+**。
这是 SDK 代码与接口正式版。Unity 游戏通过稳定 API 使用 Camera、RTSP、Video 图像，读取人体、
骨骼、人数和区域结果；默认配置使用 ScriptableObject，输入地址可在初始化代码中传入。

[下载 v0.4.1](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.1) ·
[安装](docs/UPM_INSTALLATION.md) · [配置与 API](docs/user-guide/SDK_CONFIGURATION.md) ·
[中文文档](docs/user-guide/README.md) · [版本更新](CHANGELOG.md)

## 安装

先安装 Input，再安装 SDK。Git UPM、tgz 和 unitypackage 选择一种安装方式，避免重复组件和原生库。

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.1
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.1
```

离线先导入 `HumanVisionInput-0.1.1.unitypackage`，再导入
`HumanVisionSDK-0.4.1.unitypackage`。也可用两个 tgz 安装到 UPM。
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
Runtime Prefab 验收场景通过 UGUI 的「输入连接」选择 RTSP 或本地相机。右侧原图叠加区域和每次 `Update` 读取的骨骼，左侧可设置后端和模型、采集日志并查看可读温度。界面、骨骼相机及可选中的点线节点已保存在场景中。
[常驻 Prefab 接入](docs/user-guide/UNITY_RUNTIME_PREFAB.md)。

## 模型和计算模式

| 模式 | 模型输入与支持范围 |
| --- | --- |
| Android CPU | YOLOv8n-pose FP32，512×288，1–8 人；需要本地补充包 |
| Android GPU / Vulkan | YOLOv8n-pose：低 512×288、中 640×384、高 960×576 |
| RK3588 NPU | 低 512×288、中 640×384、高 960×576，需要匹配的本地模型、运行库和设备支持 |
| Windows CPU / DirectML | YOLOv8n-pose，低 512×288、中 640×384、高 960×576 |

Android CPU 界面“固定模型”表示目前只提供一个模型输入档位，人数仍可调整。
图像输出分辨率与模型输入尺寸互相独立。缺模型、驱动或设备能力时会报出原因，不静默切换后端。

## 本次验证

原生 RTSP 可显式使用独立 Runtime，CPU/GPU/NPU 共用异步调度与完成驱动发布。预览与识别从同一个 decoder 独立输出，普通颜色 Texture 保持解码后的可见原尺寸。VideoPlayer 和 WebCamTexture 使用 Unity 兼容入口。

0.4.1 加强 Android Vulkan 原图桥对启动及图形设备重置的保护，可用独立演示程序对照预览开关和输入执行方式。一加 9 Pro 实测覆盖 1080p30 RTSP 和 Vulkan GPU 低档。RK3588 NPU 的现场启动故障和持续性能仍需目标设备验证。[本次变更与设备结果](docs/reports/2026-11-24-v041-release.md)

世界坐标为图像到世界平面的映射；当前随包姿态模型未启用真实手掌/指尖/拇指推理。
保留模型来源、上游许可证和评估/分发资格标记；本次发布没有授予新的模型商用许可。

[维护入口](docs/maintenance/START_HERE.md) · [开发状态](docs/DEVELOPMENT_STATUS.md)
