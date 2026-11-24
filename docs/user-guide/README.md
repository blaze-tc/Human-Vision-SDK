> 当前版本使用 ScriptableObject 默认配置、代码指定输入源及统一查询 API，请先阅读 [当前接入说明](SDK_CONFIGURATION.md)。下文含已发布版本和兼容 API 的历史说明。

# Human Vision SDK 中文使用文档

适用 SDK **0.4.1** / Input **0.1.1**。更新日期 2026-11-24。[版本下载](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.1)

这套文档面向在**新创建的 Unity 项目**中第一次使用 SDK 的开发者。无需先了解模型或原生推理；先学会Start初始化、输入图像和骨骼结果读取，再按需要查接口。

| 文档 | 主要内容 |
| --- | --- |
| [单组件 Unity 使用](UNITY_SDK.md) | 挂载总控、初始化、区域与骨骼查询 |
| [UGUI 设置 Demo](SETTINGS_DEMO.md) | 参考 Setting 界面、应用/保存、输入与区域编辑 |
| [Runtime Prefab Acceptance](UNITY_RUNTIME_PREFAB.md) | 连接 RTSP 或本地相机、原图、场景骨骼、直接取点和日志采集 |
| [RK3588 联合优化自动对照](RK3588_COMBINED_COMPARISON.md) | 私有测试包的一次安装对照、逐人帧率、日志文件夹与验收边界 |
| [第一次安装使用引导](FIRST_INSTALL.md) | 安装双包/模型、void Start初始化、摄像头输入、骨骼回调读取、停止释放；可选Hierarchy右键创建菜单 |
| [技术栈文档](TECH_STACK.md) | Unity/Input/Runtime/插件/后端/模型分层、异步数据流和生命周期 |
| [平台测试效果文档](PLATFORM_TEST_RESULTS.md) | SDK发布验证与历史真机测量、数字的含义、平台覆盖与复测方法 |
| [API调用与说明文档](API_REFERENCE.md) | SDK接口的作用、参数、默认值、返回值、关节/帧/区域数据含义和调用示例 |

第一次使用按[代码入门引导](FIRST_INSTALL.md)完成安装，然后只需一个空物体和一份脚本。主要示例：

- [SdkBasicUsage.cs](examples/SdkBasicUsage.cs)：无需UI引用；Start中启动初始化，打开相机，在ResultUpdated中读取人物ID与左腕，提供StopSdk关闭协程。
- [SdkBasicUsageMenu.cs](examples/Editor/SdkBasicUsageMenu.cs)：可选编辑器脚本，复制到Assets/Editor后提供Hierarchy右键创建启动物体菜单。

需要显示或打包时再看[可选示例界面与平台构建](DISPLAY_AND_BUILD.md)，其中的[SdkCameraQuickStart.cs](examples/SdkCameraQuickStart.cs)和[SdkSkeletonReader.cs](examples/SdkSkeletonReader.cs)用于预览/Overlay与独立结果消费；它们不是初始化和取数据的必做步骤。

两个 Git UPM 子目录使用同一个标签 `v0.4.1`。API 表中的类以包源码为准。

当前随包Profile关闭真实Hand/Handtip/Thumb推理，32个语义槽位不代表全部为有效观察。模型保留评估/分发资格标记，具体见[第三方说明](../../upm/com.blazetc.humanvision/THIRD_PARTY_NOTICES.md)。历史测量与本次文档检查分开记录，不把安装成功或显示FPS当作持续识别性能。

本次安装和设备验证见[0.4.1 发布报告](../reports/2026-11-24-v041-release.md)，Android CPU/NPU 资源和启动排查见[本地部署](LOCAL_ANDROID_CPU_YOLO.md)。历史版本见[0.4.0 发布报告](../reports/2026-11-24-v040-release.md)及 [SDK 用户文档验证](../reports/2026-10-07-sdk-user-docs-verification.md)。
