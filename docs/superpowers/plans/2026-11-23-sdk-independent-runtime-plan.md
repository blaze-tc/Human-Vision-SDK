# SDK Independent Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 复用现有 Host，让 SDK 在可用的原生输入路径中不依赖 Unity 帧循环完成输入、预处理、CPU／GPU／NPU 推理和结果发布；先在CPU／GPU／NPU共用异步方案上交付，再按瓶颈选择RK3588 RGA图像服务优化。

**Architecture:** 进程内原生 SourceAdapter 接入现有 RuntimeSession coordinator／Host。完成通知驱动 canonical 发布；Unity 保持主线程控制／显示和原有异步纹理兼容入口，预览不反压识别。

**Tech Stack:** C++17、版本化 C ABI、现有 CMake／GoogleTest、Unity 2021.3.45f1 与 2022.3 兼容基线、现有 ORT／DirectML／NCNN／RKNN 固定版本、Android ARM64 API26、受 BSP 条件约束的 RGA。

**Spec:** [独立运行设计](../specs/2026-11-23-sdk-independent-runtime-design.md)。[MediaPipe 参考核查](../../reports/2026-11-23-mediapipe-async-runtime-analysis.md)。

**Status:** 2026-11-23 用户批准后已进入实施。任务1、1B、2、3、4、5和9的共同基础及RTSP原生输入已落地，任务6最终复审及回归通过，任务11已完成Windows桌面功能子集：CPU／DirectML六组合、交互Editor57项、可见视频／骨骼和配置／生命周期。RGA硬件服务、原生文件／相机driver和硬件长时矩阵尚未完成；本计划不授权发布。当前范围和逐项证据以[验收记录](../../reports/2026-11-23-sdk-independent-runtime-acceptance.md)为准。

## Global Constraints

- 保留旧 V1 C ABI、语义 ConfigV1／StatsV1／Submit／Copy、Plugin ABI1／2／3 的布局、旧符号与语义。
- C++17；Android ARM64 API26；Unity 2021.3.45f1 与 2022.3 兼容基线；不得引入仅 Unity 6 可用的 Awaitable 依赖。
- Unity API／对象只能在合法 Unity 线程访问；自主生产不调用 Unity graphicsQueue 或等待其下一帧。
- 复用 RuntimeSession、RuntimeHost／GpuRuntimeHost、BackendFactory、BodyServices，不增加重复推理系统。
- 一个 session 一种 active ingestion owner；latest pending=1；source／分析／预览池分别默认3槽；canonical journal 默认64条。
- source_id／generation／frame_id／config_epoch／region_revision／clock 与变换随帧保存；不依赖 frame_id%64 补查身份。
- 无逐帧 JSON、无无界队列、无热路径逐帧 Task／buffer 分配、无默认实时优先级；暂停／重配时才允许重新分配。
- CPU／GPU／NPU共用调度与结果发布；图像服务与推理后端分别查询实际实现，NPU使用Vulkan准备／回读时不得报告为不使用GPU。
- 模型／阈值／预处理质量不随线程改造更改；RGA、共享 I/O、native GPU producer 各自独立 gate。
- 真实 1–8 人、手部语义与逐人完整骨骼30FPS目标保留；本计划不能补证尚未通过的模型或设备能力。
- 硬件验收由用户负责；无设备、skip、模拟器／mock、编译成功均不计为实际设备通过。

## Review Focus

1. 最后一帧推理完成但不再 submit／Copy／Stats：仍主动语义发布，见任务2。
2. Unity 不读预览／结果，且并发停止或换源：不得耗尽 decoder／分析池或回调已释放对象，见任务3、6。
3. 非紧密YUV／PRIVATE、正fence、RGA不支持的stride：明确拒绝或选合法fallback，见任务7、8。
4. 慢推理跨越64个输入帧、反复seek／重连／配置变化：身份不串帧，见任务1、10。
5. callback复用结果、日志慢读者、多个SDK session：不可变快照、显式overrun、资源准入，见任务2、3、11。

## 任务边界与依赖

任务1→1B→2→3为共同基础（共12个可审阅任务）。任务4给出第一个可运行闭环；任务5、6完善Unity接入。任务9先交付通用原生图像服务并接CPU／GPU／NPU；任务7、8是随后可选的RGA能力验证及优化，不作为RKNN异步调度或自主供帧的前提。任务10扩展来源，任务11联合验证。每个任务都先测试预期失败、实现最小改动、跑集中回归、更新对应 README／CHANGE_MAP／状态证据，再提交已验证变更。

### 任务1：固定帧 envelope、时钟与租约合同

**Files:** 新增 `runtime/common/frame_envelope.h`、`tests/runtime/test_frame_envelope.cpp`；修改 `runtime/host/runtime_host.h/.cpp`、`tests/runtime/test_plugin_host.cpp`；沿用 `native/src/core/latest_frame_slot.h/.cpp` 的旧复制合同。

**Interfaces:** 内部 `FrameEnvelope` 按设计第5节定义；Host job 必须持有 envelope 的值或稳定租约。旧 `HV_RuntimeSubmit` 仍在返回前复制 caller 像素。

- [x] 先写 `FrameEnvelope.DelayedCompletionBeyond64Frames`、`OriginalMetadataSurvivesReplacement`、`ClockDomainsAreExplicit`、`LeaseReleasedExactlyOnce`；旧ABI布局／导出快照同时固定。
- [x] 运行集中测试，确认新合同尚未实现而失败；不能用固定样例结果模拟真实推理通过。
- [x] 将 source／epoch／revision／变换与job绑定，移除新路径对环形旁表补查身份的依赖，保留旧入口行为。
- [x] 跑新增测试、LatestFrameSlot 与 RuntimeHost 回归；不将跨库时钟数值直接相减当延迟。
- [x] 更新 Host／frame 合同文档与准确命令结果，提交此任务。

### 任务1B：版本化图像服务与 Pipeline prepared-input 扩展

**Files:** 新增 `runtime/include/humanvision_image_service_v1.h`、`runtime/common/image_transform_contract.h/.cpp`、`tests/runtime/test_image_service_abi.cpp`、`test_prepared_image_contract.cpp`；修改plugin registry与composition显式注册，不更改旧Plugin ABI表。

**Interfaces:** 按设计第6节固定 `HV_ImageServiceApiV1` 的QuerySupport／SubmitTransform／WaitTransform／CancelTransform／ReleasePrepared，以及独立query `HV_QueryPipelineImageExtensionV1` 返回的DescribeTransform／ProcessPrepared；使用现有pipeline opaque instance，单worker串行调用。plan／prepared携带identity、变换、契约fingerprint及显式租约释放；prepared处理只能在真实完成后进行。

- [x] 先写新C header可由C／C++编译、size/version拒绝、旧插件无扩展仍可运行、lease取消不早退役、重复／缺失归一化、plan／ModelPack／epoch不匹配拒绝测试。
- [x] 确认接口缺失而失败；实现CPU图像服务oracle与一个当前pipeline扩展作为可替换实例，Host不出现模型／厂商分支。
- [x] 将DescribeTransform→服务执行→ProcessPrepared接入同一Host job；无扩展时只做合法CPU源转换后调用旧process，不跳过原预处理。
- [x] 跑ImageServiceAbi、PreparedImageContract、旧Plugin ABI1／2／3、YoloRgbPreprocess／TensorPipeline及架构guard，确认旧结构／符号快照不变。
- [x] 更新插件开发与组件合同，提交；RGA／native GPU任务只实现服务表或来源适配，不各造协议。

### 任务2：让完成通知驱动 canonical 发布

**Files:** 修改 `runtime/host/runtime_host.h/.cpp`、`runtime/host/gpu_runtime_host.h/.cpp`、`runtime/composition/session.h/.cpp`、`tests/runtime/test_runtime_session.cpp`；新增 `runtime/common/observation_journal.h`、`tests/runtime/test_observation_journal.cpp`。

**Interfaces:** Host 内部完成通知携带 job envelope 和结果 owner；coordinator 等待输入或完成。`ObservationJournal` 默认64条，`ReadAfter(cursor, caller_buffer)` 返回独立sequence／lost_count；这是内部接口，公开包装在任务3定义。

- [x] 先写 `RuntimeSession.LastCompletionPublishesWithoutPolling`、`NoUnityConsumerStillPublishes`、`SamplingDoesNotIncrementFresh`、`ObservationJournal.SlowReaderReportsOverrun`、`BodyAndHandSequencesRemainDistinct`。
- [x] 确认旧版最后一帧／无轮询场景失败；扩展现有真实插件 fixture，而不是只测mock计数。
- [x] 完成记录唤醒现有coordinator，由其单独更新BodyServices；满完成槽时限制新job admission，禁止等待Unity。GPU已有完成路径合流，不重复接受结果。
- [x] 保留 Copy 的 coherent raw／sampled语义，caller buffer 复制；native callback 只提交记录，不在Host mutex内执行用户逻辑。
- [x] 跑 RuntimeSession、RuntimeHost、RuntimeGpuComposition、CommonServices 和 journal 回归，记录后提交。

### 任务3：串行生命周期与增量控制 API

**Files:** 新增 `runtime/composition/runtime_controller.h/.cpp`、`native/include/humanvision/humanvision_runtime_control.h`、`tests/runtime/test_runtime_control.cpp`；修改 `runtime/composition/runtime_c.cpp`、`session.h/.cpp`、`runtime/CMakeLists.txt`；扩展ABI测试。

**Interfaces:** 建议 `HV_QueryRuntimeControlApiV1` 返回带size/version的新表及独立opaque `HV_RuntimeControllerHandle`。固定操作为 `CreateDeferred`、`RequestStart`、`RequestSource`、`RequestPause`、`RequestResume`、`RequestStop`、`RequestReconfigure`、`RequestClose`、`TryRelease`、`Copy`、`GetRequestStatus`、`GetCapabilities`、`GetStatsV3`、`ReadObservations`。CreateDeferred只复制有界配置，不加载设备／模型；Request返回request_id；TryRelease在owner及资源完全退出前返回BUSY，成功后handle失效。Copy保留旧coherent／sample语义；controller handle不得传给旧HV_Runtime函数。原Create／Destroy合同不变，新Unity路径使用新表。

- [x] 先写CreateDeferred无重操作、Start途中Close／取消、TryRelease在所有owner退出前BUSY、Pause清pending／晚结果拒绝、Stop超时隔离资源、永久vendor调用故障、正常退出／Editor reload、100次重建、双ingestion拒绝及旧Create／Destroy兼容测试。
- [x] 确认现有一次性Session不能直接当可恢复暂停使用；以可取消controller管理其实例重建／保留模型策略，不对终态slot直接Reset。
- [x] 在专用control owner执行可能等待的操作；不持发布锁join worker；新停止不依赖Unity pump。驱动未完成的超时进入ErrorRetiring，TryRelease保持BUSY，禁止冲突context和卸载；重配失败状态与显式恢复按设计第10节实现。
- [x] 跑 RuntimeControl、RuntimeSession、RuntimeHost、ABI1／2／3／旧导出回归，校验并发与资源准入。
- [x] 更新公开兼容与生命周期文档，提交。新增API需先经C布局测试，再提供C# wrapper。

### 任务4：原生输入协议与 Windows CPU 自主闭环

**Files:** 新增 `native/input/include/humanvision_input_v2.h`、`runtime/composition/source_binding.h/.cpp`、`tests/input/test_input_frame_lease.cpp`、`tests/runtime/test_native_source_binding.cpp`、`tools/benchmark/independent_runtime_harness.cpp`；修改 `native/input/src/input_session.cpp`、两个相关CMake目标。

**Interfaces:** Input V2表提供 `AcquireLatest(after_sequence, timeout, cancel)` 与 `ReleaseLease(token, completion)`；lease描述图像／时钟／所有权，Input库不依赖Runtime。composition adapter由新controller管理，映射clock／envelope并提交其持有的唯一session。

- [x] 先测无Unity DLL／graphics context时原生RTSP持续供帧及canonical publish，latest替换、满池、跨时钟、断流恢复；`IndependentRequired`不允许Unity fallback。
- [x] 先红后绿接Windows decoder CPU RGBA；不先上传Texture2D再readback。source在停读、退出和重连时可取消等待。
- [x] harness支持同源实时播放、关闭Unity消费者、分离输入／推理／发布计数；模型来自有效现有profile，缺模型应失败并报告。
- [x] 跑 NativeSourceBinding、InputFrameLease、RuntimeSession及现有RTSP focused tests；用真实本地RTSP整合fixture验证像素／序号。
- [x] 更新 Input／composition README，记录命令、模型与源哈希，提交Windows CPU闭环。

### 任务5：Unity 薄适配器与异步兼容输入

**Files:** 修改 `upm/com.blazetc.humanvision/Runtime/Demo/Input/HumanVisionInputAdapter.cs`、`Demo/Sdk/HumanVisionSdk.cs`、`Runtime/HumanVisionManager.cs` 对应实际包内路径；同步 `unity/HumanVisionDemo/Assets/HumanVision/` 源镜像与meta，新增 `HumanVisionIndependentRuntimeTests.cs`；保留Input包三类source。

**Interfaces:** Unity公开配置保留原AccelerationMode／初始化／ResultUpdated；新增语义执行策略与实际能力查询。Unity驱动仅保留texture交接，不再给自主源逐帧Submit。

- [x] 先写双生产者拒绝、ResultUpdated仍主线程、初始化失败可见、重配后旧callback丢弃、禁Update但native计数继续、异步readback lease归还测试。
- [x] 借鉴MediaPipe异步提交与结果快照交接；优先使用已有source事件／完成回调，禁止每次读图等待推理。不能在后台直接调用VideoPlayer／WebCamTexture API。
- [x] 对Unity texture明确“安全交接后自主”；停止source、销毁纹理、异步回读异常不访问已退役资源。保留请求FPS、方向、region、来源代次和原采样接口。
- [ ] 使用现有Unity Test Runner跑InputAdapterTests、SDK生命周期与RuntimeView相关测试，分别覆盖Windows／Android条件编译、2021.3／2022.3。
- [x] 同步两套源码并核验字节／GUID，更新SDK_CONFIGURATION与用户示例，提交。

### 任务6：预览旁路与按需精确同帧

**Files:** 新增 `runtime/composition/preview_mailbox.h/.cpp`、`tests/runtime/test_preview_mailbox.cpp`；修改Input输出fan-out、`HumanVisionRuntimeView.cs`、`PresentationTextureHistory.cs`及其现有测试。

**Interfaces:** 三种预览策略按设计第9节；预览pool与analysis pool独立，Unity取得SDK-owned预览资源，不持decoder AImage到下一Unity帧。

- [ ] 先测预览不读2000ms、preview copy实际完成被延迟、共享RGA／GPU队列拥塞、所有preview槽占满；固定copy在途≤1且至少保留2个source credits，credits不足时不另持decoder lease。另测帧代次、exact取消与停止；设备queue挂起须转ErrorRetiring，不伪称仍正常。
- [ ] 实现native owned copy／旁路最新预览；分析优先，有pending分析不追加preview，copy有实测profile预算，满池／超预算直接跳过。不通过强制释放未完成fence“解卡”。平台不支持自主预览则准确报告。
- [x] 精确拍照固定一个pending和独立buffer，按身份完整匹配原色图；不匹配返回不可用。
- [x] 跑PreviewMailbox、InputSplitSchedule／FrameRing、RuntimeView／InputAdapter相关回归，并用图形环境查真实同帧像素。
- [x] 更新预览／拍照文档和指标，提交。

### 任务7：RK3588 RGA 实际能力 gate

**Files:** 新增 `native/input/src/android/rga_image_service.h/.cpp`、`tools/test/rk3588_rga_gate.cpp`、`tests/input/test_rga_image_contract.cpp`；扩展 `docs/maintenance/RK3588_RGA_ASSESSMENT.md`，不立即改变默认路径。

**Interfaces:** `ImageTransformPlan`属于来源中立描述；pipeline给出计划，RGA service只执行图像原语。gate返回Supported／Unsupported／NeedsDevice及逐项原因，不把dlopen成功当通过。

- [ ] 先写PRIVATE不可猜layout、stride／色彩错误拒绝、正acquire fd所有权、RGA错误／超时不可标完成测试。
- [ ] 建立普通应用身份的BSP库可见性／依赖／版本receipt；逐项核实真实AHB importer、真实分配格式、querystring／imcheck、同步和allocator。
- [ ] 对held-frame执行RGB／tensor oracle及pose对照；覆盖crop、4:3／16:9、方向、601／709、range和resolution-change，先冻结误差合同。
- [ ] 没设备时仅完成host合同与可构建gate，输出NeedsDevice；不允许自动选择A路径。实际gate由用户目标板验收。
- [ ] 将receipt、失败原因与A1可否进入下一阶段写入RGA文档，提交可复现gate。

### 任务8：RK3588 A1 自主 RGA＋RKNN 与 A2 可选共享内存

**Files:** 修改MediaCodec source与新RGA image service、`runtime/plugins/pipeline/yolo/yolo_tensor_pipeline.cpp`、`runtime/plugins/backend/rknn/rknn_android_vendor.cpp`；扩展任务1B的 `tests/runtime/test_prepared_image_contract.cpp` 与现有RKNN／RGB preprocess测试。

**Interfaces:** A1先可做源CSC／方向转换并走旧pipeline缩放；模型尺寸直出使用任务1B定义的DescribeTransform／ProcessPrepared及prepared指纹，保留RKNN已验证UINT8输入语义；A2通过独立能力扩展绑定明确分配的设备内存，不改变既有Backend ABI表。

- [ ] 先测无Unity初始化／持续运行、颜色几何与mask等价、buffer在NPU完成前不复用、跨epoch晚结果拒绝；A1不依赖GPU回读。
- [ ] 接A1；保留有界copy并统计成本。若A gate失败，继续使用任务9的合法原生Vulkan→staging或可读YUV→CPU路径，不阻塞RKNN；不能锁PRIVATE伪造CPU fallback。
- [ ] A1正确后单独实施A2实验：真实layout／stride／cache／fence合同，单帧完整输出对照和同机净收益。不通过则保留A1并报告原因。
- [ ] 跑RknnBackend、YoloRgbPreprocess、YoloTensorPipeline及Input生命周期测试；ARM64构建与目标板数据分开记录。
- [ ] 更新RKNN／pipeline README与本地资源索引，保持vendor分发边界后提交。禁止只因GPU利用率下降就宣布骨骼FPS达标。

### 任务9：通用原生图像服务与 CPU／GPU／NPU 供帧

**Files:** 新增 `native/input/src/android/native_vulkan_image_service.h/.cpp`、`native_vulkan_readback.h/.cpp`、`tests/runtime/test_native_gpu_producer.cpp`、`test_native_readback.cpp`；抽取现有 `unity_input_vulkan.cpp` 可复用的纯转换部分，保留Unity adapter；修改 `runtime/plugins/backend/ncnn/ncnn_vulkan_backend.h`／`ncnn_android_session.cpp` 与来源上下文适配；扩展ORT线程策略测试。

**Interfaces:** 来源中立的device identity／AHB lease／producer completion，分别由Unity producer与native producer实现。原生服务提供RGBA AHB出口给NCNN，以及自有host-visible staging出口给CPU／RKNN；copy fence完成并确保CPU cache可见后才发布。NCNN仍消费已验证RGBA AHB，不直接将PRIVATE/YUV当RGBA。

- [x] 先测没有Unity device时初始化、正sync-fd import／失败所有权、native producer跨代次、consumer慢／停止、未知GPU明确拒绝。
- [x] native owner创建自己的device／queue／command／fence／输出pool，复用shader合同及NCNN import。新增原生异步staging回读，不使用Unity AsyncGPUReadback；测试Fence与非coherent invalidate之前不发布、失败不发布、CPU／NPU读者未完成不复用，以及无RGA时CPU／RKNN仍可运行。不能在后台借用Unity queue冒充独立。
- [x] Windows DirectML接原生CPU输入并测session Run串行；CPU统计并限制ORT／XNNPACK／preprocess总worker，禁止嵌套线程池占满所有核。
- [ ] 跑NativeGpuProducer、NativeReadback、VulkanDeviceIdentity、AHB／NCNN合同和CPU线程策略回归；用实际Mali／Adreno／Windows设备各自验收，不互相代替。
- [ ] 报告线程自主性与GPU负载竞争两组结果，更新组件文档后提交。

### 任务10：原生本地视频与相机 source

**Files:** 建议新增 `native/input/src/sources/file_source.*`、`native/input/src/android/camera_source.*`、`native/input/src/windows/camera_source.*`；新增相应 `tests/input/test_file_source.cpp`、`test_camera_source_contract.cpp`。

**Interfaces:** 复用Input V2帧租约与SourceBinding；文件driver使用PTS pacing，camera driver使用实际plane／方向／时间来源合同。每个平台source作为单独可验收提交，未实现组合保持Unsupported。

- [ ] 先测seek／loop／EOF生成新generation、30FPS素材不按离线极限送帧、断源取消、权限拒绝／撤销、设备占用／拔插、stride／rotation／timestamp不伪造。
- [ ] 先原生文件，再Android平台相机，再Windows平台相机；保留Unity入口，不自动把用户VideoClip／相机所有权切换走。USB相机未暴露时给出明确能力原因。
- [ ] 按平台正常权限流程接入；不实现vendor传感器SDK替代，不增加新编码／音视频同步承诺。
- [ ] 各source通过harness无Unity闭环和同帧像素；相机实际能力由设备验收，mock只证明协议。
- [ ] 更新来源能力表和原生／兼容模式示例，分别提交。

### 任务11：业务负载、长时验收与候选发布准备

2026-11-23桌面功能checkpoint已完成：556项原生回归、57项交互Editor测试、Windows CPU／DirectML低中高六组合及基础操作通过。已有15／30／60限帧、预览开关和消费者暂停的真实计数；不据此勾选下列包含硬件压力、100次生命周期、真实人数及长时吞吐的完整矩阵。用户已暂缓真机安装。

**Files:** 扩展 `tools/benchmark/independent_runtime_harness.cpp`；新增 `tools/benchmark/analyze_independent_runtime.py`、Unity独立验收场景中的负载开关和受控停顿工具；更新 `docs/DEVELOPMENT_STATUS.md`、维护文档、package验证与兼容索引。

**Interfaces:** 统一receipt包含commit／库／模型／素材hash、实际source／image-service／backend／能力级别、温度频率、输入／fresh／逐人完整／Unity delivery／render FPS、age与drop；不使用逐帧JSON热路径。

- [x] 先建立基线与数据完整性检查：不把同一sequence、多次sample、不同人数／模型或解码包率混作新结果FPS。
- [ ] 按设计第12节执行无Unity、禁消费者、500／2000ms停顿、15／30／60限帧、CPU／GPU分别施压、预览开关／慢读者、100次生命周期与1／2／4／6／8真实人数矩阵。
- [ ] 先5分钟预热再30分钟持续，记录最慢10秒；轻量控制建议5% fresh FPS／20ms age p95增量门槛，其余完整报告退化。缺设备项明列，绝不代填通过。
- [ ] 跑集中回归与架构／组件目录；准备候选发布时才跑完整native／Unity／包闭包／动态依赖／接口／GUID检查。SDK与Input配套，不提前改默认／上传vendor资源。
- [ ] 更新准确命令与结果，提交可审查候选；正式合并／发布按用户后续指令与既有发布合同处理。

## 集中验证入口

以下是集中验证入口；已执行的准确命令、数量及环境见验收记录。新增测试须接入CMake discovery，不能因filter零匹配而当通过。

```powershell
pwsh -File tools/test/run_native_tests.ps1 -Filter 'FrameEnvelope|ImageServiceAbi|PreparedImageContract|RuntimeControl|RuntimeHost|RuntimeSession|RuntimeGpuComposition|ObservationJournal|NativeSourceBinding|PreviewMailbox'
pwsh -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
```

`run_native_tests.ps1` 当前固定 `HV_USE_DIRECTML=OFF`，因此不能用它的通过证明DirectML。DirectML必须按现有工具链另配置已启用的build目录并实际运行同源测试。Unity与设备gate使用现有Test Runner／独立测试工程；先发现当前编辑器／设备状态，不占用用户正在运行的业务工程。

## 下一步边界

用户后续已明确批准修改，并指定独立验收工程。实施位于隔离工作树，兼容模式仍为默认；没有合并、推送或发布。用户随后要求“暂不进行真机安装”，因此本轮不安装候选APK，硬件项保留待验收。逐项复核余下RGA、来源扩展和长时性能gate，不用本地编译或Windows结果替代目标设备数据。
