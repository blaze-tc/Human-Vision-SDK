# 2026-11-18 Android RTSP 高清预览与同帧配对

## 基线与定位

业务工程锁定 SDK `0.4.0-preview.15`、Input `0.1.0-preview.8`，提交 `10363f95a60018d7078b834140835e98ba1ff6c0`。复现和候选改动在 SDK 下的 `.worktrees/rtsp-preview-cadence`，独立场景与运行证据在 `E:/UnityProject/Human-Vision-SDK-Test/Builds/RuntimePrefabAcceptance-Exercise11-20261118`。

客户跨栏王录像 04:03 的 HUD 记录游戏 30.0 FPS、Unity 观察到的高清预览 5.0 FPS、含无人结果的推理新结果 23.9 FPS，同帧为「等待」。其他采样的预览约 5.0–6.8 FPS、新结果约 18.5–23.9 FPS。此次可读取的档案未提供该 RK3588 录像同一窗口的三种原生 cadence，因此无法分配现场掉帧在让行、空槽、栅栏和解码之间的占比。

归档的 `SensoryGame-TestArchive-20261118-112058/Logs/RTSP-Startup-20261117/preview15-healthy/logcat.log` 有解码与 Vulkan 转换记录，但其 `[VisionSession]` 显示 `model_sized_input=False`、`backend.ncnn.vulkan` 对应的低档 profile 和 1280×720 请求，属于一加单路启动试验。11 月 18 日的 `Logs/RTSP-Startup-20261118/manual-npu-healthy/logcat.log` 记录在 SM8350 上拒绝了 RKNN 初始化。两组档案都无法补足录像时段的 RK3588 原生计数。

可独立复现的调度问题是 `InputSplitSchedule::Choose` 优先选就绪分析，随后释放原本可用的预览候选。在空闲高清槽和持续分析的 300 次模拟调度中，基线完成 300 次分析和 0 次预览，299 次记入 `preview_yield_to_analysis`。候选在相同条件下完成 300 次分析和 300 次高清转换。分析持续就绪但无高清空槽的模拟中，候选仍完成全部 300 次分析，高清转换为 0，有 300 次高清槽不可用机会。模拟使用即时完成和固定调度节拍，计数验证选择策略。

## 同帧画质

此前业务归档的 `before-camera-clarity-VisionGameSettings.cs` 第 33–38 行允许 Android RTSP 低档 GPU/NPU 使用模型图做同帧底图。`before-camera-clarity-VenueSettingsController.cs` 只在 `pair.BodyCount > 0` 时从实时相机像素切换到合成图，并未约束两者尺寸。此路径可以在首次识别到人时从高清实时图切到 512×288 合成图。公开 preview.15 的 RuntimePrefabAcceptance 样例也默认采用模型图配对。当前业务源码已经设为 `UseAnalysisFrameForMatchedPreview = false`，场地设置同时要求同帧底图尺寸等于实时帧。旧代码解释了出现画质切换的条件，客户录像没有记录切换时两张纹理的实际尺寸。

候选的 RuntimePrefabAcceptance 默认选择高清同帧底图。Android 分路仍按相机尺寸生成最高 1920×1080 的等比预览，低档分析仍提交 512×288。实时预览、同帧图像及基于同帧图像的拍照分别沿用相机预览像素。切源、重连或识别配置更新时丢弃旧的配对记录。结果先于对应高清图到达时，保留上一张合成图的逻辑继续校验来源和代次。若收到较新的空结果，清除旧骨骼显示。

## 软件验证与采集

- 健康的最新解码帧同时有分析槽和高清槽时，转换在同一个 Vulkan 提交中完成，两个输出使用同一源帧身份。高清槽不可用时先完成分析，过期候选按解码时间清理。原生新增合批、候选被合批替换和高清目标准备失败的计数。[原生调度](../../native/input/src/android/unity_input_vulkan.cpp)
- 基线调度回归按预期失败，候选 Android ARM64/API26 Input 构建与主机 Input 回归 56/56 通过。Unity 相关 EditMode 回归 93/93 通过。原始调度与测试结果在工作树 `out/rtsp-preview-cadence-20261118/baseline-native`、`candidate-native` 和 `managed-green.xml`。
- 验收场景记录实时预览、同帧原图、合成纹理和模型输入的实际尺寸。每个配对事件核对结果序号、帧标识、时间戳、来源、代次与可独立取得的解码帧号。支持骨骼画面开关及独立无人回放。测试工程验收脚本和命令见 `capture-and-analyze.md`。Android 和 Editor 诊断场景源码分别使用 Tuanjie Roslyn 编译通过。
- [统计工具](../../tools/test/analyze_rtsp_preview_cadence.py) 在同一时间窗口汇总合并的 JSONL 或外部 logcat。原生各阶段和 Unity 分别报告实际覆盖时长，统计高清丢弃原因、帧龄和配对最长间隔。GPU fence 字段是提交到主机观察到完成的时间。
- 历史 11 月 17 日一加 4K25 CPU 回读日志可验证统计工具的时间对齐。候选历史 APK 路径 `Builds/RuntimePrefabAcceptance-Android-PreviewCadence-20261117/verification/candidate-warm/4k` 内的 JSONL 与相同 PID 的外部 logcat 在 32 秒窗口内得到相同原生统计。解码 24.978、原生高清转换 23.032、Unity 预览 23.045、原生分析 24.812 FPS，记入让行 54 次，高清槽不足及未观察覆盖均为 0。该次使用另一份素材、旧调度和模型图配对，仅用于核查统计工具及提供历史背景。

发布版、候选 APK 和 SDK/Input 本地候选的身份随对应 build 与 package 记录核对。RK3588 上还需在同一时间窗口实测实际 `backend.rknn`、`android-rknn-npu-quality-low`、高清预览和配对帧率，以及 512×288 分路的吞吐及结果时延。

## Exercise-11 一加 9 Pro 前后对照

从客户原录像在 04:03 抽取的画面显示「游戏 30.0｜输入 5.0｜新结果 23.9 FPS｜同帧 等待」。跨栏王的 HUD 将「输入」直接绑定到 SDK 的 InputPublicationFps。该值计数 Unity 观察到的相机高清分路新帧。录像文件自身的帧率不表示各分路更新频率。证据在独立工程本轮输出的 customer-hud/at-04m03.png。

同一台 OnePlus 9 Pro（LE2120，Android 14）、同一 Exercise-11 H.264 原片、同一个本机回环 TCP RTSP 18567 路径。MediaMTX 和 FFmpeg 以流复制发布，两个包均固定使用所填码流并将 Unity 目标设为 30 FPS，UseAnalysisFrameForMatchedPreview=false。实际后端均为 backend.ncnn.vulkan，profile 为 android-ncnn-vulkan-quality-low。两个包开始时电池 32.0°C 和 34.6°C，稳态样本中位数 32.4°C 和 35.0°C，系统热状态均为 0。本次是相同热状态下的短时对照，起始温度有 2.6°C 差异。

以各自 ready 后第 6 秒为起点取 30 秒窗口。剔除窗口边缘未完整覆盖的记录后，Unity 两版覆盖 28.67 和 28.84 秒，原生约 29 秒。实际解码、高清预览、同帧底图和合成纹理均为 1920×1080，模型提交均为 512×288。

| 稳态指标 | preview.15 / Input preview.8 | cadence.1 |
| --- | ---: | ---: |
| Unity 游戏 FPS | 30.06 | 30.06 |
| 硬解输出 / AImage 获取 FPS | 30.04 / 30.04 | 30.00 / 30.00 |
| 原生高清转换 FPS | 20.48 | 29.11 |
| Unity 实际观察到的高清帧 FPS | 18.35 | 26.11 |
| SDK InputPublicationFps 的窗口采样均值 | 18.43 | 26.14 |
| 原生分析发布 FPS | 29.10 | 29.11 |
| SDK 分析输入 FPS 的窗口采样均值 | 26.93 | 26.10 |
| 新结果 / 有效骨骼 FPS | 23.89 / 23.89 | 23.96 / 23.96 |
| 高清同帧配对 FPS | 17.09 | 23.96 |
| 有效骨骼配对率 | 490/685，71.53% | 691/691，100% |
| 高清配对最长更新间隔 | 183.39 ms | 117.12 ms |
| 解码至有效结果 P50 / P95 | 123.40 / 139.36 ms | 120.53 / 140.99 ms |
| 解码至高清配对 P50 / P95 | 141.99 / 158.65 ms | 138.44 / 160.64 ms |
| 分析提交至观察到 fence 完成（均值） | 9.11 ms | 9.49 ms |

稳态让行 255 → 0 次，高清空槽 0 → 1 次，候选过期和目标准备失败均未观察到。完成转换后的未观察覆盖为 64/603 → 87/856，约 10.6% → 10.2%。Unity 每次取最新已完成帧，因此当多帧在两次观察之间完成时，旧发布会被替换。这解释了原生转换 29.11 FPS 与 Unity 观察 26.11 FPS 的剩余差距。候选的 691 个稳态配对均核对了输入帧身份，身份及纹理尺寸不一致为零。

Exercise-11 原片的单独无人窗口将右侧切为无骨骼实时预览，并保留内部配对计数。两组有效骨骼均为 0，高清预览 16.83 → 25.88 FPS，短窗口新结果 24.12 → 22.53 FPS，整个 20 秒分段分别收到了 459 和 453 个有效提交后的新结果。零人体计数用于区分无人体与停止更新。配对底图和实时纹理保持 1920×1080。停止并重新发布 RTSP 八秒后，两组均进入 Reconnecting、隐藏旧同帧图、切换代次 2 → 3 并恢复有效骨骼配对。候选重连窗口核对了 837 个输入帧身份，无身份或尺寸不一致。

原始前后日志按场景分在独立工程的 android-baseline-measure 和 android-candidate。每个场景有 runtime-prefab.jsonl、同 PID 的 logcat.txt、温度、截图及 summary.json。已安装 APK 与本地 APK 的 SHA-256 已逐包核对，两个 APK 均通过 Android 签名检查，Input 原生库分别与发布版和候选库逐字节对应。

## Windows 功能对照

在独立的团结引擎 2022.3.61t4 Windows Editor / D3D11 运行 Exercise-11 测试。发布代码基线 1/1、候选代码 3/3 PlayMode 通过。候选采集 427 个 1920×1080 配对（129 有人、298 无人），基线 485 个。两组关闭骨骼均继续收到 71 个高清帧且配对回调为零，重新连接后新来源恢复配对。Windows 实际提交 1280×720，模型尺寸 512×288。单次桌面配对节拍有差异，因此这里以功能检查为主。输出在 SDK 工作树的 windows-runtime-prefab-baseline 和 windows-runtime-prefab-candidate 系列文件。当前 Unity 2021.3.45f1 的额外 preview.15 RTSP 回放记录也保存在独立工程 windows-baseline。

## 扩展检查与最终候选

最终本地候选为 SDK 0.4.0-preview.16-cadence.3 / Input 0.1.0-preview.9-cadence.3。新增独立高清机会覆盖分析槽全部被借用的情况。优先转换仍有效的已分析候选，以便同帧配对。没有这样的候选时，转换最新合格的相机帧并分配新的唯一序号，识别槽释放后恢复同一帧同时转换两路。该场景有测试先失败后通过，一加最终候选也观察到一次分析无槽且高清单独发布成功。保留原生 GPU 每次提交的一条兼容完成日志，新增高清独发计数。Android ARM64/API26 原生库共有 828 个动态定义、47 个公共 C/Unity 导出、145 个导入和 9 个动态依赖，与发布库逐项一致。

真实业务的头像裁剪在 Update 读取当前已绘制的配对，RuntimeView 在 LateUpdate 合成下一组。新增回归用真实人体图、实际识别结果与 D3D11 栅栏，验证 SDK 接收新结果时上一组纹理和关节仍相互对应，以及拍照使用的 FramePresented 在合成后提供新一组精确身份和相机原色纹理。回归在会使头像跳过有效画面的限制下失败，保留当前配对的接口行为后通过。

最终 APK 在 40.0°C 电池起点、热状态 0 的一加稳态窗口得到 30.01 解码、29.18 高清转换、26.32 Unity 实际观察、29.28 原生分析、23.34 新结果和 23.23 高清配对 FPS。按客户 HUD 口径的 InputPublicationFps 窗口采样均值是 26.36 FPS。670/673 个有效结果完成高清配对，其中 670 个输入身份核对全部通过。实际预览和配对为 1920×1080，提交为 512×288。解码到有效结果 P50/P95 为 123.43/141.39 ms。最长配对间隔 318.35 ms，发生在采集截图的同一秒，次长 151.20 ms。本轮保留完整窗口的最大值。无人且关闭骨骼窗口获得 25.94 FPS 高清预览与 25.12 FPS 新结果，0 有效人体骨骼，335 次输入身份核对无不一致。

## 一加 512×288 CPU 回读验证

公开 CPU 模式保持原条件。直接启动该模式会提交 1280×720，无法覆盖本轮目标的 512×288 分路。专用本地验收 APK 通过回环 RTSP 意图显式启用分路与已有的 512×288 identity-model-size-rgb 回读。APK 按实际输出报告 backend.ort.cpu / android-cpu-nohands。测试 APK 中的回读条件覆盖不进入 SDK/Input 候选包，独立工程的托管文件在构建后恢复为候选原样。

在这条路径上各运行 90 秒。控制 APK 从同一验收 APK 单独换入发布库，重新签名后校验 ZIP 中只有 libhumanvision_input.so 内容不同。对比 ready 后第 15–75 秒，有 1570 / 1551 个可核对的独立 native 槽按分析发布、观察到 snapshot fence、归还顺序完成，归还非零、队列拒绝和过期回调均为 0。两轮观察到的最大待回读数均为 3，配置上限 4。回读均值 98.95 → 98.99 ms，分析发布到观察归还中位数均约 33.36 ms。此路的原生高清转换 23.36 → 29.11 FPS、Unity 观察 23.39 → 25.85 FPS、原生分析 28.64 → 29.11 FPS。高清让行 312 → 0 次，最长配对间隔 299.79 → 199.77 ms。热状态 3 的采样较少/较多分别为 13 和 51 个，新结果 17.21 → 15.31 FPS，不作为温度相同的推理性能结论。日志在 android-readback-control / android-readback-candidate。

最终 APK 使用 UnitySkills 8091 对应的 Unity 2021.3.45f1 构建。当前编辑器运行 InputAdapterTests（25/25）和针对识别纹理覆盖相机的用例（1/1）。新增配对时序回归、93 项相关 EditMode 与 3 项 Windows PlayMode 另在独立团结引擎编辑器运行。Input 56/56、公共接口/架构/组件目录、15 项打包回归及候选包 692 文件/374 GUID 闭包校验通过。候选包产物、SHA256SUMS.txt 及候选源码清单保存在工作树 out/rtsp-preview-cadence-20261118/local-candidate-v3。

最终 cadence.3 回读复测与控制组电池起点为 36.6°C、36.8°C，系统热状态均为 0。在两组热状态均为 0 的 ready 后第 15–45 秒，Unity 观察高清为 23.74 → 25.94 FPS，原生分析 28.57 → 28.92 FPS，结果 18.23 → 17.66 FPS，高清配对 16.76 → 17.66 FPS，最长配对间隔 167.77 → 133.23 ms，解码到有效结果 P50/P95 为 233.78/261.62 → 236.44/258.92 ms。与控制组相比，新结果低约 3%，结果时延的变化约为数毫秒。最终 APK 的 60 秒窗口另外核对了 1033 个输入配对和 1532 个 fence 后归还的 native 槽，身份/尺寸和归还错误均为零，待回读最多 3/4。记录见 android-final-readback 的 summary.json、similar-thermal-summary.json 与 readback-summary.json。

最终 APK 从 1920×1080 直播断流后，同一地址切换到 Exercise-11 的 1280×720 降采样片段。首次试验的编码器每 8.33 秒才产生关键帧，连接时触发启动关键帧超时。改用已核对每秒一个关键帧的本地片段后，同一候选 APK 重新连接并移至代次 3，恢复 1280×720 解码、实时预览和精确合成，模型仍收到 512×288。Reconnecting 窗口没有显示旧配对，恢复后重新显示有效骨骼。原始失败与通过记录分存 android-final-scenarios/reconnect 与 android-final-geometry/reconnect。

Unity 和一加覆盖了共享的 MediaCodec/Vulkan 输入、HD 同帧底图、512×288 GPU 快照/CPU 回读、槽位回收及代次切换。本轮无 RK3588 设备接入，因此未测 Rockchip Vulkan 驱动、RKNN 运行时、客户整机的热状态与 NPU 后端吞吐。客户场景的具体 5 FPS 损失占比仍无法从 HUD 单独量化；旧调度可以出现高清持续让行，并且可在一加用同一路日志逐段证实。

工作流的改进点是先用单次诊断 APK 验证实际后端、原生是否分路以及回读尺寸，再冻结回放时长、温度和场景。纯原生调度 A/B 复用同一 APK 的托管内容，只替换 Input 库。生产候选再合并已验证的托管修复并一次构建。先核对实际调用方的 Update/LateUpdate 时序，可以减少对既有配对契约的试探。
