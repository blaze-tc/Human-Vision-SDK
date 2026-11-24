# 跨栏王升级 Human Vision SDK 0.4.1 的执行提示词

将下面分隔线后的内容复制给负责跨栏王业务工程的 Codex，执行 SDK 升级、业务回归和候选构建。

---

请在 Unity 工程 `E:/UnityProject/YS-Sensory game project/Sensory-Game-2021.3.45` 中，将跨栏王升级到 Human Vision SDK **0.4.1** 和 Human Vision Input **0.1.1**，两包来源都锁定 **`v0.4.1`**。先核验 [GitHub 正式发布](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.1) 的 tag 提交、附件和 SHA-256。保持独立 RTSP、原图预览、SDK 直接取点和 Ready 同帧拍照，重点验证 RK3588 的真实宿主冷启动。完成本地候选 AAR 和联调宿主 APK，保留回退材料，正式业务交付另行确认。

## 1. 核对并保留可回退基线

- 读取适用的 `AGENTS.md`、现有构建说明和项目布局。先确认 Unity 是否正在 Play、构建或编辑未保存场景。归档本次会触及的文件、现用 AAR/APK、包/模型清单及哈希至 `Logs/HumanVision-0.4.1-upgrade/<时间>/`。
- 编写提示词时实查工程使用 Unity **2021.3.45f1**，`Packages/manifest.json` 和 `Packages/packages-lock.json` 均指向本地 Android 交付中的 `com.blazetc.humanvision-0.4.0-local-android.tgz` 与 `com.blazetc.humanvision.input-0.1.0.tgz`。执行时重新读取两份文件，核对 Unity 实际加载的 `PackageInfo`、native 插件路径和模型根目录。备份 `Assets/StreamingAssets/HumanVision/Runtime`、`SdkDefaults`、场地配置、现用 `AarVisionBuildConfig`、Player/Graphics 设置及用户保存配置。
- 保留 `Assets/Scenes/Init.unity`、`Assets/Scenes/Setting.unity`、`Assets/Scenes/Ready.unity`、`Assets/Scenes/Games/HurdleKing.unity` 的业务流和美术布局。跑跳、举手、起跳时机、赛道、计分、排行榜、头像、成绩与宿主回调继续使用既有合同。验收 Demo 可用来对照 SDK 用法，业务工程继续使用自己的场景、控制器和渲染器。
- 阅读项目的 `Logs/HumanVisionAarStartup-20261124/给SDK工程的回归定位与修改说明.md`。本次 SDK 修改涉及 Android Vulkan 输入初始化和设备资源生命周期。现场原因与是否恢复启动，以实际故障 APK、原始堆栈及本次对照结果为准。

## 2. 成套更新 SDK、Input 和设备资源

公开 Windows/Android 路线将下面的两项依赖合并到 `Packages/manifest.json` 的 `dependencies` 中，并由 Unity 生成锁文件。保留其余业务依赖。

```json
{
  "com.blazetc.humanvision.input": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.1",
  "com.blazetc.humanvision": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.1"
}
```

- 主线目标包含 **RK3588 Neural**，因此宿主候选使用随 **0.4.1** 交付的完整本地 Android **RKNN-ON SDK 替代包**和配套 **Input 0.1.1**。本机配套目录为 `E:/Project/Human Vision SDK/out/releases/v0.4.1-local-android/`。读取其中 `README.md`，根据 `SHA256SUMS.txt` 和 `local-addon-manifest.json` 验证 `com.blazetc.humanvision.input-0.1.1.tgz` 及 `com.blazetc.humanvision-0.4.1-local-android.tgz`，先安装 Input 再安装完整私有 SDK，使用原 package id。公开 SDK 的 RKNN-OFF 版本可用于公开 GPU 路线。两种 SDK 安装路线分别验证，单个工程会话内各包只解析一份。
- 核对 SDK、Input、`librknnrt.so`、模型/Profile/manifest 的实际路径及逐文件 SHA-256。以新交付清单为准，检查 `Assets`、嵌入式 Packages 与 UPM 有效导入项，留一份适用插件。旧私有包、单独拷贝的 SO、旧模型安装器和 `PackageCache` 都只用于核查与回退。私有资源及其清单按原交付范围保存在本地。若缺本次配套私有包，先完成公开路线、构建准备和可做的回归，记录缺少的包或资源及预计影响。
- 若 Runtime 仍由旧 `staged-runtime.json` 固定，先把旧 Runtime 整体备份到 `Assets` 外，再按新包的部署文档使用对应安装器。核对实际 `RuntimeRoot` 和新清单，保留场地配置、用户保存配置和业务数据。更换已被 Editor 加载的 native 前保存项目并正常退出 Unity，换包后冷开，确认无重复插件和编译错误。

## 3. 核对业务接入，按实际差异最小修改

- `Assets/Scripts/Application/AppRuntime.cs` 和 `VisionGameSettings.cs` 负责已有单一 SDK owner 与串行启动。宿主业务配置由 `Assets/SensoryAarTool/Runtime/HostedVisionPolicy.cs` 合并。检查每次合并和重新应用后的 `ExecutionPolicy`、`PreviewEnabled`、源、后端、质量和区域。生产 RTSP 使用 `IndependentRequired`、`OpenInputOnInitialize=true`、`UseModelSizedAnalysisInput=true`、`PreviewEnabled=true`、`AutoSelectRtspStream=false`。WebCamera 和 Video 使用各自兼容路径，保留现有用户配置。`SENSORY_RTSP_COMPAT_DIAGNOSTIC` 只用于明确标记的兼容诊断候选，检查该宏未进入正常业务候选。
- Setting 原图使用 `Sdk.GetColorImageTex()`、`TryGetInputFrame` 或 `view.GetLiveTexture()`，按实际 Texture 可见尺寸保持宽高比及区域映射。分析图用于内部识别。`VenueSettingsSkeletonRenderer` 和玩法 `PrefabPose` 继续在 `Update` 读取 `TryGetJointNormalizedPosition`、图像或世界坐标的有效值，保留动作本身已有的去重和时间逻辑。保持普通绘制不依赖 `TryGetAnalysisInputFrame`、匹配帧或 FPS 阈值。状态输出区分 `PreviewError`、输入错误、SDK `LastError` 和实际 `RuntimeDiagnostics`。
- 普通预览 `PresentationMode=Disabled` 以关闭持续配对。`Assets/Scripts/Scenes/Ready/ReadyPhotoCapture.cs` 继续按需、串行临时使用 `MatchedColor`，核对配对颜色与骨骼的来源、代次、帧身份及 tracking ID，存取 500×500 头像，完成、取消或退出时恢复显示模式并等待在途 GPU 写入/读回完成。使用现有单接收源、单 SDK 和宿主退出屏障。复用现有实现时可以保留原代码，通过回归证明合同。

## 4. 重建 Unity AAR 和隔离宿主 APK

1. 使用既有 `Assets/Editor/SensoryAarTool/AarBuildWindow.cs`、`AarBuildPipeline.cs` 和 `VisionBuildSnapshot.cs`。检查项目级实际选择的 `Assets/SensoryAarTool/Configuration/*.asset` 与构建快照。现有 `Vision_NPU_Low.asset` 供 RK3588 Neural/Low 基线，`Vision_GPU_Low.asset` 可用于 GPU 对照，按实际运行目标选择。记录 `AccelerationMode`、`InputQuality`、图形 API、实际 RTSP 来源及渲染设置。后端/模型配置变更后重新构建 AAR。
2. 核对 Android ARM64、IL2CPP、Vulkan 优先、minSdk 26+、网络权限与原宿主协议。按现有构建链导出本地候选，正常宿主流程使用 `hostTesting=false`。带模拟动作/面板的联调候选独立命名并记录 `hostTesting=true`。
3. 读取 `E:/UnityProject/YS-Sensory game project/HostApp/README.md`。在隔离验证副本使用本次候选 AAR，按该项目的 `build.ps1 -Offline` 构建测试 APK，正常宿主与专用联调变体用各自匹配的 AAR。构建成功后立即保存对应 APK 副本，保留 AAR、APK、实际打包快照、构建日志和 SHA-256。保留原生产目录和原交付文件。
4. 从最终 AAR 与 APK 提取库和资源，逐级比较实际 ARM64 `libhumanvision*`、Input、`librknnrt.so` 及模型清单。保存 APK 中这些 ELF 的 `llvm-readelf --notes` 输出及 BuildId。分别记录工程选择、构建包字节和真机加载证据。检查宿主 `SensoryGame.getGames(context)`、启动、返回大厅和再次启动是否保留原协议。

## 5. 先对照 SDK 路径，再验 RK3588 宿主

- 在同一台获授权连接的 RK3588 上，先用配套本地 NPU SDK 构建的验收 Demo，依照 [SDK 启动诊断说明](../../tools/test/NPU_STARTUP_DIAGNOSTICS.md) 运行 A/B/C。三组固定同一显式 RTSP、TCP/镜像、Neural/Low、模型资源和实际图形 API，完整停止后依次冷启。现有 `hv_npu_startup_case` 是 SDK 验收 APK 的入口，业务宿主只使用其实现中实际存在的诊断开关。

  | 组别 | RTSP 执行策略 | 原图预览 |
  | --- | --- | --- |
  | A | `IndependentRequired` | 开 |
  | B | `IndependentRequired` | 关 |
  | C | `CompatibleUnityInput` | 开 |

- 每次启动前开始 `adb logcat -v epoch`，从进程创建前保留到正常退出或故障后。记录 Unity 启动、SDK 初始化、首输入、首原图、首结果、退出和再启动，诊断 Demo 归档 `HV_NPU_STARTUP`、`HV_INDEPENDENT_PROGRESS`、启动 JSONL 与可读的模块映射。发生崩溃时保留 crash buffer、可获取的 tombstone、故障 APK SHA-256、设备系统、实际图形 API、匹配实际库 BuildId 的符号定位。真实 RTSP 地址由现场提供，报告对外导出前脱敏，并记录权限限制和证据缺失情况。
- 在正常配置的联调宿主 APK 重走 `Init`、`Setting`、`Ready`、`HurdleKing`、计分/返回大厅、再开一局。正常路径须有持续原尺寸 Texture、实际 `backend.rknn`、有效骨骼、无崩溃、可退出和可重入。若异常复现，再按 A/B/C 相同控制变量构建隔离业务诊断候选。C 组可参考现有 `Assets/Editor/SensoryGame/HumanVisionCompatCandidateBuild.cs`，先核对它的历史输出路径和本次设置。仅关闭预览或使用兼容 RTSP 恢复启动时，保留该对照结果和业务预览未通过状态。
- 分开记录源解码/原生 live 发布、Unity 原图 Texture 更新、分析接纳/推理完成、Unity 新结果、有效骨骼、结果年龄和游戏帧耗时。使用 `TryGetSourceProgress` / `TryGetRuntimeProgress` 的累计值并按同一来源/代次/配置纪元求差，Unity 预览按实际新帧计。记录 `HurdleVisionMetrics` / `HurdleMotionDiagnostics`，同时观察真实游戏渲染负载。短时及长时各采样一轮，温度和 FPS 采用同一时间窗，读取不到标为未知。
- Editor 使用真实回放验证 Setting 预览和取点、重连、模型切换、Ready 照片及跨栏游戏逻辑。有目标设备时验证真实摄像头和宿主场景。拍照前后按同一源记录稳态原图、有效骨骼和游戏 FPS，确认照片结束后预览继续更新。分别报告 SDK Demo、Editor、宿主、OnePlus GPU 和 RK3588 的测试结论。当前设备缺失时将对应硬件项目标为未验证。

## 6. 交付报告与回退

提交中文报告，列出升级前后版本及正式 tag 提交、包/模型/native 清单及哈希、改动文件、实际构建配置、测试设备和实测后端、逐项通过/失败/未验、诊断日志及候选 AAR/APK 绝对路径和 SHA-256。写明恢复原 SDK/Input/Runtime 资源、配置及 AAR/APK 的步骤。仅输出已核验的本地候选，等待用户决定正式业务替换与交付。

使用说明见 [安装](../UPM_INSTALLATION.md)、[Runtime Prefab 与 Update 取点](../user-guide/UNITY_RUNTIME_PREFAB.md)、[SDK 配置与 API](../user-guide/SDK_CONFIGURATION.md)、[本地 Android 部署](../user-guide/LOCAL_ANDROID_CPU_YOLO.md)。
