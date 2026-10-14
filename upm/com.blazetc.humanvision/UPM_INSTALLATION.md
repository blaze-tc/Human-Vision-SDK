# Unity 安装 — 0.4.0-preview.8 本地开发包

需要 Unity 2021.3 或更新版本。先安装 Input，再安装 SDK。
选择 Git UPM、两个本地 tgz 或两个 unitypackage 中的一种；不要混装。
从 Assets 安装切换到 UPM 前，先备份项目和配置，再移除旧 SDK/Input 文件，避免重复脚本与插件。

## Git UPM

公共 preview.7 Release 安装仍使用以下地址；本次 preview.8 请使用下方本地包。
在 Package Manager 使用 Add package from git URL，依次添加：

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.7
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.7
```

上述公共 Release 的 SDK 版本为 0.4.0-preview.7；Input 版本为 0.1.0-preview.5。
必须显式安装两个 Git 子目录，SDK 的版本依赖不能自动找到另一个 Git 子目录。
Input 负责 camera/video/RTSP 采集与预览，SDK 负责推理、跟踪、骨骼和区域查询。

## 离线安装

1. 导入 HumanVisionInput-0.1.0-preview.5.unitypackage，等待编译完成。
2. 导入 HumanVisionSDK-0.4.0-preview.8.unitypackage，等待模型和脚本导入完成。

本地 UPM 安装则依次使用 com.blazetc.humanvision.input-0.1.0-preview.5.tgz 和
com.blazetc.humanvision-0.4.0-preview.8.tgz。SHA256SUMS.txt 可验证附件完整性。
Android CPU/GPU/NPU 完整测试资源随后导入匹配的本地 CPU 补充包，
见 Documentation/user-guide/LOCAL_ANDROID_CPU_YOLO.md；该本地包不属于公共 Release。

## 配置与使用

Project 右键 Create → Human Vision → SDK Configuration 创建默认配置资产。
可设置图像尺寸、模型等级、CPU/GPU/NPU、人数、区域、镜像、置信度和结果时效。
HumanVision → Create SDK 创建总控并绑定资产。camera 设备名、RTSP 地址和视频路径
可在代码 InitializeFromPreset(configuration, kind, address) 中传入。

仅保留 Settings 官方样例：HumanVision → Create SDK settings demo assets，
生成 Settings 场景、Prefab 和配置资产。Camera/Video/RTSP 在同一界面切换。
已有用户保存设置优先于资产默认值；创建器不会覆盖同名用户场景。
完整 API、坐标合同与代码见 Documentation/user-guide/SDK_CONFIGURATION.md。

## 平台与已知限制

- Windows x64 带 CPU/DirectML 运行库；Android 为 ARM64、API 26+，GPU 需 Vulkan。
- CPU/GPU/NPU 由平台和安装资源决定；不支持时明确报错，不静默回退。
- RK3588 NPU 是可选集成路线：公共附件不包含私有 librknnrt.so 或私有 RKNN 模型，
  须按仓库 tools/models/rknn 与 tools/package/stage_rknn_experimental.py 的说明另行准备。
  OnePlus 验证不能替代 RK3588 实机验收。
- World 骨骼坐标是图像到世界平面的映射，不是实测人体三维深度；当前模型不承诺真实手端点。
- 稳定每人 20–25 或 30 个完整新骨骼/秒尚未验收；插值和显示 FPS 不算新识别帧率。
- 当前原生全量回归受历史 golden/model 测试数据缺失限制；热机视频长测曾记录一次
  自动恢复的 GPU copy reservation 异常。详见 GitHub Release 说明与验证报告。
- 这是预发布评估包，模型继续保留 local_evaluation_only、distribution_qualified=false
  等标记。上游许可证与来源见 Licenses / THIRD_PARTY_NOTICES.md，标记未被本次发布改变。
