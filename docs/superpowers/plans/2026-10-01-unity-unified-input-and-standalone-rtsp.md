# Unity Unified Input and Standalone RTSP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unity统一获取视频、摄像头和RTSP Texture；画面可独立预览，识别通过可选适配器消费同一帧。

**Architecture:** 独立`com.blazetc.humanvision.input`包和`humanvision_input`平台插件，禁止反向依赖识别库。Android RTSP硬件解码后经Vulkan转换进入Unity纹理；现有ncnn桥只由识别适配器接入。Windows复用已有FFmpeg解码，公共骨骼API/V1 ABI不变。

**Tech Stack:** Unity2021.3.45f1实际验证基线、C#、C++17、FFmpeg7.1-1.5.11锁定来源、Android API26/ARM64、MediaCodec、AImageReader/AHB、Unity Vulkan、uGUI。

**Spec:** [已批准设计](../specs/2026-10-01-unity-unified-input-and-standalone-rtsp-design.md)，初版设计提交`5a94798`；用户2026-10-01确认，随后明确追加三个Demo及公共/模式配置。本计划已纳入该要求，设计优先于本计划。

## Global Constraints

- SDK骨骼API、V1 C ABI、Canonical Skeleton、Tracker、Profile/ModelPack保持稳定。
- Android NCNN显式选择、fail-fast；Vulkan/API26/ARM64；无整帧CPU回读及ORT fallback。
- Android RTSP首版H.264/RTSP TCP。UDP/H.265不宣称已支持，硬件失败不走软件解码。
- 不进行模型替换、Hand/QNN/RTMO或ORT/Windows推理性能优化；不合并main/发Release。
- 仅安装input包即可预览：无模型、HumanVisionManager、Runtime Host、ORT/ncnn依赖。
- Unity对象/Texture生命周期仅主线程；GPU拷贝/转换仅render event；复用纹理、AHB导入及事件数据。
- 源纹理只等待source-copy fence退休；已拷贝inference槽由Runtime持有；Close不得等待inference。
- 当前矩形YOLO仅已验证16:9横屏。其他输入可预览；识别必须明确拒绝，不能拉伸/默默裁剪。
- WebCamTexture时间是UnityObserved；RTSP接收/解码时间及PTS不得冒充sensor time。R4 sensor gate继续未通过。
- fresh FPS按完整observation frame计，不按人数累加；空帧/部分人数必须保留；25过渡/30硬目标。
- 先保存并声明当前dirty R4快照；不reset/clean、不改现有用户项目。独立worktree执行并保存源/二进制hash。新计划代码不假装当前dirty native快照已经能由cleanHEAD重建。
- 本计划用户已确认并开始逐Task执行；保持用户已选择的fresh implementer + spec compliance + quality review、GPT-6.1 Sol medium。每Task独立commit，review不通过不得前进。

## Review Focus

1. 快速Video→Camera→RTSP切换及Close后迟到回调：只新generation可发布，旧纹理安全退休（Task1/2/8/9）。
2. 请求1080p但设备输出720p、180度旋转或前置镜像：用实际契约，预览和识别一致，拒绝未验证模型形状（Task2/6/9）。
3. RTSP断线、无关键帧、队列积压：主线程可操作，受控重连等待关键帧，凭据不出日志（Task3/4/7）。
4. PRIVATE AHB/external-format或sync-fd能力缺失：报具体能力错误，绝不偷用CPU解码/无fence（Task5/6/7）。
5. 无SDK/模型、识别慢或adapter detach：独立预览继续，source-copy退休与inference槽解耦（Task1/8/10）。

## File roots and implementation ownership

下文标为input包的路径均相对`upm/com.blazetc.humanvision.input/`；SDK Demo/Tests相对`upm/com.blazetc.humanvision/Runtime/`，SDK Samples~/Editor/package.json相对SDK包根目录。Unity镜像相对`unity/HumanVisionDemo/Assets/HumanVision/`，含相同Runtime/Demo/Editor职责，不能用复制整个dirty目录代替明确文件迁移。每个新增Unity资源同时提交独立`.meta`。
SDK当前UPM缺少Android GPU支持；Task8/9必须对齐已验证生产所需的`Runtime/Android/HumanVisionAndroidRuntimeSelection.cs`、`Runtime/Android/HumanVisionAndroidGpuFrameBridge.cs`、`Runtime/HumanVisionNativeApi.cs`、`Runtime/HumanVisionManager.cs`及必需native frame结构，Android conditional compile逐项红绿验证。不能将这次测试APK等同于Git包已支持GPU。未验证R4诊断/fixture/shader一律不打包。

## Deliverables and order

三段可独立验收交付，按A→B→C→D串行：A=统一输入包（Task1–2），B=Windows独立RTSP（Task3–4），C=Android硬件RTSP producer（Task5–7），D=识别适配/兼容打包/真机验收（Task8–11）。C真机格式/同步gate失败停止，不伪装通过或提前进入D。每Task只拥有声明文件，先RED再GREEN、独立review、明确add、commit并更新状态证据。

### Task1: 独立输入契约及GPU资源退休

**Files:** Create `upm/com.blazetc.humanvision.input/package.json`, `Runtime/HumanVision.Input.asmdef`, `Runtime/IHumanVisionFrameSource.cs`, `Runtime/HumanVisionSourceSettings.cs`, `Runtime/HumanVisionTextureFrame.cs`, `Runtime/SourceRetirement.cs`, `Runtime/InputMonotonicClock.cs`, `Tests/EditMode/HumanVision.Input.Tests.asmdef`, `Tests/EditMode/FrameContractTests.cs`; Create `tools/test/run_input_tests.ps1`.
**Interfaces:** Namespace `HumanVision.Input`. `InputSourceState {Stopped,Opening,Streaming,Reconnecting,Error,Closing}`; `InputKind {Video,WebCamera,Rtsp}`.
`IHumanVisionFrameSource.Open(HumanVisionSourceSettings settings)`, `Close()`, `State`, `LastError`, `CurrentTexture`, `bool TryGetLatestFrame(long afterFrameId,out HumanVisionTextureFrame frame)`.
`HumanVisionTextureFrame`值类型：`ulong SourceId, Generation`; `long FrameId, PublishedTimestampUs, PresentationTimestampUs`; `Texture Texture`; `int Width,Height,AppliedRotationDegrees`; `bool AppliedMirrorX`; `FrameRowOrigin RowOrigin`; `FrameColorSpace ColorSpace`; `FrameTimestampKind TimestampKind`; `ulong ResourceToken`。
`FrameRowOrigin {UnityBottomLeft,NativeTopLeft}`；规范源输出为正向UnityBottomLeft，Applied字段为已经应用的变换，消费者不得再次旋转；TimestampKind区分UnityObserved/LocalDecode，PTS仅独立字段。
`FrameTimestampKind {UnityObserved,LocalDecode}`；`FrameColorSpace {Srgb,Linear,Unknown}`。`ISourceCopyFence.IsComplete`只表示copy的GPU完成，不包含inference。
资源接口`bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame,out SourceCopyLease lease)`；`SourceCopyLease.RetireAfter(ISourceCopyFence fence)`。源Close不接收inference fence，borrowed预览纹理只在当前generation有效。

- [x] RED：`FrameContractTests.OldGenerationCannotPublish`断言切源后旧回调无输出；`ClosingWaitsForCopyNotInference`用未完成copy fence阻止destroy，copy完成后即退休而不依赖模拟inference；`FrameMetadataUsesActualGeometry`请求1920×1080/实际1280×720返回实际值。
- [x] 建立隔离Unity测试runner：`pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -Output out/input/task1`；记录预期缺类型/契约失败，无sensor输入伪造。
- [x] 实现上述接口、generation/单调帧ID和退休队列，sourceId跨重连保持、generation提升；ResourceToken明确lease身份。添加包/asmdef和`.meta`，不得引用HumanVision.Runtime。
- [x] GREEN同命令：三个测试PASS；反射/依赖测试`InputAssemblyHasNoInferenceReferences`断言包不依赖ORT/ncnn/SDK。更新状态，spec+quality review后仅add这些文件，commit `feat: define standalone Unity input frame contract`。

Task1审查ruling：设计要求时间戳时钟域优先于上述简写字段列表；增加独立InputMonotonicClock、SourceTimestampUs/SourceClockDomain/SourceClockId，发布时钟固定InputMonotonic，源时间与PTS分离。实际Unity11/11PASS，SpecPASS/QualityPASS。

### Task2: Unity Video/WebCamera和独立预览

**Files:** Create input包 `Runtime/VideoFrameSource.cs`, `Runtime/WebCameraFrameSource.cs`, `Runtime/FramePreview.cs`, `Runtime/FrameTextureNormalizer.cs`, `Runtime/Resources/HumanVisionInputOrientation.shader`, `Tests/EditMode/UnitySourceTests.cs`, `Tests/PlayMode/UnitySourceLifecycleTests.cs`。
**Interfaces:** Task1；`VideoFrameSource : MonoBehaviour,IHumanVisionFrameSource`使用VideoPlayer；`WebCameraFrameSource`同接口；`FramePreview.Bind(IHumanVisionFrameSource source)`仅绑定RawImage。`FrameTextureNormalizer.Update(Texture input,int rotationDegrees,bool verticalMirror,bool displayMirror)`复用正向RT并发布实际尺寸，不暴露模型尺寸。

- [x] RED：`UnitySourceTests.AsymmetricMarkerTransformsExactlyOnce`对0/90/180/270和镜像组合验证GPU彩色标记及Width/Height交换；测试夹具可诊断读回，生产禁止；`PreviewWithoutModels`无SDK/模型也能绑定实际VideoPlayer纹理。
- [x] Run `pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase UnitySources -Output out/input/task2`，缺源实现FAIL。
- [x] 实现MP4 VideoPlayer→RT、WebCamTexture→GPU normalization→RT。复用已验证视频回调/纹理方向，使用实际rotation/mirror，权限异步。纹理只契约变化时重建，暂停/恢复提升generation。
- [x] PlayMode测试`SwitchAndPauseRejectLateFrames`、`RequestedResolutionIsNotActualResolution`、`PreviewHasNoPerFrameManagedAllocationsAfterWarmup`；采集/预览与消费者速度无关。GREEN同命令，记录真实摄像头请求/实际分辨率，review后commit `feat: add independent Unity video and camera preview`。

### Task3: 独立Windows RTSP插件

**Files:** Create `native/input/CMakeLists.txt`, `native/input/include/humanvision_input.h`, `native/input/src/input_session.cpp`, `native/input/src/ffmpeg_rtsp_demux.cpp`, `native/input/src/windows_rtsp_decoder.cpp`, `tests/input/test_rtsp_session.cpp`; Modify `native/CMakeLists.txt`; Create `tools/package/build_input_native.ps1`。
**Interfaces:** 新独立C ABI：opaque `HV_InputHandle`；`HV_Input_Open(const HV_InputOptions*,HV_InputHandle*)`, `HV_Input_Close(HV_InputHandle)`非阻塞发起关闭、`HV_Input_Release(HV_InputHandle)`在copy/worker退休后销毁（未退休返回BUSY，handle继续可查询）、 `HV_Input_GetState`返回Closing直至worker/copy退休、 `HV_Input_GetLastError`, `HV_Input_PollFrame(HV_InputHandle,uint64_t afterSequence,HV_InputFrameInfo*)`，`HV_Input_CopyRgba`仅Windows新路由。结构含size/version、实际尺寸/sequence/generation/本地monotonic时间/PTS；禁止模型类型。旧`HV_Rtsp*`导出保持原库签名和语义，本Task不重定向旧入口或删除旧实现。

- [x] RED：`RtspSession.StalledReadCanBeCancelled`超时/Close有界并不占Unity主线程；`RtspSession.SequenceAndGenerationOnReconnect`；`ErrorsRedactCredentials`断言`rtsp://user:secret@host`不进入错误输出。
- [x] Run `pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Windows -RunTests`，缺humanvision_input目标FAIL。脚本使用已锁定FFmpeg来源，v143/Ninja Multi-Config；输出out/input-native/windows。
- [x] 复用`native/src/input/rtsp_source.cpp`中的已验证demux/decoder逻辑，独立sessionworker/缓冲；不得链接humanvision/ORT/ncnn/Runtime。后台解码RGBA后主线程上传成本明确。队列最新已解码帧，不任意丢P/B包。
- [x] GREEN同命令；CTest `ctest --test-dir out/input-native/windows -C Release -R input --output-on-failure`；动态依赖审计无ORT/ncnn/humanvision；旧RTSP ABI测试仍PASS。review后commit `feat: extract independent Windows RTSP input runtime`。

### Task4: Unity RTSP组件与Windows受控实流

**Files:** Create input包 `Runtime/NativeInputBindings.cs`, `Runtime/RtspFrameSource.cs`, `Runtime/RtspSourceSettings.cs`, `Tests/EditMode/RtspSourceTests.cs`, `Tests/PlayMode/RtspPreviewTests.cs`; Create `tools/test/run_rtsp_fixture.ps1`, `tools/test/rtsp_fixture_manifest.py`。
**Interfaces:** Task1/3；`RtspFrameSource : IHumanVisionFrameSource`，设置URL/Transport=TCP/OpenTimeoutMs=5000；状态/错误脱敏。地址仅本地设置，报告不存凭据。Windows在主线程复用Texture2D上传，FrameTimestampKind.LocalDecode。

- [x] RED：`RtspPreviewTests.PreviewWithoutInferencePackage`在只装input包的干净项目播放真实TCP H.264流；`ConnectionLossReconnectsWithoutBlockingControls`断开受控服务器后状态重连、恢复generation递增。
- [x] Run `pwsh -NoProfile -File tools/test/run_rtsp_fixture.ps1 -Platform Windows -Output out/input/task4`；没有RtspFrameSource时FAIL。受控服务器版本/来源/配置和MP4hash保存，仅本机监听；停止只本脚本创建PID，不碰其他服务。
- [x] 实现RtspFrameSource WindowsPoll/Copy/上传路径，输入插件缺失报可操作错误；使用已有工具缓存，缺工具按锁定版本补齐，不任意下载最新二进制。用`video-1.mp4`生成真正RTSP服务，不用VideoPlayer直读MP4冒充协议验收。
- [x] GREEN同命令；实流独立预览及重连PASS；请求/实际尺寸、状态序列、包依赖审计记录。review后commit `feat: expose standalone Unity RTSP preview on Windows`。

### Task5: Android MediaCodec/PRIVATE AHB实际能力gate

**Files:** Create `native/input/src/android/mediacodec_source.cpp`, `native/input/src/android/ahb_capabilities.cpp`, `native/input/include/android_input_gpu.h`, `tests/input/test_android_input_capabilities.cpp`, input包 `Tests/PlayMode/AndroidInputCapabilityProbe.cs`; Extend `tools/package/build_input_native.ps1` Android参数；Create `tools/test/collect_android_input_gate.ps1`。
**Interfaces:** Task3压缩H.264 packets；`AndroidDecodedImage`内部lease携带AImage/AHB、acquire-fd、generation、PTS/本地时间。`ProbeDecodedBuffer(AHardwareBuffer*,VkPhysicalDevice,InputGpuCapabilities*)`查询实际AHB format/externalFormat、sampled/YCbCr/features和sync-fd，decoder/AImageReader PRIVATE usage组合必须真机可用。

- [x] RED：能力单测`MissingExternalFormatFailsExplicitly`/`MissingSyncFdCannotAdmit`；真实MediaCodec→AImageReader PRIVATE + GPU sampled probe未实现时不能标记PASS。
- [x] Run `pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests`；API26 ARM64符号/依赖审计，不链接ncnn/ORT/humanvision。
- [x] 实现异步MediaCodec hardware output Surface，AImageReader acquireLatestImageAsync；无CPU image planes/RGBA staging。先查询codec支持与实际buffer属性，不能把RGBA/TRANSFER_DST或任意usage写死。
- [x] Run `pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate Capabilities -Serial e7c07019 -Output out/input/task5-device`。Snapdragon888实流打印actual codec/format/externalFormat/usage/features/fence能力（分别验证physical支持和Unity logical device实际启用的YCbCr/sync能力），不含URL凭据。缺能力FAIL停止C，不走软件/CPU。review并提交Task5能力结果后，只有PASS进入Task6。

### Task6: Unity Vulkan解码buffer导入与GPU色彩转换

**Files:** Create `native/input/src/android/unity_input_vulkan.cpp`, `native/input/src/android/ahb_image_cache.cpp`, `native/input/src/android/yuv_to_rgba.cpp`, `native/input/shaders/input_yuv_to_rgba.comp`, `tools/shaders/build_input_shaders.py`, `tests/input/test_ahb_image_cache.cpp`, `tests/input/test_input_color_contract.cpp`。
**Interfaces:** Task5实际InputGpuCapabilities。`HV_Input_BindUnityTarget(handle,void* unityTexture,uint32_t width,uint32_t height,uint64_t generation)`仅记录target；`HV_Input_GetRenderEventFunc()`供Unity render event。VkImage通过Unity Vulkan AccessTexture获取，绝不把Surface/OES handle强转为VkImage。`InputImageCache.Acquire(AHardwareBuffer*,generation,contract)`按buffer身份缓存且跟踪bufferRemoved，`RetireBufferAfterFence`退休资源。

- [x] RED：`SameBufferReusesImport`断言连续100帧相同活跃buffer不create/import；`RemovedBufferWaitsForFence`；`YuvColorContract`彩色/灰阶参考验证BT.601/709和full/limited、裁剪、转向。
- [x] Run Task5 build命令及`ctest --test-dir out/input-native/android-host-tests -C Release -R input --output-on-failure`；尚无import/转换FAIL。host测试runner是Task5脚本创建的真实可执行target，不能把AndroidELF在host执行。
- [x] 实现只读sampled AHB→YCbCr conversion→Unity-owned RGBA RT GPU路径；external-format读取sampler conversion实际参数，目标storage/color attachment功能按设备查询选择。shader用仓库锁定NDK/Vulkan编译器生成并保存hash，禁止per-frame管线创建。
- [x] Run `pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate Color -Serial e7c07019 -Output out/input/task6-device`。诊断GPU标记/受控色彩帧可单独读回作为gate工具，生产整帧回读计数必须0。颜色/方向实际PASS才继续，review后commit `feat: import Android RTSP frames into Unity Vulkan textures`。

### Task7: 跨队列fence、ownership与bounded最新帧协议

**Files:** Create `native/input/src/android/input_frame_ring.cpp`, `native/input/src/android/input_gpu_sync.cpp`, `tests/input/test_input_frame_ring.cpp`; Modify Task5/6 producer/cache/render event；Create `tools/test/analyze_android_input_gate.py`。
**Interfaces:** 固定3个输出slot；状态`Free→Acquired→CopyQueued→Published→Retiring→Free`，buffer导入cache独立于slot。空slot缺失则最新已解码frame/drop；`InputGpuSync.WaitAcquireFdAndOwn`, `SignalReleaseFdAndReturnOwnership`在Unity queue操作；release fence供AImage_deleteAsync，AHB acquire/release各一对。

- [x] RED：`NoFreeSlotDropsWithoutRenderThreadWait`、`CloseBeforeCopyCompletesKeepsBufferAlive`、`ReconnectionRejectsOldGeneration`、`EncodedBacklogFlushWaitsForKeyframe`。检查acquire/release/fd所有权完整，禁止漏wait或直接销毁cache。
- [x] 同Task5构建/CTest；尚无ring/sync正确行为FAIL。实现wait/signal sync-fd、外部queue ownership transfer，render event只入队copy，绝不等待ncnn；错误/Close路径fence退休及fd释放一致。
- [x] 与input包RtspFrameSource连接：复用固定event data；RenderTexture主线程创建、供native GPU写入；PollFrame只读metadata，不读image。网络/decoder/输出队列有界，关键帧恢复受控，不随机丢压缩P/B。
- [x] Run `pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate Lifecycle -Serial e7c07019 -Output out/input/task7-device`，含60s播放、10次切换/断线/暂停、Close时在途copy。要求readbacks/errors=0、slot/cache/fd持有数回到基线，旧帧不发布。统计drops不当成功；review后commit `feat: synchronize and retire Android RTSP GPU frames safely`。

### Task8: 将SDK源copy退休与inference槽退休解耦

**Files:** Modify `runtime/gpu/android/unity_vulkan_bridge.cpp`, `runtime/gpu/android/unity_vulkan_plugin.cpp`, `native/include/humanvision/humanvision_android_gpu.h`, Unity `Runtime/Android/HumanVisionAndroidGpuFrameBridge.cs`, `Runtime/HumanVisionManager.cs`; Test `tests/runtime/test_unity_vulkan_bridge.cpp`；对应UPM只迁移已验证新公共桥方法，不混入未验证R4诊断。
**Interfaces:** 新增V2/internal窄接口`HV_AndroidGpuRetireSourceCopies(generation,out_copy_retirement_token)`和`HV_AndroidGpuPollSourceRetirement(token)`；V1 ABI原样保留。现有end-source接口保持原同步语义用于旧调用者，新adapter使用非阻塞退休token；Runtime持有已copy AHB/inference槽。具体token字段增加在V2/internal扩展，不能更改V1结构大小。

- [x] RED：`RetireSourceDoesNotWaitInference`在copy完成/inference未完成时返回可退休；`QueuedCopyKeepsSourceAlive`及`RetiredGenerationCannotPublish`。新增接口必须先断言实际native状态，禁止仅managed计时假装异步。
- [x] 构建affected host/Android，`ctest --test-dir out/input-sdk-host -C Release -R unity_vulkan_bridge --output-on-failure`（Task8建立隔离CMakebuild，复用仓库native测试配置）；现有copied-slot寿命检查PASS，新增契约初始FAIL。
- [x] 分离sourcecopy引用与inference引用；不同generation/copy fences计数独立，不删AHB acquire/release或sync-fd。主线程Close轮询退休，旧纹理由源Retirement持有到GPU确认。不得调用旧同步全量drain达到新Close。
- [x] 同命令GREEN+API26 ELF/public ABI审计；实际RTSP预览Close/adapterdetach不被慢inference阻塞。review后commit `feat: retire GPU input sources independently of inference slots`。

### Task9: 统一识别适配器和旧Demo/API兼容

**Files:** Create Unity `Assets/HumanVision/Demo/Input/HumanVisionInputAdapter.cs`, `Tests/EditMode/InputAdapterTests.cs`；Modify Demo `VideoPlayerFrameSource.cs`, `Live/HumanVisionCameraManager.cs`, `Live/HumanVisionLiveSource.cs`；对应UPM Demo同功能镜像。
**Interfaces:** Task1/8。`HumanVisionInputAdapter.Bind(IHumanVisionFrameSource source)`/`Detach()`；只消费generation/actual geometry/Texture，调用既有提交/骨骼API。FramePreview无需adapter/manager。旧StartCamera、GetColorImageTex、Video路径方法保留签名并转发新source；旧RTSP导出保持。

- [x] RED：`UprightPreviewAndInferenceShareCoordinateContract`对asymmetric标记只做一次旋转/镜像，native top-row normalization显式一次；`UnsupportedPortraitPreviewContinuesButInferenceFails`；`AdapterDetachKeepsPreviewRunning`；`SlowInferenceNeverThrottlesPreview`。
- [x] Run `pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Adapter -Output out/input/task9`；缺适配器FAIL。明确NCNNfail-fast/profile选择不变；ORT兼容模式仅显式既有路径，不优化/静默切换。
- [x] 实现adapter/compat wrappers，并用独立preview Texture和已验证GPU输入行契约，lease结束按Task8copy-only退休。禁每帧JSON/Task/分配；模型MaxBodies设置与source无关，Region仍bbox/pelvis assignment。
- [x] GREEN实际Windows/Android conditional compilation及公共API反射测试；分别编译真正UPM来源，不能用Unity-source测试替代旧CPU-only UPM GPU迁移。未批准R4代码不混入包；review后commit `feat: adapt unified Unity frames to stable skeleton APIs`。

### Task10: 独立输入包、三个演示场景与干净导入

**Files:** Create input包 `Samples~/InputPreview/InputPreview.unity`, `Samples~/InputPreview/InputPreviewController.cs`, `Editor/InputPreviewSceneBuilder.cs`, `Documentation~/INPUT_GUIDE.md`；Create SDK Demo `Demo/Input/HumanVisionDemoNavigator.cs`, `Demo/Input/SharedRecognitionSettings.cs`, `Demo/Input/DemoModeSettings.cs`, `Demo/Input/HumanVisionSharedSettingsPanel.cs`, `Demo/Input/HumanVisionModeSettingsPanel.cs`, `Demo/Input/HumanVisionSettingsStore.cs`, `Demo/Input/SharedSettingsPanel.prefab`, `Samples~/UnifiedInput/{HumanVisionCameraDemo,HumanVisionVideoDemo,HumanVisionRtspDemo}.unity`, `Tests/EditMode/DemoSettingsTests.cs`；Modify SDK `package.json`, `Editor/HumanVisionCameraDemoBuilder.cs`及Unity镜像；Create `tools/package/package_input.py`, `tools/package/check_input_package.py`。
**Interfaces:** SDK三个场景CameraDemo/VideoDemo/RtspDemo；`HumanVisionDemoNavigator.SwitchTo(InputKind kind)`公共按钮切换并沿用Task1退休/generation协议。公共设置面板Prefab复用；`SharedRecognitionSettings`持有MaxBodies、Region列表/编号、AnalysisProfileId/ModelPack声明的分析尺寸；`DemoModeSettings`按InputKind存设备名/视频路径/RTSP URL、镜像、线宽/点径、采集请求参数。`HumanVisionSettingsStore.LoadShared/SaveShared`与`LoadMode/SaveMode(InputKind,DemoModeSettings)`独立文件/键，设置写入仅用户操作时JSON，不入每帧路径。input-only InputPreview仅RawImage/source选择，作为独立样例。CanvasScaler ScaleWithScreenSize reference1280×720/match0.5；请求720p/1080p/FPS和实际值分开，GUI隐藏不停源。

- [x] RED：`InputPackageHasNoInferenceDependencies`检查asmdef/native动态依赖/无模型；真实干净Unity Git/file导入只输入包成功，Preview场景独立运行。`DemoSettingsTests.SharedRegionsSurviveThreeSceneSwitches`验证公共Region/人数不丢；`ModeSettingsNeverOverwriteAnotherMode`验证三种模式镜像/9px/27px等独立；`AnalysisResolutionMustMatchModelPackContract`拒绝任意未验证尺寸。SDK依赖input包精确版本，Git安装先按文档解析两个URL或使用本地打包清单，不假定UPM自动解析同仓库另路径。
- [x] Run `py -3.13 tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input`、`pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Package -Output out/input/task10`，缺独立包/sceneFAIL。
- [x] 构建输入插件多平台包，manifest列hash/许可证/锁定source；nativePluginImporter平台设置正确、meta GUID唯一，权限仅Camera/Internet等所需。input-only包含FFmpeg依赖而不加载SDK库。三个Skeleton Demo沿用稳定骨骼调用，默认9px/27px各模式可配置。公共区域规划面板复用已有经验证拖拽/缩放/持久化，不改Region推理后assignment。分析分辨率仅切换已经验证的明确Profile/ModelPack，不改模型文件内容。576局部候选未完成物理语义门槛不得作为默认/正式已验收选项；首版可仅提供当前明确640契约，高清采集请求仍独立配置。
- [x] GREEN Editor、Windows player、Android APK三条真实构建与input-only无模型预览；1280×720/3840×2160/手机横竖safe-area GUI可点/滚动。只上传开发分支资源（获授权时），禁止main/Release。review后commit `feat: package standalone input and unified source demos`。

### Task11: 实流、摄像头、视频统一验收及维护交付

**Files:** Create `tools/test/collect_unified_input_acceptance.ps1`, `tools/test/analyze_unified_input_acceptance.py`, `docs/reports/2026-10-01-unified-input-acceptance.md`, `docs/maintenance/UNIFIED_INPUT.md`; Modify `docs/maintenance/START_HERE.md`, `docs/DEVELOPMENT_STATUS.md`。
**Interfaces:** Task2/4/7/9/10；每run固化commit/声明dirty快照/APK/native/model/profile/source hash、source/generation/frame sequence/clock provenance；60s采集、10s预热+固定40s窗口，含空/部分人数结果。

- [ ] RED：分析器拒绝counter重置/重复frame/result sequence、缺source证据、CPU读回、error或不足40s窗口；这些是计量fixtures，不能制造骨骼当真机结果。先运行`py -3.13 -m unittest discover -s tests/reference -p test_unified_input_acceptance.py`并记录预期FAIL（此Task创建该测试文件）。
- [ ] 按Video→WebCamera→RTSP顺序Run `pwsh -NoProfile -File tools/test/collect_unified_input_acceptance.ps1 -Serial e7c07019 -Mode <Video|WebCamera|Rtsp> -Output out/input/acceptance/<mode>`；Androidinput-only先跑，SDK接入后再跑。APK安装hash核对，缺真地址使用Task4真实受控RTSP流并标明局限。
- [ ] 自动分析fresh complete observation FPS、检测到人数帧数、P50/P95本地age、source FPS、bridge/decoder drops、copy/import/runtime错误和热状态；RTSP source PTS/local age分列，绝不冒充sensor E2E。25/30门槛未过继续标FAIL，不用prediction/人数相加改口径。
- [ ] 保存方向/左右镜像/动作跟随录像和用户结论；1人/多人来自用户实机或已指定video-1/video-2，25FPS录像不能证明30freshFPS。摄像头/RTSP未sensor-verified明确保留R4 gate；独立输入完成不等于整体30FPS目标完成。
- [ ] 独立facts/quality review重新从原始log和APK计算结果；更新维护入口、失败诊断/切源/元数据/包安装指南，commit `docs: record unified input verification and maintenance`。没有用户最终物理验收不合并main/发布Release。

## Self-review and handoff

设计各节映射：独立模块/API/无模型=1/3/10；统一采集/方向/高清=2/9；Windows RTSP=3/4；Android实际能力/YCbCr/fence/cache/队列=5/6/7；source/inference退休=1/8；API兼容=3/9/10；安全凭据/断线/切换=3/4/7；完整帧指标及用户验收=11。Review Focus五项均有明确测试。
接口名称一致：source接口仅Task1定义；native ABI仅Task3定义、GPU target接口Task6扩展；frame kind只UnityObserved/LocalDecode，未产生伪sensor域。版本、shape拒绝、profile显式选择和V1稳定不受新源改变。
三段独立交付防止先写ncnn输入再假装RTSP预览完成；11个Task是审查单位，不新增模型/Renderer性能任务。代码和设备gate尚未执行，checkbox全部保持未勾。
2026-10-01摄像头真实证据：40s固定窗口14.55freshFPS，后段日志10.4–11.0FPS；输入重构不能承诺使之达到30FPS。推理性能目标仍未完成，不将独立输入交付当作性能验收。
计划供用户书面审查；沿用已选Subagent-driven和GPT-6.1 Sol medium。确认本计划后开始Task1；Milestone内自动连续测试/review/修复/commit，设备gate需要用户配合时再停。
