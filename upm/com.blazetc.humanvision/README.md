# Human Vision SDK 0.4.0

SDK代码与接口正式版，依赖 Input 0.1.0。Unity/V1 API 保持兼容。
先安装 Input，再安装 SDK；UPM、tgz、unitypackage 选择一种方式。
[安装](UPM_INSTALLATION.md) · [配置与 API](Documentation/user-guide/SDK_CONFIGURATION.md) ·
[常驻 Runtime Prefab](Documentation/user-guide/UNITY_RUNTIME_PREFAB.md) ·
[Settings](Documentation/user-guide/SETTINGS_DEMO.md) · [本次验证](Documentation/reports/2026-11-24-v040-release.md)

Android CPU 使用固定 512×288 YOLOv8n-pose，人数 1–8；完整 CPU 资源需要匹配本地补充包。
Vulkan 和 RK3588 NPU 提供低、中、高档。RK3588 NPU 需要匹配的本地三档模型和运行库。
公共附件不含 CPU YOLO ONNX、私有 RKNN 模型、librknnrt.so、用户视频或设备测试 APK。
[本地 Android 资源部署](Documentation/user-guide/LOCAL_ANDROID_CPU_YOLO.md)说明如何启用完整测试资源。

在 HumanVision → Create Default SDK Configuration 创建项目级 ScriptableObject，挂载 HumanVisionSdk 或执行 HumanVision → Create SDK，
使用 InitializeFromPreset 在代码中设置 Camera/RTSP/Video 来源。Initialized 在配置提交后触发。
Package Manager 的 Samples 可导入 SDK Settings 或 Runtime Prefab Acceptance。

Runtime Prefab 使用右侧单个原图实时预览，叠加区域与每次Update读取的有效骨骼。精确同帧照片独立请求；普通RTSP颜色Texture不随模型尺寸缩小。原生RTSP可显式启用独立Runtime，输入、推理与完成发布自主推进。公共Android SDK不含私有RKNN厂商实现，CPU/NPU完整资源以匹配本地替代包提供。RK3588 设备性能与动作响应仍需验收。
世界坐标为图像到世界平面的映射，当前随包模型未启用真实手端点。
模型来源、许可证及 local_evaluation_only/distribution_qualified=false 标记保留。
