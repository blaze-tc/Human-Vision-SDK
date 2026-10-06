# Human Vision SDK 中文使用文档

适用版本：SDK **0.4.0-preview.4** / Input **0.1.0-preview.2**。核查日期：2026-10-07（北京时间）。

这套文档以正式项目 `Sensory-Game-2021.3.45` 的接入代码、已安装包和留存测试资料为依据。读者不需要先了解模型或原生推理。

| 文档 | 解决的问题 | 建议读者 |
| --- | --- | --- |
| [第一次安装使用引导](FIRST_INSTALL.md) | 从准备环境、安装双包到设置、运行、打包和排错，每步都有检查方法 | 首次使用者、项目接入者 |
| [技术栈文档](TECH_STACK.md) | 游戏、Unity SDK、输入、原生 Runtime、模型和后端如何配合 | 开发与维护人员 |
| [平台测试效果文档](PLATFORM_TEST_RESULTS.md) | Windows/Android 实际有什么证据、测试数字代表什么、还有哪些未验收 | 测试、交付与选型人员 |
| [API 调用与说明文档](API_REFERENCE.md) | 每个接入 API 的作用、参数、返回值、数据含义和调用示例 | Unity/C ABI 接入开发者 |

第一次接入按安装引导顺序操作。已有项目先读技术栈中的调用链，再查 API。评估设备性能先读平台测试文档。

## 版本与证据约定

- Git 安装使用标签 `v0.4.0-preview.4`；两个包在正式项目的 lock 文件中都解析到 `a201e0f44aa68a3f831f248b67400bd5fd7358c9`。
- 文档使用“SDK API”指包提供的接口，“项目 API”指 `SensoryGame.Vision` 自己的封装。SDK 安装不会自动安装正式项目的游戏脚本、字体、素材或设置场景。
- 历史 Android Demo 测试、正式项目 Windows 留存记录、本次文档核查分别标注；没有在本次编写中重跑手机性能测试。
- 当前是接入预览版。当前随包配置关闭真实手部推理；32 个语义关节点槽位不代表 32 点全部有效。模型保留评估与分发资格标记，正式交付前须核对项目使用的模型授权范围，参见包内 [第三方说明](../../upm/com.blazetc.humanvision/THIRD_PARTY_NOTICES.md)。
- [发布下载](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.4) 与[发布验证记录](../reports/2026-10-05-preview4-release-verification.md)是这个版本的依据。旧 `SDK_API.md`/preview.3 指南属于历史兼容说明，首次接入以本目录为入口。

## 正式项目分析范围

项目位置：`E:\UnityProject\YS-Sensory game project\Sensory-Game-2021.3.45`；Unity `2021.3.45f1`。

核对了 `Packages/manifest.json`、`packages-lock.json`、`Assets/Scripts/HumanVisionGame/`、`GameLoading.cs`、`SettingManager.cs`、`HurdleKingManager.cs`、编辑器设置生成器和 `Assets/Tests/HumanVisionGame/Editor/`。

正式项目不是 SDK 仓库的一部分；本文里的项目相对路径用于在该项目中定位。没有把游戏源码、用户图像、视频、RTSP 凭据或电脑日志目录上传到 SDK 仓库。可公开复查的脱敏事实与文件哈希见 [正式项目留存证据摘要](evidence/sensory-project-baseline.json)。
