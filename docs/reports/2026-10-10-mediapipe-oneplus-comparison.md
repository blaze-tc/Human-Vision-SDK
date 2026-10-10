# MediaPipe 与 HumanVision：OnePlus 同机实测

本轮完成的是 **MediaPipe Tasks Android 1.1.0、Bitmap/RGBA 输入、同步 VIDEO
模式独立探针**与当前 SDK 的同机测量，并非 homuler MediaPipeUnityPlugin
打包样例的测量。结果不能推导为“MediaPipeUnityPlugin 不如当前 SDK”或
据此排除插件路线。用户观察到插件骨骼明显更流畅，仍需对实际插件版本、
示例与输入链路进行等条件验证。此次没有替换生产算法、模型或公共 API。

在本轮特定探针链路里，SDK 多人 Vulkan 吞吐更高，MediaPipe 单人返回更
连续；SDK 单人容量大量已完成但无人结果，是独立需要修复的问题。
不能只凭模型执行次数判断骨骼显示流畅度，也不能据当前探针成绩保证
生产插件或多人管线的20–25帧。

## MediaPipeUnityPlugin：用户观察后的源码核查

核查固定 master `eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6`，以及旧版
v0.14.4 `c2d594e0639c3d0e9bafb58f9334928db7adc5ee`。这是参考源码核查，
并未确认用户使用这两个版本，也没有新增该插件APK实测成绩。

- 当前 Pose Landmark Detection 默认 Android GPU delegate、Full 模型、
  单人、LIVE_STREAM；输入默认 **CPUAsync**。GPU推理不等于GPU纹理输入。
- Runner通过 DetectAsync提交、结果回调DrawLater；Screen直接绑定输入纹理。
  因此视频预览不需要等同一帧的推理结果。探针的同步VIDEO/完成后预览不同。
  此机制解释了预览体验差异的可能来源，但不证明骨骼更新率已达到多少。
- Runner另有GPU纹理输入分支：OpenGLES3与有效GPU资源下BuildGPUImage。
  分支仍有纹理复制/等待一帧的TODO，不能称为已证明零拷贝，也不能假定
  用户启用该分支或把这一分支套到Vulkan图形API。
- 旧版Pose Tracking默认smoothLandmarks=true；GPU图包含FlowLimiter，
  推理忙时丢弃输入来限制积压。这些机制影响关节连续性与延迟；不能把
  单人平滑样例的观感直接换算为多人新鲜完整骨骼FPS。
- 当前README要求Unity2022.3以上并列MediaPipe0.10.22；测试项目为
  Unity2021.3.45，探针为Tasks1.1.0。不能把版本不同的成绩视作插件上限。

[当前默认配置](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkDetectionConfig.cs)，
[异步与纹理路径](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkerRunner.cs)，
[预览绑定](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Common/Scripts/Screen.cs)，
[旧版平滑默认值](https://github.com/homuler/MediaPipeUnityPlugin/blob/c2d594e0639c3d0e9bafb58f9334928db7adc5ee/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Tracking/PoseTrackingGraph.cs)，
[旧版限流图](https://github.com/homuler/MediaPipeUnityPlugin/blob/c2d594e0639c3d0e9bafb58f9334928db7adc5ee/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Tracking/pose_tracking_gpu.txt)。

下一比较应采用实际插件，在同一OnePlus/同一输入/相同容量下分别计量
新鲜骨骼回调、无人结果比例、源帧到回调年龄与输入预览帧率，并确认模型、
delegate、运行模式、读取模式和平滑。不能用渲染FPS替代骨骼更新率，也不能
用本探针替代该插件验证。此比较尚未完成，不报告任何预期提速为实测。

## 用户明确的优化目标

用户进一步说明：当前骨骼能够连续，本意是提高识别帧率。后续主目标因此是
提高真实新完成骨骼识别次数，优先验证模型执行耗时与推理链路。平滑、插值、
重复绘制和预览FPS不作为识别提速。七人片段/容量1的间歇空结果仍保留为
独立回归案例，不把该案例改写为用户当前症状或主要优化任务。

## 设备、输入与测量边界

- OnePlus 9 Pro LE2120，SM8350 / 骁龙888，Adreno660，Android14。
- 来源：用户的 `StreamingAssets/video-1.mp4`，选取45–65秒连续动作；
  1024×576、25fps、500帧。实际有七人，测试的是人数上限1/4，
  不是画面只有一人/四人的验收。两条路线均循环该20秒片段。
- 正式 MediaPipe 固定 `com.google.mediapipe:tasks-vision:1.1.0`，官方
  Lite/Full `float16/1` bundle。VIDEO 模式、阈值均0.5、分割输出关闭，
  CPU/GPU明确选择；GPU在同一创建线程执行与关闭。
- MediaPipe 使用预解码原始RGBA，SDK使用MP4解码。500帧逐字节验证
  PNG解码像素与RGBA一致；并不证明硬件视频解码与Bitmap的颜色转换逐值相同。
- SDK固定已安装APK、native和模型。Vulkan Low是YOLO全图512×288 FP32；
  CPU上限1为 `precision-t-26`，上限4为 `rtmo-t-416`。两者模型与管线
  不同，不能将容量/CPU/GPU变化当作同模型加速器倍率。
- 手机串行测试，没有同时运行两个推理应用。没有修改热策略或强制频率。
  SDK每轮还原原始设置文件并逐字节核对；电脑Unity、RtspServer、VLC保留。

MediaPipe逐帧记录真正完成时间、源索引、返回时间戳、人数和33点；
检查单调性、容量、有限值、行数与完成标记。25fps实时节拍跳过过期输入。
表中“含人新结果/s”只表示一次新结果含至少一个人体，不是每个人的完整
骨骼帧率。MediaPipe时长60s/预热15s，Lite GPU1为90s/预热15s；SDK捕获
90s，按实际Running/Streaming段排除20s，正式有效约63–64s。

## 实测结果

### MediaPipe正式矩阵

任务耗时是同步 `detectForVideo` 墙钟时间，包含任务图、GPU等待、结果回传等，
不是单个模型纯GPU时间。应用帧率还包括RGBA读取、Bitmap、预览拷贝、
JSON日志和线程调度。GPU由Adreno660 OpenGL ES委托执行，不是NCNN Vulkan。

| 模型 | 模式 | 上限 | 含人新结果/s | 任务平均 / P95 ms | 实际平均人数 | 满容量帧比例 |
|---|---|---:|---:|---:|---:|---:|
| Lite | CPU | 1 | 17.360 | 45.761 / 89.266 | 1.000 | 100.00% |
| Lite | CPU | 4 | 6.826 | 120.333 / 173.497 | 3.809 | 85.11% |
| Lite | GPU | 1 | 17.202 | 39.377 / 47.492 | 0.999 | 99.92% |
| Lite | GPU | 4 | 8.323 | 89.784 / 121.008 | 3.800 | 81.33% |
| Full | CPU | 1 | 13.600 | 59.387 / 101.650 | 1.000 | 100.00% |
| Full | CPU | 4 | 4.839 | 174.656 / 260.965 | 3.784 | 80.28% |
| Full | GPU | 1 | 13.852 | 52.134 / 61.278 | 1.000 | 100.00% |
| Full | GPU | 4 | 8.839 | 84.290 / 126.488 | 3.995 | 99.50% |

所有正式MediaPipe轮的系统热状态为0。四人容量并非始终返回四人；
Full GPU4的返回人数更稳定，但没有人工标注，不能据此宣称精度获胜。
旧0.10.35/PNG预读试验以及短smoke没有混入正式矩阵。

### 当前SDK正式矩阵

| 模式 | 上限 | 完成结果/s | 含人新结果/s | backend平均ms | 热状态 |
|---|---:|---:|---:|---:|---:|
| Vulkan Low | 1 | 23.519 | 10.200 | 39.760 | 0 |
| Vulkan Low | 4 | 22.826 | 22.482 | 40.741 | 0 |
| CPU | 1 | 14.837 | 8.413 | 24.219 | 0 |
| CPU | 4 | 7.121 | 7.121 | 134.134 | 3 |

SDK CPU4的系统热状态3表示严重热节流，平均CPU温度69.10°C、应用CPU
一核口径347.12%（全核43.39%）。这一轮不能与MediaPipe热状态0的CPU轮
直接算模型/硬件提速倍数。自然空闲恢复后连续三次状态0，未修改策略。
[Android热状态定义](https://developer.android.com/reference/android/os/PowerManager#THERMAL_STATUS_SEVERE)。

SDK Vulkan4在全部有效一秒诊断窗口中95.31%达到20个含人新结果/s，
最低19.66，P05为20.70；仍未全程稳定达标。该轮每秒末的人数快照平均
3.34，满四人的快照43.75%。这是稀疏快照，与MediaPipe每帧人数统计
采样不同，不能直接作为精度或四个人各自22.5fps的证明。

## 单人间歇空结果：设备证据与代码机制

SDK Vulkan1首轮每秒完成23.519次，含人的新结果只有10.200次；
独立复测为完成23.833次、含人11.972次。两轮backend约39.6–39.8ms，
热状态0、无pipeline错误。首轮每秒末快照63次中38次为0人。
CPU1也出现同类现象：完成14.837/s、含人8.413/s，64次快照32次为0。
因此存在持续检出问题，不能仅看模型执行帧率。

原始日志中无人时native/public人数均为0；不是native有人而UGUI未画。
单人轮抽样稳定ID频繁变化，支持候选切换方向，但这不是标注过的身份准确率。
首轮native/public配对和抽样ID保存在 `native-public-body-diagnostics.json`。

代码路径：YOLO decoder先按源面积排序并截断到配置容量；GPU pipeline没有
赋予这些观察crop track ID。BodyServices随后关联既有稳定轨迹。槽位满、
新候选不能匹配且无已知crop身份时，新观察会被略过；Raw只输出本轮新观察。
这保护既有身份，但当多人画面里的最大候选不断切换、容量仅1时，
上游截断可能让既有被跟踪者根本没有进入关联阶段。

直接编译当前 `runtime/services/body_services.cpp` 的离线诊断复现：
每帧输入一个有效bbox，A→B→A且间隔40ms，容量1的Raw人数为1→0→1；
容量2下A→B为1→1。这是合成几何输入对既有代码机制的验证，
不是模型推理/设备精度测试，也尚未逐帧证明所有设备空结果都来自该机制。
诊断源码、编译日志、JSON都保留；没有通过提前释放稳定轨迹或伪造关节来掩盖。

下一步应让候选选择保留已跟踪者的匹配机会，然后再限制公开人数；
必须同时检查新用户进入、遮挡、离场、区域绑定和稳定ID。
不能简单把人数调小理解为更快，或直接让每帧不同的人复用同一个ID。

## 具体耗时与优化方向

SDK Vulkan4的23个独立native采样：

| 阶段 | 平均ms |
|---|---:|
| 输入/预处理记录 | 0.068 |
| 预处理提交等待 | 1.996 |
| 模型extract/download合并阶段 | 33.939 |
| 后续推理/输出提交等待 | 3.581 |
| 稠密输出拷贝 | 0.417 |
| 所有权释放 | 0.846 |
| 同批native合计 | 40.846 |

extract/download占同批合计约83.1%，包含内部GPU计算、提交、等待与下载；
不是“纯拷贝33.9ms”，也没有逐层GPU计时来区分算力与带宽。
本轮GPU利用率76.40%是设备整体vendor指标，不能单独证明SDK已耗尽GPU。
native六段是稀疏串行墙钟时间，不与另外一个frame的SDK总耗时相减。

MediaPipe正式多人任务平均84–175ms，输入读取仅2–3ms；改为RGBA后，
读取已不再是主要测得耗时。补测不限速Lite单人：GPU16.493/s、CPU16.793/s，
任务分别41.087/47.206ms，热状态0；没有解除25fps节拍就达到高帧率的证据。
不限速仍包含探针预览与完整日志，不是纯算法最高帧率或实时延迟测量。

官方图实现对每个人的ROI执行landmark任务；跟踪足够时跳过检测，人数不足
时再检测，且当前主分支平滑只支持单人。这解释了为什么单人样例不能直接
推算多人速度/观感；这里是源码机制解释，没有声称主分支与固定1.1.0二进制
逐字一致。[ROI任务源码](https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/tasks/cc/vision/pose_landmarker/pose_landmarks_detector_graph.cc)，
[跟踪与平滑源码](https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/tasks/cc/vision/pose_landmarker/pose_landmarker_graph.cc)。

建议先修复已复现的单人候选/跟踪衔接，同时完成实际MediaPipeUnityPlugin
异步链路对照；生产路线选择仍开放。当前多人全图Vulkan作为基线保留，
继续核查合并阶段的模型执行与等待。MediaPipe的CPU/GPU都可作为备选，
须另行验证输入、身份、关节契约与同机体验，不能由本探针决定全面替换。
本轮没有使用官方LIVE_STREAM/摄像头纹理输入来衡量MediaPipe最优接入方式，
也没有测RTSP端到端延迟，所以不推广为“MediaPipe普遍比SDK快/慢”。

## 稳定性、验证与交付边界

- 正式探针1.1.0在GPU轮出现 `tensor.cc:425` Tensor重复写入同步错误提示；
  任务仍真实完成并返回有限坐标。保留原始日志，没有声称问题已解决或GPU
  委托已满足生产稳定性。没有用“GPU不可用/回退CPU”解释该轮。
- 四个正式SDK轮和单人复测均无pipeline错误。Vulkan1/4、CPU1及单人复测
  的events与hardware日志各有一条force-stop留下的尾部JSON残片；hardware
  告警在分析JSON，全部JSONL检查在 `all-sdk-jsonl-audit.json`，均明确排除
  尾部残片。CPU4无此告警。性能CSV、timings及骨骼JSON完整，未补造数据。
- 分析器先真实RED（模块尚未实现），随后6/6 GREEN；拒绝重播源帧、人数
  与点数矛盾、非有限点，排除预热和不完整时间窗；不限速不提供实时延迟。
- 探针ARM64 APK真实构建、安装后与源APK SHA一致；全部500帧像素核对通过。
  离线候选机制诊断编译/执行通过；架构与public surface检查PASS。
  当前Human-Vision-SDK-Test Editor `debug_get_errors` 返回0。
- 没有更换SDK/APK，没有SDK推理实现变更。因此本轮没有重新声称全量native/
  Unity回归或新性能优化交付。数据支持方向选择，不是已修复单人问题的版本。
- Pose33、COCO17/Body26与SDK32关节契约不同。没有证明真正独立Hand/Handtip/
  Thumb、3D测量、完整每人帧率或有标注的精度；SDK world plane不是真实深度。
  OnePlus数据不推算RK3588或NPU性能，不发布实验模型为公开Release。

## 身份与可复核文件

| 资产 | SHA256 |
|---|---|
| 原始视频 | e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8 |
| 对照片段 | bf22d75111d5c5892c6ca66d21502d558386b7b6dedd242f7dd78ed8d7d21981 |
| RGBA500帧 | 598f5f52f9320eab71e857220f8f9d0ef1866793137bc658e7b3111d857a261f |
| 正式探针APK | a05c068e789451f1a043c31431d74118472c3b2c728af11f5ad4a23370047963 |
| 探针JNI | af5da2251aae340a8ac9d225f0780ecfc50a5fb36ea17e17218f362b1e90dceb |
| 固定SDK APK | 1efc3ba975618156d7fa227fc87306c095da005b8e0edb1315646fad38ee7a82 |
| SDK native | 5fd4366a82e4ef038e4e72c311a3302912d6c0cd331e96904ffa123bcb794f5e |
| Input native | 5ad008563b23ef5281c7387800cb47bd79f7542de85cc7b493e70714a8b6c2b3 |
| Lite bundle | 59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a |
| Full bundle | 5134a3aad27a58b93da0088d431f366da362b44e3ccfbe3462b3827a839011b1 |

SDK测量版本为 `f5f8525`；本报告提交只增补诊断记录。
用户可直接获取整个文件夹：
`E:/UnityProject/Human-Vision-SDK-Test/DiagnosticsVerification/MediaPipe-Comparison-20261010/`。
内含REPORT/README、每轮完整session和logcat、截图、JSON/CSV汇总、模型/
像素/APK身份、探针源码配方、capture/analyzer及RED/GREEN日志。
大视频、APK、原始设备日志保留本地，不放入源码Git提交。

官方固定模型：[Lite](https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task)，
[Full](https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_full/float16/1/pose_landmarker_full.task)。
接入方式参照[官方Android指南](https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker/android)。
