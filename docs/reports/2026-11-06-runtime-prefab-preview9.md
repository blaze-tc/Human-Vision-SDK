# 2026-11-06 常驻 Unity Runtime Prefab 与示例

SDK 0.4.0-preview.9–preview.10 / Input 0.1.0-preview.5。本次新增常驻 SDK Prefab、Runtime Prefab 样例和配套文档。输入包、模型、原生库及 ABI 沿用 preview.8。

## 实际验证

- Windows Unity 2021.3.45f1，D3D11，Windows CPU profile。使用本机正在运行的 RTSP 服务 `rtsp://127.0.0.1:554/Exercise2.mp4`；ffprobe 读到 H.264 1920×1080，SDK 当前配置的输入发布尺寸为 1280×720。
- 在隔离 Unity 项目从最终两个 tgz 安装；PlayMode 2/2。验证跨场景存活、重复创建去重、相机不参加常规渲染，以及实际 RTSP 配对、稳定 ID、关节 Transform/视口/绿色覆盖像素对应、背景像素保留、停止后不可查询、重启产生新来源、等待卸载后销毁。该次连续 25/25 个已接受识别结果配对成功；最后一次采样输入发布 ID 59、原生提交 ID 44。该比例只针对这次采样，分母不是所有解码帧。
- EditMode 94/94：SDK 生命周期、关节查询、输入适配器、历史姿态、旧骨骼绘制与配置。无图形启动时旧骨骼绘制测试的 `Camera.Render` 在 Unity 原生侧崩溃，改用 D3D11 后全部通过。首轮 RTSP 检查发现 DX11 复制完成栅栏迟迟不返回以及离屏标记 Y 轴反向，分别用有序 1×1 异步读回和正确的投影方向修复。
- `py -3.13 tools/maintenance/check_architecture_boundaries.py` 通过（含 V1 公共边界）；`pwsh -NoProfile -File tools/test/verify_unity_sdk.ps1 -Package -CompileOnly` 的包内 Editor、Android 托管编译通过。不带 `-Package` 的历史 Demo 编译有基线问题：`HumanVisionAndroidGpuGateBuild` 引用了未定义的 `HumanVisionCameraDemoBuilder.CreateGateScene`，本次未改动该诊断工具。
- 发布包用 `tools/package/release-preview9-authority.json` 固定 SDK/Input 完整闭包，验证原生库、RuntimeData、场景 GUID、两个 tgz 和两个 unitypackage 的 SHA-256。

可复跑源码包 PlayMode：
```powershell
$env:HV_RUNTIME_VIEW_RTSP = 'rtsp://127.0.0.1:554/Exercise2.mp4'
pwsh -NoProfile -File tools/test/verify_unity_sdk.ps1 -Package -Graphics -TestMode PlayMode -Filter HumanVision.Tests.HumanVisionRuntimeViewTests
```

- Unity 2021.3.45f1 Package Manager 实际导入两个 Samples。Runtime Prefab 读取保存的三个自定义区域，使用 1280×720 RTSP 验证区域 2 可取点，空区域 0 无骨骼。

## Android 真机短测

本地 RuntimePrefabAcceptance APK 在 OnePlus 9 Pro（Android 14）运行，使用 Exercise2 RTSP、Vulkan 低档和
1280×720 输入。33 个约一秒的窗口内，输入发布 FPS 与新识别 FPS 的平均值均为 14.49。游戏平均 54.33 FPS，
SDK 上报平均 46.82 ms，Demo 读取和提交绘制的平均 CPU 耗时为 0.326 ms。
1,826 次 Update 均读取到配对纹理和有效关节，共 486 个不同配对。33 个窗口末尾的输入与推理来源帧 ID 一致，
一次 5 点背景像素对照差值为零。真机观察中骨骼跟随正常，短测开始和结束的电池温度均为 32.8°C。
这组数据来自本地验收 APK。RK3588 和长期热机仍待设备验证。

公开 Runtime Prefab 样例默认等待输入地址。通过 Package Manager Samples 导入后，在 Play Mode 填入 RTSP URL，
可看输入发布、新识别、两者差值、最长配对间隔和 Update 取数统计。
Demo 载入 Settings 中保存并启用的区域，左侧按零起始 index 标记输入区域，右侧以同一 index 取骨骼。preview.10 将区域布局显示在输入侧。OnePlus 的数字来自加入区域示范前的本地验收 APK。公开包保持已有模型评估与分发标记。
接入见[常驻 SDK Prefab](../user-guide/UNITY_RUNTIME_PREFAB.md)。
