# Human Vision SDK 0.4.0-preview.8

预发布评估版，依赖 Input 0.1.0-preview.5。Unity/V1 API 保持兼容。
先安装 Input，再安装 SDK；UPM、tgz、unitypackage 选择一种方式。
[安装](UPM_INSTALLATION.md) · [配置与 API](Documentation/user-guide/SDK_CONFIGURATION.md) ·
[Settings](Documentation/user-guide/SETTINGS_DEMO.md) · [发布核验](Documentation/reports/2026-10-14-preview8-release.md)

Android CPU 使用固定 512×288 YOLOv8n-pose，人数 1–8；完整 CPU 资源需要匹配本地补充包。
Vulkan 保留低/中/高，RK3588 NPU 保留原低档模型和驱动，Windows 与 Input 保留原路线。
公共附件不含 CPU YOLO ONNX、私有 RKNN 模型、librknnrt.so、用户视频或设备测试 APK。
[本地 Android 资源部署](Documentation/user-guide/LOCAL_ANDROID_CPU_YOLO.md)说明如何启用完整测试资源。

创建 ScriptableObject 默认配置，挂载 HumanVisionSdk 或执行 HumanVision → Create SDK，
使用 InitializeFromPreset 在代码中设置 Camera/RTSP/Video 来源。Initialized 在配置提交后触发。
HumanVision → Create SDK settings demo assets 创建唯一官方 Settings 样例。

OnePlus 同包 30 秒短测：新 CPU 14.23、Vulkan 低档 24.60 个新结果/秒；
主要四人完整17点 CPU 11.97–12.60、Vulkan 21.16–21.90 FPS。
RK3588 NPU 和长时稳定性能未验收；不以显示/插值帧代替真实识别帧率。
世界坐标为图像到世界平面的映射，当前随包模型未启用真实手端点。
模型来源、许可证及 local_evaluation_only/distribution_qualified=false 标记保留。
