> 当前版本使用 ScriptableObject 默认配置、代码指定输入源及统一查询 API，请先阅读 [当前接入说明](SDK_CONFIGURATION.md)。下文含已发布版本和兼容 API 的历史说明。

# Human Vision SDK 中文使用文档

适用版本：SDK **0.4.0-preview.8** / Input **0.1.0-preview.5**。更新日期：2026-10-14。[版本下载](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.8)

这套文档面向在**新创建的 Unity 项目**中第一次使用 SDK 的开发者。无需先了解模型或原生推理；先学会Start初始化、输入图像和骨骼结果读取，再按需要查接口。

| 文档 | 主要内容 |
| --- | --- |
| [单组件 Unity 使用](UNITY_SDK.md) | 挂载总控、初始化、区域与骨骼查询 |
| [UGUI 设置 Demo](SETTINGS_DEMO.md) | 参考 Setting 界面、应用/保存、输入与区域编辑 |
| [RK3588 联合优化自动对照](RK3588_COMBINED_COMPARISON.md) | 私有测试包的一次安装对照、逐人帧率、日志文件夹与验收边界 |
| [第一次安装使用引导](FIRST_INSTALL.md) | 安装双包/模型、void Start初始化、摄像头输入、骨骼回调读取、停止释放；可选Hierarchy右键创建菜单 |
| [技术栈文档](TECH_STACK.md) | Unity/Input/Runtime/插件/后端/模型分层、异步数据流和生命周期 |
| [平台测试效果文档](PLATFORM_TEST_RESULTS.md) | SDK发布验证与历史真机测量、数字的含义、平台覆盖与复测方法 |
| [API调用与说明文档](API_REFERENCE.md) | SDK接口的作用、参数、默认值、返回值、关节/帧/区域数据含义和调用示例 |

第一次使用按[代码入门引导](FIRST_INSTALL.md)完成安装，然后只需一个空物体和一份脚本。主要示例：

- [SdkBasicUsage.cs](examples/SdkBasicUsage.cs.txt)：无需UI引用；Start中启动初始化，打开相机，在ResultUpdated中读取人物ID与左腕，提供StopSdk关闭协程。
- [SdkBasicUsageMenu.cs](examples/Editor/SdkBasicUsageMenu.cs.txt)：可选编辑器脚本，复制到Assets/Editor后提供Hierarchy右键创建启动物体菜单。

需要显示或打包时再看[可选示例界面与平台构建](DISPLAY_AND_BUILD.md)，其中的[SdkCameraQuickStart.cs](examples/SdkCameraQuickStart.cs.txt)和[SdkSkeletonReader.cs](examples/SdkSkeletonReader.cs.txt)用于预览/Overlay与独立结果消费；它们不是初始化和取数据的必做步骤。

两包Git标签均为`v0.4.0-preview.8`，对应发布标签请以本次 Release 的提交为准。本目录只描述SDK和可独立复制的入门示例；API表中的类以包源码为准。

当前随包Profile关闭真实Hand/Handtip/Thumb推理，32个语义槽位不代表全部为有效观察。模型保留评估/分发资格标记，具体见[第三方说明](../../THIRD_PARTY_NOTICES.md)。历史测量与本次文档检查分开记录，不把安装成功或显示FPS当作持续识别性能。

本次发布见[preview.8核验](../reports/2026-10-14-preview8-release.md)，Android CPU/NPU 资源见[本地部署](LOCAL_ANDROID_CPU_YOLO.md)。历史版本依据见[preview.4发布验证](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.8/docs/reports/2026-10-05-preview4-release-verification.md)；文档与代码检查见[本次验证记录](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.0-preview.8/docs/reports/2026-10-07-sdk-user-docs-verification.md)。旧SDK_API/preview.3章节作为历史兼容资料，首次接入以本目录为入口。
