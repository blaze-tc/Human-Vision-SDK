# Runtime Prefab Acceptance

在 Package Manager 的 Human Vision SDK → Samples 选择导入 **Runtime Prefab Acceptance**，打开 `RuntimePrefabAcceptance.unity`。
进入 Play Mode，填写设备可访问的 RTSP 地址后点击连接。也可给样例绑定 Startup Preset，
或通过 **HumanVision → Create Default SDK Configuration** 配置项目启动输入。
预设可选摄像头、RTSP 和视频。Android 使用 ARM64、IL2CPP、Vulkan，Internet Access 设为 Require。
电脑本机回环地址只指向运行设备自身。

左侧显示最新输入和识别区域，连接前可核对布局。右侧也使用最新相机画面，采样骨骼作为独立图层叠加。
关闭骨骼时视频继续更新。同一路输入短时缺少新采样时，骨骼最多保留 180 毫秒。识别到无人、切换输入或重连时立即清除。
点右侧「全屏预览」可按输入比例查看最新画面。右上角可切换等比与填满裁切、开关骨骼或退出全屏。
RuntimePrefabLiveSkeleton.cs 演示实时叠加，RuntimePrefabAcceptancePreview.cs 演示精确拍照。
样例启动时优先读取绑定预设和项目默认资产。没有预设时可读取 Settings 已保存的区域，默认四个等宽区域。
从「区域数量」下拉框选择 1–8 人。更改人数后，当前会话按所选人数生成等宽区域。
左侧区域从 0 开始编号，空区域的取点返回 false，其他区域编号保持不变。
HUD 显示游戏、输入、新识别帧率和 SDK 各阶段耗时。主动获取精确帧时也记录配对数据。
CPU、GPU、NPU 和模型等级以平台及安装资源为准。NPU 需要 RK3588 双模式构建及本地补充包。

RTSP 连接启用同通道宇视码流探测，优先用 `1920×1080` 以内分辨率最高的 H.264 码流。
仅有 4K 时仍可连接，Unity 预览等比限制在 `1920×1080` 以内。Android GPU/NPU 低档通过独立分路送入 `512×288`。
左侧预览下方显示所选码流与解码或探测尺寸，状态行单独显示识别输入帧率。无预设时，Android 样例以 GPU 低档启动，安装匹配本地资源后可切换 RK3588 NPU。

Android 分路模式下，实时预览来自相机分路，最高 `1920×1080`。低档识别继续使用独立的 `512×288` 输入。
样例的 `PresentationMode` 平时为 `Disabled`。点击「拍照（精确同帧）」时临时切到 `MatchedColor`，等待当前输入代次的新鲜配对，将原色帧以 PNG 保存到应用的 `runtime-photos` 目录，再恢复实时模式。拍照需要图形栅栏和异步 GPU 回读支持。
需要持续精确合成的界面可选择 `MatchedOverlay`。业务动作仍应消费新结果。

「一键测试」连接当前输入并在首个有效推理结果后记录 180 秒。结果包括独立的解码、识别、有效骨骼和预览统计。
「导出 JSONL / 复制路径」保存日志路径。Android 分路日志记录可用的 GPU 快照、归还和 CPU 回读数据。

[完整接入说明](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.16/docs/user-guide/UNITY_RUNTIME_PREFAB.md)
