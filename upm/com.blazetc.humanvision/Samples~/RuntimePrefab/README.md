# Runtime Prefab
在 Package Manager 的 Human Vision SDK → Samples 导入 **Runtime Prefab**，打开 RuntimePrefabAcceptance.unity。
进入 Play Mode，填入运行机器可以访问的 RTSP 地址，点击连接。Android 使用 ARM64、IL2CPP、Vulkan，
并在 Player Settings 中设置 Internet Access 为 Require。电脑本机回环地址只指向运行设备自身。

左侧是最新输入。右侧默认显示游戏在 Update 中从公共接口读取的配对画面和 Unity 骨骼点，
并通过独立相机、世界空间 Canvas、Mesh 和 LineRenderer 重绘。可切换到 Prefab 合成进行对照。
Play Mode 的 Hierarchy 中展开 API Redraw - Scene Rig → Skeleton Objects，可以检查单个关节点和骨线。
RuntimeSkeletonReader.cs 展示按区域 index 读取关节，RuntimePrefabAcceptance.cs 展示 Update 调用。
Demo 会载入同项目 Settings 已保存且启用的识别区域。没有启用的设置时使用四个等宽区域。
右侧区域从 0 开始编号，空区域的取点返回 false，其他区域编号保持不变。
HUD 显示输入和新识别 FPS 的差、最长配对间隔及 Update 取数成功次数。

[完整接入说明](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.9/docs/user-guide/UNITY_RUNTIME_PREFAB.md)
