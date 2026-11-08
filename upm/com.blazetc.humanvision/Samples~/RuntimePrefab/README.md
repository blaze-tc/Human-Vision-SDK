# Runtime Prefab Acceptance

在 Package Manager 的 Human Vision SDK → Samples 选择导入 **Runtime Prefab Acceptance**，打开 `RuntimePrefabAcceptance.unity`。
进入 Play Mode，填写设备可访问的 RTSP 地址后点击连接。也可给样例绑定 Startup Preset，
或通过 **HumanVision → Create Default SDK Configuration** 配置项目启动输入。
预设可选摄像头、RTSP 和视频。Android 使用 ARM64、IL2CPP、Vulkan，Internet Access 设为 Require。
电脑本机回环地址只指向运行设备自身。

左侧是最新输入和识别区域，连接前可核对布局。右侧默认显示游戏在 Update 中从公共接口读取的配对画面和 Unity 骨骼点，
并通过独立相机、世界空间 Canvas、Mesh 和 LineRenderer 重绘。可切换到 Prefab 合成进行对照。
Play Mode 的 Hierarchy 中展开 API Redraw - Scene Rig → Skeleton Objects，可以检查单个关节点和骨线。
RuntimeSkeletonReader.cs 展示按区域 index 读取关节，RuntimePrefabAcceptance.cs 展示 Update 调用。
样例启动时优先读取绑定预设和项目默认资产。没有预设时可读取 Settings 已保存的区域，默认四个等宽区域。
左侧区域从 0 开始编号，空区域的取点返回 false，其他区域编号保持不变。
HUD 显示游戏、输入、新识别、配对帧率、最长配对间隔、Update 取数成功次数及 SDK 各阶段耗时。
CPU、GPU、NPU 和模型等级以平台及安装资源为准。NPU 需要 RK3588 双模式构建及本地补充包。

[完整接入说明](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.11/docs/user-guide/UNITY_RUNTIME_PREFAB.md)
