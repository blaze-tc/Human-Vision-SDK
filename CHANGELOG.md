# Human Vision SDK 更新记录

## 0.4.1 · Input 0.1.1 · 2026-11-24

- Android Vulkan 原图桥在进入渲染前验证 Vulkan 实例、设备、队列、函数入口及 Unity 目标图像。设备重置或退出使旧代次失效，延迟回调保留必要资源直至完成。
- 独立 RTSP 关闭预览时跳过 Android 原图桥，预览故障从 `PreviewError` 查看。增加 RK3588 Neural/Low 启动 A/B/C 诊断，用预览开关和兼容输入路径定位故障阶段。
- **Runtime Prefab Acceptance** 使用场景中预先搭建的 UGUI。连接弹窗支持 RTSP 和本地相机，成功连接后保存。实时预览叠加骨骼由场景相机渲染，点和线可在 Scene 中选中。
- Demo 可一键采集日志并复制保存路径，日志底部显示设备可读的 CPU、GPU、NPU 和电池温度。

[本版验证与限制](docs/reports/2026-11-24-v041-release.md) · [安装与升级](docs/UPM_INSTALLATION.md)

## 0.4.0 · Input 0.1.0 · 2026-11-24

原生 RTSP 独立执行、可见原尺寸纹理、分析与预览分路、`Update` 直接取点和按需精确配对。[0.4.0 发布报告](docs/reports/2026-11-24-v040-release.md)
