# SDK 自有 NPU 启动 A/B/C 诊断

本工具对应业务故障说明：`E:/UnityProject/YS-Sensory game project/Sensory-Game-2021.3.45/Logs/HumanVisionAarStartup-20261124/给SDK工程的回归定位与修改说明.md`。只定位 SDK 自有验收程序的启动路径，不改业务工程、不替换正式 tarball、不自动发布。Windows 参数/编译检查和非 RK3588 设备上的拒绝结果不能算 RK3588 通过。

## 从 Demo 界面采集

普通 RuntimePrefabAcceptance 在左栏通过「输入连接」打开 UGUI 弹窗。RTSP 页填写完整地址并选择传输或执行配置。本地相机页可以刷新设备、选设备和设置请求尺寸、帧率与镜像。选择「应用并连接」生效，识别和配置保存使用「识别与显示设置」。

在普通 RuntimePrefabAcceptance 界面的左侧「实时状态 / 日志」面板点击「一键采集日志」。尚未连接、连接失败或正在识别时均可使用。每次点击会生成一份 JSONL 快照，记录点击时的 SDK 和输入状态，在面板显示实际保存路径并复制到剪贴板。RTSP 地址在诊断日志中脱敏。

Android 优先将快照写入「下载 / HumanVisionLogs」，公共目录不可用时保存到应用目录。若应用在按钮可用前就退出，按下文在启动前开始采集 logcat，并保留故障堆栈。

## 入口与隔离条件

`RuntimeIndependentAcceptanceProbe.cs` 原来已有 `hv_independent_execution=native|compatible` 和 `hv_independent_preview_off=true|false`，足以配置执行策略/预览，但普通 RPA 会先检测多个后端，且自动 fixture 限于 loopback。不能拿该预检过程当作一次独立的 Neural 冷启动。

新增入口必须明确设置 **`hv_npu_startup_case=A|B|C`**：

| Case | ExecutionPolicy | PreviewEnabled | OpenInputOnInitialize | UseModelSizedAnalysisInput |
| --- | --- | --- | --- | --- |
| A | IndependentRequired | true | true | true |
| B | IndependentRequired | false | true | true |
| C | CompatibleUnityInput | true | true | true |

三组统一使用 `Neural/Low`、明确的同一个 `hv_acceptance_fixture` RTSP URL、`hv_npu_startup_tcp`、`hv_npu_startup_mirror`。不改人数、区域、置信度、年龄或模型，不换 URL、不打开 WebCam fallback、不启用自动选子码流。

该 opt-in 在 `AfterSceneLoad`、RPA `Start` 前禁用本场景 RPA 自动控制器，跳过其无输入多后端预检。它复用唯一的生产 `HumanVisionRuntimeView` / `HumanVisionSdk.Initialize`；发现已初始化/Busy 或不属于该 RuntimeView 的 SDK owner 时拒绝，不打开第二个会话。普通 RuntimeView 使用 `Disabled`，不持续抓同帧图。

不传此 flag 时，既有 Demo/交互/fixture 入口保持原流程，不运行 `RunNpuStartup`、不由探针禁用 RPA。此独立诊断没有普通 Demo UI，不应将它用作布局、头像或游戏业务验收。A/C 的图像证据取实际 SDK Texture 的尺寸及帧号前进，B 没有预览是请求结果，不是故障。

当前冻结 Probe 只在启动阶段保存实际首张 Texture 的信息，**没有增加持续 Texture/有效点计数**。原生 progress 与 first Texture 足以划分本次 startup；持续图像/骨骼需由主流程另在 Unity 主线程通过 `Sdk.TryGetInputFrame` 的身份/编号和 `TryGetJointNormalizedPosition` 或现有正常 Demo/inspect 观察并归档。不得把首次有图、NativeFreshFrames 或回调次数当成持续 Texture 帧率/有效骨骼点数。

## 参数与冷启动

先由主流程构建/核验包含匹配 private NPU SDK、Input 和完整模型闭包的验收 APK。只 stage 新 Probe 到测试工程原有位置，保留 meta/GUID；无需替换 RPA 或业务脚本。`stage_independent_probe.py` 已能携带该 Probe，当前正在使用的项目也可只精准更新它，避免无关文件重放。

Android 参数：

- `hv_npu_startup_case`：必须为 A/B/C，其他非空值明确拒绝。
- `hv_acceptance_fixture`：明确的 `rtsp://` 地址。此 opt-in 可使用设备实际可达的非 loopback 地址；无有效地址就拒绝，不默认打开相机。
- `hv_npu_startup_tcp`：默认 true。三组必须相同。
- `hv_npu_startup_mirror`：默认 false。按业务现场填值，三组必须相同。
- `hv_independent_target_fps`：建议三组同为 30。
- `hv_acceptance_stage_seconds`：初始化成功后持续观察秒数，默认 60，限制 1–3600。然后调用并等待真实 `Sdk.Shutdown()`，输出关闭起止标记，不把超时当释放完成。
- `hv_independent_hash_loaded_modules`：默认 false；设 true 时对 `/proc/self/maps` 中可读的相关 SO 文件计算 SHA-256。哈希/I/O 会增加诊断开销，三组应使用同一设置；不能把这种观测耗时当作未加探针的 SDK 启动耗时。

不要混入停顿、CPU 压力、GPU passes、禁用消费者等其他实验 extras。首次仅跑上述三组，分别冷启动和归档。

以下仅是交给已获设备操作授权的主流程执行的命令模板；本工具的参数合同 runner 不会执行它。不要在报告/命令清单中展开真实 URL：

```powershell
$adbBin = '<Android SDK>/platform-tools/adb.exe'
$deviceSerial = '<同一台 RK3588 的 serial>'
$acceptancePackage = 'com.blazetc.humanvision.runtimeacceptance.independent'
$startupCase = 'A' # 第二、三轮分别为 B、C
# 在本地环境变量 HV_NPU_STARTUP_RTSP 中提供原 URL，不将值写入证据文档。
if ([string]::IsNullOrWhiteSpace($env:HV_NPU_STARTUP_RTSP)) { throw '缺少本地 RTSP 环境变量' }
& $adbBin -s $deviceSerial shell am force-stop $acceptancePackage
& $adbBin -s $deviceSerial shell am start -W `
  -n "$acceptancePackage/com.unity3d.player.UnityPlayerActivity" `
  --es hv_npu_startup_case $startupCase `
  --es hv_acceptance_fixture $env:HV_NPU_STARTUP_RTSP `
  --ez hv_npu_startup_tcp true --ez hv_npu_startup_mirror false `
  --ez hv_independent_hash_loaded_modules true `
  --ei hv_independent_target_fps 30 --ei hv_acceptance_stage_seconds 60
```

在启动前单独开始完整 `adb logcat -v epoch` 采集，不清除此前故障日志。存活组等 `shutdown_returned` 且 SDK Stopped 后再运行下一组；若崩溃/卡住，保存故障证据，记录未正常关闭，然后才由主流程结束该进程。不要并行运行三个 case，也不要在一个仍工作的会话上改 intent 冒充冷启动。

Editor 的纯参数检查无需启动 NPU。若确需测试编辑器诊断入口，可在既有 `Library/HumanVisionIndependentProbe.txt` 写同名键值再进入 Play；Windows 正常不支持 Neural，拒绝应如实记录。不要把 Windows 的拒绝变成 forced success。测试后移除显式 NPU case 配置，再验普通 8091 Demo。

## 日志、已加载 SO 和故障证据

Probe 在 `Awake` 先开日志；`initialize_begin` 在进入生产 SDK 协程前写入、Flush 并发送到 logcat。文件位于应用 `Application.persistentDataPath/RuntimePrefabDiagnostics/`，logcat 会报告完整本地路径：

| 文件/marker | 内容与边界 |
| --- | --- |
| `npu-startup-<UTC>.jsonl` / `HV_NPU_STARTUP` | case、单调 `elapsed_us`、UTC、请求/实际配置区别、三个布尔、Neural/Low、TCP/镜像、URL SHA-256、SDK/Input 状态、实际 profile、RuntimeDiagnostics、错误及预览错误、图形设备/API/fence 能力、实际 live Texture 尺寸/编号。 |
| `independent-progress-<UTC>.log` / `HV_INDEPENDENT_PROGRESS` | 原生/源累计计数、来源身份、callback 交付计数及错误；不可用为 null/false，不伪造零吞吐或 NPU 成功。 |
| `loaded-maps-<UTC>-<event>.txt` | 该阶段应用自己读取的完整 `/proc/self/maps`。权限/文件失败会明确记录 unavailable；没有 maps 不能冒充已确认加载。 |
| JSONL 中 `loaded_module` | maps 中可辨认的 humanvision/Input/RKNN/ORT/NCNN/Vulkan/Mali `.so` 路径，以及可选的实际可读文件哈希。不是仅根据 APK 声明推测已加载。 |

阶段为 `probe_awake`、`runtime_view_create_begin`、`options_configured`、`initialize_begin`、`sdk_state_observed`、`initialize_returned`、首次源获取/Texture/结果回调、`shutdown_begin/returned`。`startup_rejected*` 是明确拒绝。SDK 状态和“首次可见”是 Unity 观察时间；**不等同 native 内部 plugin-load、RTSP open、首轮 NPU execute 的精确起止**。如果最后只有 initialize_begin，需结合 native 日志/backtrace，不能凭这个标记指认崩溃函数。

Probe 不直接输出 URL；配置用 `url_sha256` 验证 A/B/C 是否保持同一 URL，错误/RuntimeDiagnostics 中的整个 RTSP URL也会遮蔽。完整 logcat/tombstone 仍可能包含其他组件的敏感内容，应本地保留原件、对外提交前脱敏，不截去故障前后的关键堆栈。

SO 证据需区分三层：

1. 已安装 APK 的 SHA-256 和其中 `lib/arm64-v8a/*.so` 的 SHA-256/ELF BuildId：证明包内实际字节。
2. Probe maps 与可读 backing SO 哈希：证明当时的映射路径及对应可读文件；若是 APK 内映射、文件不可读或崩溃发生得太早，相关哈希未知，不能用候选目录的哈希填补“已加载”。
3. 故障 tombstone/backtrace 的 BuildId：用故障 APK 对应的未剥离符号定位，核对其与上述真实库一致。

当前 Probe 的 `build_id` 明确为 null，没有假读 ELF；主流程需对从**该实际 APK**提取的库使用 NDK `llvm-readelf --notes` 等工具读取 BuildId，并保存工具输出。拿本地 SDK 同名 SO 的 BuildId 代替客户 APK 是无效证据。

由主流程归档：完整 logcat、crash buffer、可取得的 tombstone、APK/hash/包版本、全部 startup/progress/maps 文件、匹配实际库的符号化结果、设备型号/系统/GPU、同一 URL digest/TCP/镜像和三组结论。`adb pull` 使用实际日志报告的 `RuntimePrefabDiagnostics` 目录；受权限限制时可用已授权的 debuggable `run-as` 读取应用自己的文件。权限拒绝如实记录，不自动 root 设备。发生 native crash 前连 Probe Awake 都未执行时，只能靠平台日志与 APK/库证据继续定位。

## 无硬件参数合同与编译

`RuntimeNpuStartupOptionsContract.cs` 直接执行新 Probe 的生产参数方法：A/B/C、Neural/Low、三个布尔、显式 URL、TCP/镜像、识别配置不变、无效参数先拒绝、JSON 控制字符转义及完整 URL 遮蔽。它不创建 GameObject、不初始化 SDK/模型/来源，不启动 NPU，也不调用 ADB/Unity REST。

```powershell
py -3 tools/test/run_npu_startup_probe_contract.py `
  --unity-data 'D:/Developer/2021.3.45f1/Editor/Data' `
  --sdk-assemblies 'E:/UnityProject/Human-Vision-SDK-Test/Library/ScriptAssemblies' `
  --define UNITY_EDITOR --output 'out/npu-startup-options-editor-new'
```

`--sdk-assemblies` 必须指向匹配且已验证的 Input/Runtime/Demo DLL；测试会记录其哈希。另用匹配 Android 条件编译的 SDK DLL 目录、`--define UNITY_ANDROID` 和新输出目录编译检查 Android 路径。输出有 compile.log、run.log、receipt.json，拒绝覆盖旧目录。成功只说明参数/序列化与编译通过，不代表 Activity、RenderLive、已加载 SO 或 RKNN 执行通过。

现有 `run_independent_runtime.py` 的自动 fixture runner 只支持 cpu/gpu 与它自己的 loopback fixture，`run_unity_independent_runtime.py` 也是旧 CPU 停顿 gate；没有将它们改名冒充本次 NPU A/B/C 采集。真实三组由主流程用上面的显式入口执行，保留失败记录和硬件未验项。
