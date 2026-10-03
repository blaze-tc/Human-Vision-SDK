# Human Vision SDK 0.4.0-preview.3

独立 Unity SDK，目标为 Windows x64 和 Android ARM64，不依赖 AzureKinectExamples。
本版使用语义 Runtime Host、可替换流水线/后端、ModelPack 和 Profile。
实际手机帧率、延迟、1–8 人完整骨骼和双手质量仍由用户进行设备验收。

## 安装

当前开发分支拆分为独立输入包与骨骼 SDK。Unity manifest 显式添加两个依赖：

```
"com.blazetc.humanvision.input": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision.input#codex/android-ncnn-vulkan-implementation",
"com.blazetc.humanvision": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision#codex/android-ncnn-vulkan-implementation"
```

以上地址需开发分支已上传；尚未上传的本地改动不能通过 Git 安装。本地验证使用
两个 `file:` 路径分别指向包目录。SDK 中的精确版本依赖不能自动解析同仓库另一
Git 子目录。只预览画面时仅安装 input 包，不需要 SDK 或模型。当前工作不是新
Release；旧发布包不包含本轮统一输入改动。只选择一种导入方式，避免重复脚本和原生库。
编辑器按包内索引复制数据到 Assets/StreamingAssets/HumanVision/Runtime；Android
首次启动按哈希提取到 persistentDataPath，后续复用相同文件。无需清除 Library。
原有脚本 GUID、17+6 点接口和 V1 C ABI 保留。

## 场景与设置

导入 Package Manager 的 UnifiedInput sample，或使用 HumanVision > Create unified
demos in dedicated folder 创建 Camera、Video、RTSP 三个场景。公共按钮切换模式。
公共设置保存人数和编号区域；各模式分别保存设备/路径/RTSP 地址、镜像、采集请求
尺寸及骨骼样式。请求尺寸与实际源尺寸不同，模型分析尺寸必须符合 Profile/ModelPack
契约。Settings 可收起、滚动，隐藏面板不停源。Edit regions 可移动和缩放区域，
Save all settings 保存后重启复用。RTSP 当前合格路径为 H.264 / TCP。

统一 Demo 的区域使用推理后的 bbox/pelvis assignment，不执行整图 CPU mask。
该筛选不代表减少模型推理量。区域编号对应查询占位；跨区域人物或区域边缘动作的
效果仍需实际评估。骨骼来自模型观察，不通过区域框制造关节。

统一 Demo 沿用现有批量骨骼叠加绘制；模式设置提供线宽与点直径。控件与区域边框
使用 1280×720 参考画面和 CanvasScaler，区域线宽不取决于输入视频像素。
旧 HumanVisionSkeletonOverlayer 的 prefab 路径仍保留，新 GameObject/LineRenderer
替换方案尚未交付。点是图像平面坐标，并非深度传感器的米制3D坐标。

## 获取数据

原有 GetColorImageTex、TryGetBodyByRegionIndex、TryGetJointByRegionIndex 等继续可用。
body.CanonicalJoints 提供32个语义关节点，使用 HumanVisionCanonicalJointId 枚举。
双手输出 Hand、Handtip（食指指尖）、Thumb（拇指指尖）；手掌由实际识别点求中心，
标记 IsDerived。看不清的手可以无效，不用手腕伪造手指。
每个关节点带独立 ObservationTimestampUs，手部与人体任务异步进行。

HumanVisionManager.Bodies 是原始观察，SampledBodies 是平滑/有限预测后的显示数据。
Demo 的 targetDisplayFrameRate 默认60，实际显示速率仍取决于设备。
预测最多25ms，随后在按实测观察周期计算的300–800ms显示窗口内保持最后的
滤波骨骼；超过窗口后才隐藏。原始观察时间戳不变，显示保持不计作新识别帧。
数组循环复用，保留历史时自行复制。
ResultUpdated 表示新的原生观察，显示采样不能计算为新的识别帧。
HumanVisionRaisedHandDetector 是举手示例，按区域比较有效肩膀与手腕高度。

## 性能诊断

Android Runtime Mode 在 Project Settings > Human Vision > Android Runtime 显式选择：
NCNN Vulkan（默认）、ORT XNNPACK、ORT CPU。NCNN 模式要求 ARM64、API26、Vulkan、
已匹配的 native/profile/models 与该 Profile 声明的 FP16 能力；失败明确报错，
不静默回退 ORT。兼容模式由用户主动选择后重新构建。预览成功不代表识别初始化成功。

统一 Demo 的紧凑状态条始终显示源状态和错误，展开 Settings 查看识别统计。
原始观察帧率、显示采样帧率和视频帧率分别计量。30 FPS 是每秒30个包含当前全部
Body 的完整新观察帧，不按人数累加，也不把保持/预测结果计作新识别。
端到端年龄必须标明时钟来源，RTSP 的本地解码年龄不是已验证的 sensor age。
当前独立输入与演示功能工作不构成30 FPS 或最终物理设备验收。

维护入口：docs/maintenance/START_HERE.md。当前 Android 输入/NCNN 路径最低 API26；
Unity 托管兼容目标2021.3/2022.3。自动化结果以 DEVELOPMENT_STATUS 为准。
