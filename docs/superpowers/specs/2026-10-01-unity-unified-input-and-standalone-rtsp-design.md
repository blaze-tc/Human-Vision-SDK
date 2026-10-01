# Unity 统一画面输入与独立 RTSP 模块 — 待用户审查

日期：2026-10-01。基线 Android worktree `736c874`；PC Demo `2031f90`。
状态：用户已确认设计并要求编写实施计划，尚未实施；2026-10-01追加三个独立Demo及公共/模式设置分离要求。

## 用户目标与边界

Unity 控制视频、摄像头和 RTSP 的启动、停止、切换与画面获取。三种源提供
同一 Texture/帧信息接口；识别模块只消费画面。RTSP 像 WebCamTexture 一样能
作为独立摄像头画面功能使用，不要求安装模型、创建 HumanVisionManager 或
初始化识别 Runtime。高清采集/预览分辨率与模型输入尺寸分开配置。

“Unity 采集”指 Unity 拥有源组件及其生命周期、最终 Texture 和控制接口；
RTSP 网络协议、解码及 GPU 导入由独立平台插件执行。不是用 C# 重写解码器。
公共骨骼 API、V1 C ABI、Canonical Skeleton、Tracker、Profile/ModelPack 保持稳定。
Android NCNN 仍显式选择、fail-fast、Vulkan/API26/ARM64、无整帧 CPU 回读及 ORT fallback。
不进行模型替换、Hand/QNN/RTMO 或 ORT/Windows 推理性能优化；不合并 main/发 Release。

## 现有问题与代码证据

- `HumanVisionLiveSource.cs` 依赖 VideoPlayerFrameSource，并直接访问识别管理器，
  还在旋转时调用 CameraManager.ApplySettings，不能独立预览。
- `HumanVisionCameraManager.Start` 先准备模型/Runtime，再打开画面。
- `VideoPlayerFrameSource.SubmitExternalTexture` 在预览前要求 manager 初始化。
- 既有 `native/src/input/rtsp_source.cpp` 自身不需要模型，但编译进 humanvision；
  FFmpeg 解码后 `sws_scale` 产生 CPU RGBA，Unity LoadRawTextureData/Apply 上传。
- Android NCNN 在 LiveSource 中显式拒绝上述 CPU RTSP 输入。
- 现有正常 Android NCNN MP4 PlayUrl 被拒绝；视频诊断已有 VideoPlayer→GPU
  Texture→AHB→ncnn 的实际运行证据，应迁移这条已验证输入能力而非保留仅测试入口。

## 方案比较与建议

| 方案 | 取舍 |
|---|---|
| 推荐：独立输入包 + 平台解码插件 + SDK 订阅适配器 | 符合独立预览和 GPU 输入，初期需要 Android RTSP 解码纹理桥 |
| 只从 LiveSource 拆出 RTSP 控制脚本 | 较小改动，但 Native 库仍携带识别依赖且 Android 仍走 CPU RGBA，不满足目标 |
| 直接用 Unity VideoPlayer 播放 RTSP | 无当前版本/目标平台支持证据，不能据此承诺可用；不采用 |

## 模块与依赖

长期 SDK，采用四个边界，避免输入实现进入骨骼算法层：

1. `HumanVision.Input`：源配置与统一帧契约，VideoSource、WebCameraSource、RtspSource。
2. `humanvision_input` native：独立 RTSP 网络/解码/平台纹理插件，不链接 ncnn、ORT、
   Runtime Host 或模型；Windows 为 DLL、Android 为 SO。复用已锁定 FFmpeg 来源。
3. `FramePreview`：仅从输入源取 Texture，供 RawImage/材质使用，不依赖识别。
4. `HumanVisionInputAdapter`：可选识别消费者，管理 SDK 入帧、GPU lease 与结果映射。

独立输入包计划 `com.blazetc.humanvision.input`，独立 asmdef；SDK 包依赖它。
旧 CameraManager、GetColorImageTex 等 API 通过适配继续可用；旧 HV_Rtsp 导出保留
兼容入口，不删除或改签名。新应用可只装输入包；旧 SDK 场景仍可以运行。
不能让独立 input 插件反向依赖 SDK native/clock/模型，也不能以空模型假装独立。

## 统一帧契约与生命周期

源组件提供 Open/Close、State/LastError、CurrentTexture、TryGetLatestFrame。
帧值类型包含：sourceId、session/generation、frameId、Texture、实际 width/height、
本地 monotonic timestamp、timestamp 含义/时钟域、PTS（如有）、rotation/mirror、
row-origin、color-space，以及资源有效期。对外不暴露算法/张量类型。

预览和识别消费同一标准化画面及坐标变换；旋转/镜像只处理一次。实际分辨率、
色彩和方向以帧契约为准，不能只用请求值。当前矩形 YOLO 只接受16:9横屏；
独立预览仍支持其他方向，识别适配器对未验证输入给出明确错误，不强制拉伸。

主线程负责 Unity 对象、Texture/RenderTexture 生命周期与事件分发；网络/解码
在后台，渲染事件负责 GPU 操作。Warm-up 后复用槽、纹理和帧描述，不每帧创建
AHB/import pipeline、JSON、Task 或事件对象。只发布最新完成的解码画面；识别
慢、初始化失败、没有订阅者时，预览持续更新。

源切换、重连、新尺寸/色彩契约均提升 generation，旧帧/异步回调不可进入新源。
输入源拥有预览纹理；适配器只持有 GPU snapshot-copy 的源 lease。切源/Close 先
禁止新提交，等待已排队源 copy 的 GPU fence 退休后，才销毁旧源纹理。已完成 copy
的不可变 inference 槽由适配器/Runtime 独立持有，源 Close 不等待模型 inference。
适配器 detach 时取消尚未入队的提交，退休已入队源 copy lease；槽内推理结果按旧
generation 丢弃，Runtime 后续自行回收。仅安装输入包时没有 inference lease。
若现有 EndAndroidGpuSourceLease 仍将 source-copy 与 inference drain 混在一起，实施
需先拆分这两个退休边界并验证 fence/引用计数；不得直接将同步全量 drain 用作源 Close。
预览可借用活跃纹理；网络/解码异步停止，在源 GPU 操作退休后回收，不等待模型。

## 平台采集路径

- 视频：Unity VideoPlayer→Unity RenderTexture，正常运行入口支持本地 MP4。
  本轮不宣称所有 URL/编码/流协议可用。复用实际视频诊断中验证过的帧回调、
  方向和源 generation；删除测试名称依赖，保留旧行为兼容适配。
- 摄像头：Unity WebCamTexture→GPU 方向校正→Unity RenderTexture。
  保留 UnityObserved 时间语义，不声称是 sensor capture timestamp。
  原 Revision4 的 Camera2 sensor-age gate 未因此通过；本提案仅明确 Unity 统一
  输入与元数据边界，不把当前摄像头诊断变为 production 物理验收。
- Windows RTSP：独立插件复用当前 FFmpeg CPU 解码与 Unity Texture 上传，明确
  DecodeMode/成本；这不是 GPU-only 路径，也不修改 Windows 识别性能实现。
- Android RTSP：独立 FFmpeg RTSP demux/压缩 access units→MediaCodec 硬件解码
  output Surface→AImageReader/AHardwareBuffer→Unity Vulkan GPU 色彩转换/拷贝
  →Unity-owned RGBA RenderTexture→现有 AHB bridge→ncnn。
  网络压缩数据经 CPU 合理，禁止整帧解码图像进入 CPU、CPU sws_scale 和 CPU 回读。

Android 首版 gate 为 H.264/RTSP TCP；UDP、H.265 不宣称已完成，后续按明确能力
逐项验证。输入源缺少硬件解码/格式/Vulkan 能力时给出错误，不自动走软件解码。
第一阶段不按后端裁剪 APK，旧兼容库同包不代表自动 fallback。

## Android RTSP GPU 桥的实现 gate

MediaCodec Surface 本身不是 Unity Vulkan Texture。优先验证 API26 的
AImageReader PRIVATE + GPU sampled usage；不能假定 decoder 支持 RGBA 或任何
usage/format 组合。查询实际 AHB/Vulkan external format、sampled features、
YCbCr conversion 参数、dataspace/range/crop/stride；不支持则明确失败。
GPU 色彩转换写入 Unity RGBA 目标，验证 BT.601/709、full/limited range、方向，
不将 YUV/OES handle 当 Unity VkImage 使用。现有三槽 ncnn AHB bridge 不改。

获取最新 AImage 时保留 acquire fence；Unity GPU 队列显式 wait，取得资源外部
ownership 后 sample/color-copy，copy 完成 signal release fence，再释放 AImage/
AHB lease。不能阻塞 render thread 等模型，也不能删除 sync/ownership 操作。
AHB 导入按活跃 decoder buffer 身份及 session 缓存；bufferRemoved/重连/契约改变
后在 GPU 退休再销毁，不按帧重建。无可用源输出槽时丢解码画面，不随机丢压缩
P/B 包破坏解码依赖；压缩队列有界，过载则受控 flush/重连等待关键帧。

timestamp 区分 RTP/PTS、本地接收/解码、Unity发布；未经时钟映射/摄像头校验的
RTSP PTS 不能冒充 sensor capture time，也不能据此宣称端到端摄像头延迟通过。

## 场景、配置与验收

SDK提供三个独立场景：CameraDemo、VideoDemo、RtspDemo，用公共选择按钮互相切换。
公共识别设置包含人数、区域规划和ModelPack已验证的分析输入尺寸；任意输入尺寸不得
绕过模型契约。每个Demo独立保存本模式参数：设备/视频/RTSP地址、镜像、骨骼线宽
和点径等。公共设置面板Prefab复用，模式参数不在切场景时互相覆盖。
独立输入包仍提供无识别组件/模型的InputPreview样例，证明只需输入包即可播放。
三个SDK Demo通过同一源接入识别，保留现有骨骼调用，可独立显示画面。
请求720p/1080p/FPS和实际值分别显示；RTSP 地址配置不写入分享日志/报告中的凭据。
新输入层不重写骨骼 Renderer，也不用 prediction 冒充 fresh FPS。

先测接口/状态/代际/资源退休和旧 API 兼容，再验证独立预览/缺模型仍可播放，
之后按 Video→WebCamera→RTSP 真机串行验证。同一源同时预览/识别，对照坐标与
方向；断线/恢复、切源、旋转、尺寸变化、暂停恢复均有测试。Android GPU route
整帧 CPU readback 计数必须0，copy_error/import_error/runtime_error 必须0；正常
GPU copy/import 操作计数应存在并与实际提交对应，不能把这些必要操作误写为零。
能力不足必须错误而非 fallback。
fresh observation FPS 按完整帧计；含空/部分 Bodies，另报关节跟随/人数覆盖和
P50/P95年龄、drops/热状态。>=25过渡/30硬目标不因输入重构自动视为达成。

需要真实 RTSP 地址或受控 RTSP 测试服务器后才能完成协议/摄像头验收；当前没有
地址，不将普通 MP4 代替 RTSP 验收。不在本设计审查阶段实现该 native producer。

## 外部依据与待验证事实

- [Unity VideoPlayer](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/video/videoplayer)：
  文档示例为文件/HTTP 与 RenderTexture，不能由此推导 Unity2021.3 RTSP 可用。
- [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)：
  decoder configure 可用 Surface 输出；实际 decoder/Surface 组合需要设备 gate。
- [Android NDK ImageReader](https://developer.android.com/ndk/reference/group/media)：
  API26 newWithUsage/PRIVATE 与 AHardwareBuffer 能力；并非任意格式/usage 都支持。

用户已确认上述独立输入包/插件边界和Android GPU RTSP路径；当前进入详细实施计划
阶段。沿用sequential fresh implementer + spec/quality review，计划书面审查后实施。
