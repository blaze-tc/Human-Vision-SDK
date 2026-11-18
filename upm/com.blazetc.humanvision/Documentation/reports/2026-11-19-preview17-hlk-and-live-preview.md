# Human Vision SDK 0.4.0-preview.17

SDK `0.4.0-preview.17` 与 Input `0.1.0-preview.10` 使用同一 Git 标签。按 Input、SDK 顺序安装。

## Android RTSP 与 Runtime Prefab

- HLK-AL00 上的 Android 10 海思 AVC 输入采用设备专用的 AImageReader 容量。原始 1920×1080 H.264 Main 码流在同机同解码器对照中恢复输出；其他设备按原有配置创建读者。输入仍以来源、帧号、时间戳和重连代次记录。
- Runtime Prefab Acceptance 用最新相机帧显示实时预览，采样骨骼独立绘制。点击精确拍照时等待当前来源代次的同帧原色图。SDK 可选择 `MatchedOverlay`、`MatchedColor` 或 `Disabled`。既有精确合成调用方可以继续使用默认的 `MatchedOverlay`。
- CPU、Vulkan 和 RK3588 支持匹配本地资源的 `UltraLow` 实验模型。RTSP 识别传输为 512×288，模型输入为 384×224，设备验收中实时相机预览为 1920×1080。超低档七人抬腕样本只通过 1/7。公开包包含已有 Vulkan 档位，额外模型使用匹配此版本的本地资源。

## 设备记录

同一验收 APK 在 HLK-AL00 和一加 9 Pro 使用原始 1080p Exercise-11。每组统计首个就绪结果起 30 秒内的完整窗口。

| 设备 | CPU 低档 → 超低档新结果 FPS | Vulkan 低档 → 超低档新结果 FPS |
| --- | ---: | ---: |
| HLK-AL00 | 8.73 → 13.54 | 8.21 → 12.36 |
| 一加 9 Pro | 18.00 → 24.51 | 21.52 → 24.84 |

八组记录实际后端、分析提交尺寸和 1920×1080 实时预览。HLK CPU 超低另验收 1920×1080 精确同帧照片。一加完成界面切模型、切后端及 JSONL 导出。实验 APK 与设备日志为本地证据。RK3588 超低档完成模拟器原始输出检查，实机吞吐和业务动作待对应设备验收。

YOLO26 为研究对照，当前发布的 Android 识别模型使用已配置的 YOLOv8 资产。局域网演示地址由项目的场景或启动预设填写。

## 发布构建检查

- Windows SDK 原生回归 475/475，独立 Input 回归 57/57。Android Input 重建结果与包内 ARM64 文件一致。
- Android SDK 与 Input 通过 API 26 依赖审计。公共包验证 8 个 Android 原生库、9 个 Windows 原生库和两个 UPM 文件与 GUID 闭包。
- Unity 2021.3.45f1 的 Editor、Android 托管编译和 Runtime Prefab 样例编译通过。

- [安装和升级](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.17/docs/UPM_INSTALLATION.md)
- [同机首帧对照](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.17/docs/reports/2026-11-18-hlk-al00-rtsp-first-frame.md)
- [超低档实机数据](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.17/docs/reports/2026-11-18-hlk-ultra-low-384x224.md)
