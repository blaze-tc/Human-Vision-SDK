# Human Vision SDK 中文使用文档

适用版本：SDK **0.4.0-preview.4** / Input **0.1.0-preview.2**。更新日期：2026-10-07。[版本下载](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.4)

这套文档面向在**新创建的 Unity 项目**中第一次使用 SDK 的开发者。无需先了解模型或原生推理；先把相机和骨骼跑起来，再按需要查接口。

| 文档 | 主要内容 |
| --- | --- |
| [第一次安装使用引导](FIRST_INSTALL.md) | 创建项目、安装双包/模型、运行自带示例、从空场景搭建Canvas/预览/骨骼、初始化、关节点读取、停止、Windows/Android构建与排错 |
| [技术栈文档](TECH_STACK.md) | Unity/Input/Runtime/插件/后端/模型分层、异步数据流和生命周期 |
| [平台测试效果文档](PLATFORM_TEST_RESULTS.md) | SDK发布验证与历史真机测量、数字的含义、平台覆盖与复测方法 |
| [API调用与说明文档](API_REFERENCE.md) | SDK接口的作用、参数、默认值、返回值、关节/帧/区域数据含义和调用示例 |

第一次使用从[安装引导第1节](FIRST_INSTALL.md#1-准备环境并创建项目)开始。跟随第4节先运行自带示例，再按第6～10节创建自己的场景。可以直接复制的完整脚本：

- [SdkCameraQuickStart.cs](examples/SdkCameraQuickStart.cs)：Windows x64 CPU初始化、相机输入、预览/Overlay绑定与停止释放。
- [SdkSkeletonReader.cs](examples/SdkSkeletonReader.cs)：新身体结果回调、多人遍历、人物ID、左腕/左膝读取与取消订阅。

两包Git标签均为`v0.4.0-preview.4`，对应源码提交`a201e0f44aa68a3f831f248b67400bd5fd7358c9`。本目录只描述SDK和可独立复制的入门示例；API表中的类以包源码为准。

当前随包Profile关闭真实Hand/Handtip/Thumb推理，32个语义槽位不代表全部为有效观察。模型保留评估/分发资格标记，具体见[第三方说明](../../upm/com.blazetc.humanvision/THIRD_PARTY_NOTICES.md)。历史测量与本次文档检查分开记录，不把安装成功或显示FPS当作持续识别性能。

版本依据见[preview.4发布验证](../reports/2026-10-05-preview4-release-verification.md)；文档与代码检查见[本次验证记录](../reports/2026-10-07-sdk-user-docs-verification.md)。旧SDK_API/preview.3章节作为历史兼容资料，首次接入以本目录为入口。
