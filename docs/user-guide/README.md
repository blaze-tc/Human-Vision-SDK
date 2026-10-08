# Human Vision SDK 中文使用文档

适用版本：SDK **0.4.0-preview.6** / Input **0.1.0-preview.4**。更新日期：2026-10-07。[版本下载](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.6)

这套文档面向在**新创建的 Unity 项目**中第一次使用 SDK 的开发者。无需先了解模型或原生推理；先学会Start初始化、输入图像和骨骼结果读取，再按需要查接口。

| 文档 | 主要内容 |
| --- | --- |
| [单组件 Unity 使用](UNITY_SDK.md) | 挂载总控、初始化、区域与骨骼查询 |
| [UGUI 设置 Demo](SETTINGS_DEMO.md) | 参考 Setting 界面、应用/保存、输入与区域编辑 |
| [第一次安装使用引导](FIRST_INSTALL.md) | 安装双包/模型、void Start初始化、摄像头输入、骨骼回调读取、停止释放；可选Hierarchy右键创建菜单 |
| [技术栈文档](TECH_STACK.md) | Unity/Input/Runtime/插件/后端/模型分层、异步数据流和生命周期 |
| [平台测试效果文档](PLATFORM_TEST_RESULTS.md) | SDK发布验证与历史真机测量、数字的含义、平台覆盖与复测方法 |
| [API调用与说明文档](API_REFERENCE.md) | SDK接口的作用、参数、默认值、返回值、关节/帧/区域数据含义和调用示例 |

第一次使用按[代码入门引导](FIRST_INSTALL.md)完成安装，然后只需一个空物体和一份脚本。主要示例：

- [SdkBasicUsage.cs](examples/SdkBasicUsage.cs)：无需UI引用；Start中启动初始化，打开相机，在ResultUpdated中读取人物ID与左腕，提供StopSdk关闭协程。
- [SdkBasicUsageMenu.cs](examples/Editor/SdkBasicUsageMenu.cs)：可选编辑器脚本，复制到Assets/Editor后提供Hierarchy右键创建启动物体菜单。

需要显示或打包时再看[可选示例界面与平台构建](DISPLAY_AND_BUILD.md)，其中的[SdkCameraQuickStart.cs](examples/SdkCameraQuickStart.cs)和[SdkSkeletonReader.cs](examples/SdkSkeletonReader.cs)用于预览/Overlay与独立结果消费；它们不是初始化和取数据的必做步骤。

两包Git标签均为`v0.4.0-preview.6`，对应发布标签请以本次 Release 的提交为准。本目录只描述SDK和可独立复制的入门示例；API表中的类以包源码为准。

当前随包Profile关闭真实Hand/Handtip/Thumb推理，32个语义槽位不代表全部为有效观察。模型保留评估/分发资格标记，具体见[第三方说明](../../upm/com.blazetc.humanvision/THIRD_PARTY_NOTICES.md)。历史测量与本次文档检查分开记录，不把安装成功或显示FPS当作持续识别性能。

版本依据见[preview.4发布验证](../reports/2026-10-05-preview4-release-verification.md)；文档与代码检查见[本次验证记录](../reports/2026-10-07-sdk-user-docs-verification.md)。旧SDK_API/preview.3章节作为历史兼容资料，首次接入以本目录为入口。
