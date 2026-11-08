# Human Vision SDK 0.4.0-preview.12

预发布评估版，依赖 Input 0.1.0-preview.6。Unity/V1 API 保持兼容。
先安装 Input，再安装 SDK；UPM、tgz、unitypackage 选择一种方式。
[安装](UPM_INSTALLATION.md) · [配置与 API](Documentation/user-guide/SDK_CONFIGURATION.md) ·
[常驻 Runtime Prefab](Documentation/user-guide/UNITY_RUNTIME_PREFAB.md) ·
[Settings](Documentation/user-guide/SETTINGS_DEMO.md) · [preview.12 Windows YOLO 验证](Documentation/reports/2026-11-08-preview12-windows-yolo.md)

Android CPU 使用固定 512×288 YOLOv8n-pose，人数 1–8；完整 CPU 资源需要匹配本地补充包。
Vulkan 保留低/中/高，RK3588 NPU 保留原低档模型和驱动，Windows 与 Input 保留原路线。
公共附件不含 CPU YOLO ONNX、私有 RKNN 模型、librknnrt.so、用户视频或设备测试 APK。
[本地 Android 资源部署](Documentation/user-guide/LOCAL_ANDROID_CPU_YOLO.md)说明如何启用完整测试资源。

在 HumanVision → Create Default SDK Configuration 创建项目级 ScriptableObject，挂载 HumanVisionSdk 或执行 HumanVision → Create SDK，
使用 InitializeFromPreset 在代码中设置 Camera/RTSP/Video 来源。Initialized 在配置提交后触发。
Package Manager 的 Samples 可导入 SDK Settings 或 Runtime Prefab Acceptance。

OnePlus 9 Pro 的 Exercise2 RTSP 短测，输入发布及新识别均约 14.5 FPS，游戏更新约 54.3 FPS。
RK3588 NPU 和长时稳定性能未验收；不以显示/插值帧代替真实识别帧率。
世界坐标为图像到世界平面的映射，当前随包模型未启用真实手端点。
模型来源、许可证及 local_evaluation_only/distribution_qualified=false 标记保留。
