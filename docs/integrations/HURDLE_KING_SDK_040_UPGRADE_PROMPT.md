# 跨栏王升级 Human Vision SDK 0.4.0 的执行提示词

以下内容可整体交给负责业务工程的 Codex。它描述待执行的接入任务，不代表跨栏王已经完成升级或 RK3588 已通过验收。

---

请在 Unity 工程 `E:/UnityProject/YS-Sensory game project/Sensory-Game-2021.3.45` 中，将 Human Vision SDK 升级到正式 **0.4.0**，Input 升级到正式 **0.1.0**。公开 Git 安装的两包固定在同一个标签 **`v0.4.0`**；本地 NPU 安装使用下文同版本的完整私有 SDK 替代包和配套 Input。完成现有跨栏王业务的适配、回归和候选构建。

目标发布入口：`https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0`。开始时核验该正式发布、tag 对应提交、包内版本及附件校验清单；不要用 `main`、预览版或未发布的本地候选代替正式版。如果正式附件或本地资源尚未交付，先完成可独立推进的盘点、备份和适配准备，明确记录缺项，不能把旧包改名当作新包。

本任务允许业务接入修改、自动测试、Unity 验证，以及独立候选目录中的本地 AAR 和宿主验证 APK 构建。**不要覆盖正式宿主工程中的生产 AAR、替换客户交付件、发布业务 AAR 或上传发布产物。**业务正式 AAR 的最终交付仍由用户明确授权；SDK 的正式发布授权不等于业务 AAR 的发布授权。先把差异、候选产物、SHA-256 和验收结论准备完整。

## 一、先核对当前工程，再做最小升级

1. 阅读业务工程及其上层目录适用的 `AGENTS.md`。检查工作区未提交修改、Unity 是否正在 Play/构建，以及用户尚未保存的场景。备份与本次修改相关的文件，备份和日志放入本工程独立的 `Logs/HumanVision-0.4.0-upgrade/<时间>/`，不要把用户正在编辑的内容回退成 SDK 示例。
2. 本提示词编写时的只读基线是 Unity **2021.3.45f1**，`Packages/manifest.json` 中 SDK 仍为 `0.4.0-preview.17`，Input 为 `0.1.0-preview.10`，均使用标签 `v0.4.0-preview.17`。再次核实当前实际版本；不能假设已装正式版，也不能将历史测试结果算成本次通过。
3. 至少备份并记录以下内容：
   - `Packages/manifest.json`、`Packages/packages-lock.json`、相关 asmdef、SDK 插件导入设置。
   - `ProjectSettings/HumanVisionAndroidRuntimeSettings.asset`、Player/Quality/Graphics/Build Settings。
   - `Assets/Resources/HumanVisionGame/SdkDefaults.asset`、场地设置与设备保存配置的原文件及版本。
   - `Assets/SensoryAarTool/Configuration/` 中当前选中的识别打包配置、AAR 工具项目级保存设置。
   - `Assets/StreamingAssets/HumanVision/Runtime` 的模型索引、profile、manifest，以及 Android/Windows 原生插件和 SHA-256。
   - 当前可回退的 AAR/APK 路径、哈希、宿主依赖及本次将编辑的业务脚本。
4. 先追踪真实调用链，至少检查：

   ```text
   Assets/Scripts/Application/AppRuntime.cs
   Assets/Scripts/Application/VisionGameSettings.cs
   Assets/Scripts/Scenes/Setting/SettingManager.cs
   Assets/Scripts/Scenes/Setting/VenueSettingsController.cs
   Assets/Scripts/Scenes/Setting/VenueSettingsSkeletonRenderer.cs
   Assets/Scripts/Scenes/Setting/VenueSettingsDiagnostics.cs
   Assets/Scripts/Scenes/Ready/ReadyPhotoCapture.cs
   Assets/Scripts/Scenes/Ready/ReadyManager.cs
   Assets/Scripts/Shared/UI/TrackingCameraManager.cs
   Assets/Scripts/Vision/PrefabPose.cs
   Assets/Scripts/Vision/Motion/RunAction.cs
   Assets/Scripts/Vision/Motion/JumpAction.cs
   Assets/Scripts/Games/HurdleKing/HurdleVisionMetrics.cs
   Assets/SensoryAarTool/Runtime/HostedVisionPolicy.cs
   Assets/SensoryAarTool/Runtime/AarVisionBuildConfig.cs
   Assets/Editor/SensoryAarTool/AarBuildWindow.cs
   Assets/Editor/SensoryAarTool/AarBuildPipeline.cs
   Assets/Editor/SensoryAarTool/VisionBuildSnapshot.cs
   ```

   继续追踪实际 Ready/头像、宿主启动/退出、成绩上报调用方，不以文件名替代代码检查。
5. 保留原有跑跳判断、分道/人数、举手加入、起跳时机、阈值、计分、排行榜、账号/头像缓存及宿主回调协议。保留场景布局、摄像机、灯光、材质、美术、排行榜标题和既有场景修复。不把 SDK 验收 Demo 的 IMGUI 或整个 Settings 示例覆盖进业务场景；复用业务现有 UI 和单一 SDK owner。

## 二、两包和本地 Android 资源必须配套

公开版 UPM 依赖固定如下；先解析 Input，再确认 SDK 编译。保留其他业务依赖，由 Unity 正常更新锁文件。需要本地 NPU 时按下面的私有 SDK 替代方案安装，不同时保留两份 SDK：

```json
{
  "dependencies": {
    "com.blazetc.humanvision.input": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0",
    "com.blazetc.humanvision": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0"
  }
}
```

仅将这两项合并到业务现有 `dependencies`，不要用这个最小示例覆盖整份 manifest。

- 核对解析后的 SDK `package.json` 为 `0.4.0`，其 Input 依赖为 `0.1.0`，Input 包内版本为 `0.1.0`；公开 Git 路线的两条依赖锁定同一正式提交。本地私有替代 SDK 则核对交付清单中的来源提交及实际包哈希与本次发布配套。若使用离线包，使用同一次发布的配套附件，UPM/Assets 安装方式只选一种。
- 排查 `Assets`、embedded Packages、UPM 缓存实际加载路径和插件导入设置，确认有效 SDK/Input 各一份。不要全删 `Library` 或删除用户模型来掩盖重复插件，也不要仅替换 C# 而保留旧 native。
- 本次本地 Android 配套的持久交付位置为 **`E:/Project/Human Vision SDK/out/releases/v0.4.0-local-android/`**，本次 SDK 工作树的同名相对目录也保留副本。正式交付目录应包含：

  ```text
  com.blazetc.humanvision.input-0.1.0.tgz
  com.blazetc.humanvision-0.4.0-local-android.tgz
  local-addon-manifest.json
  SHA256SUMS.txt
  README.md
  ```

  先读取 `README.md`，逐个计算两份 tgz 的 SHA-256，与 `SHA256SUMS.txt` 和 `local-addon-manifest.json` 的 `archives` 相互校验；核对 `source_commit` 与正式交付来源。不要使用 `040-local-candidate` 等候选目录的归档哈希代替最终清单。
- 完整私有 SDK 保留原 62 项 Android Runtime 资源并合并公开 Windows YOLO 的 12 项，共 **74 项 Runtime 文件**。核对 manifest 的 `merged_runtime_file_count`、`private_source_file_count`、`public_supplemental_files` 及逐文件来源/哈希，不能只验数量。核对 `sdk_native_sha256`、`input_native_sha256`、`input_windows_native_sha256`、`windows_native_sha256`、`rknn_runtime_sha256`、`public_sdk_native_sha256` 及包内的真实文件；Windows 库也必须使用最终交付清单的实值，不保留早期候选 DLL。清单中的本地/分发/硬件资格标记原样保留，不能把打包成功改写成 RK3588 性能或资源分发资格通过。
- **公开 SDK 的 RKNN-OFF 原生库与本地 NPU 的 RKNN-ON 原生库不是同一文件。**需要 NPU 时，在 Unity Package Manager 使用 **Add package from tarball**，先选上述 Input tgz，再选完整私有 SDK tgz。它替换原来那一条 SDK 依赖，保持同 package id、版本 `0.4.0`、脚本 GUID，并与 Input `0.1.0` 配套；不要把公开 RKNN-OFF 哈希误当 NPU 应用应加载的私有库哈希。
- **本次私有交付只有两份 tgz，没有私有 SDK `.unitypackage`。**原 Assets/offline SDK 布局需先盘点并备份，移除旧 SDK/Input 代码、旧原生插件及冲突安装项后迁移到上述 UPM 路线，保留业务脚本、场景、预设和用户配置并核对引用。禁止在仍生效的 Git/UPM SDK 旁导入第二套 Assets SDK/native；若以后确需 Assets 离线包，须另行制作并验证，不能假设本次已提供。
- 模型安装器会保留显式 staged 数据：旧 `Assets/StreamingAssets/HumanVision/Runtime/staged-runtime.json` 存在时，不能指望换包自动覆盖旧 62 项索引。先把旧 **整个 Runtime 目录连同该文件**备份移到 `Assets` 之外，保留 `SdkDefaults`、场地/业务设置与用户保存配置，再执行 **HumanVision → Install Packaged Models**，核对新安装的 74 项闭包和实际加载路径。检查显式 `RuntimeRoot` 不再指向旧副本。不要运行旧 preview.7/preview.17 硬编码旧哈希的 Enable Local 安装器来覆盖当前包；使用本次包内安装器及 PackageInfo 对应的审计资料。不要清空整个 persistentDataPath 丢失业务账号/设置。
- 旧提示词 `E:/Project/Human Vision SDK/out/releases/preview17-work/HurdleKing-preview17-upgrade-prompt.md` 可用于理解历史接入；其中 `preview.17` 资源包路径、库哈希及 `1920×1080` 预览上限不是本次正式版清单。
- `librknnrt.so` 仅保留一份有效插件，完整 RKNN 模型/profile 索引必须相互匹配。公开 Git 包与本地 CPU/NPU 补充资料的范围不同，按实际交付清单处理，不把“存在模型文件”当作运行后端可用。沿用已有资源使用/分发边界，不把本地补充资源另行上传公共发布。
- 如果正式版配套本地资源未提供，明确列出缺失和现有哈希，不混用旧包、不伪造 NPU 可用、不擅自换模型。可继续验证已具备的 Windows/Android GPU 路线。
- native 已被 Editor 加载时，先保存用户工作、正常停 Play/关闭该 Editor，替换后冷启动。不能假设 Asset Refresh 会卸载旧 DLL。备份完整，回退时 SDK、Input、补充资源和配置成套恢复。

## 三、保持单一生命周期，RTSP 使用原生独立模式

继续由现有 `AppRuntime` / `HumanVisionRuntimeView` 持有唯一 `HumanVisionSdk`，保持已有 UniTask/协程启动串行和宿主退出屏障。不要再挂第二个 `HumanVisionSettingsController`、第二个 Runtime Prefab，或另开一个 RTSP decoder 给预览。
SDK 配置/查询和 Unity 对象操作在 Unity 主线程完成，原生 worker 由 SDK 自己管理；不要把 `sdk.Initialize` 或公开查询塞进 `Task.Run`。

在**完成现有预设、宿主打包配置及用户场地设置合并之后**，统一形成最终 `HumanVisionSdkOptions`。RTSP 的新执行策略使用真实公开 API：

```csharp
// requested 是现有业务合并后的深副本；不新建一份空配置覆盖宿主选择。
requested.ExecutionPolicy = HumanVisionExecutionPolicy.IndependentRequired;
requested.OpenInputOnInitialize = true;
requested.UseModelSizedAnalysisInput = true;
requested.PreviewEnabled = true;
yield return sdk.Initialize(requested);
if (!sdk.IsRunning)
    Debug.LogError(HumanVision.Demo.HumanVisionSettingsController.Redact(sdk.LastError));
```

上述仅用于 `requested.SourceKind == HumanVision.Input.InputKind.Rtsp`。TCP/UDP、真实 RTSP 地址及凭据沿用用户/宿主配置，不硬编码测试地址，也不写入公开日志。

- `HumanVisionExecutionPolicy` 的实际枚举为 `CompatibleUnityInput`、`IndependentPreferred`、`IndependentRequired`。此任务 RTSP 主验收使用 `IndependentRequired`，让缺少原生能力或初始化失败明确暴露。需要对照时可显式切兼容模式，记录实际模式；不能悄悄回退或同时打开第二输入。
- WebCamera、VideoPlayer 当前仍使用原有异步兼容驱动。切这些来源时使用 `CompatibleUnityInput`；保存 RTSP 独立策略偏好，切回 RTSP 时恢复，不能把非 RTSP 的兼容选择覆盖掉 RTSP 偏好。
- 注意 `HumanVisionSettingsData.FromOptions/ToOptions` 的旧保存合同没有 `ExecutionPolicy`、`PreviewEnabled`。检查正式版实际代码及业务每个转换边界，在业务自己的持久化/配置合并层显式保留这两个字段；覆盖启动、Setting 应用/保存/重载、返回 Ready、宿主初始化和重新连接。不要只在第一次启动临时赋值，下一次 `ToOptions()` 又回到兼容模式。
- 最小适配是在现有单一启动/应用边界完成旧设置与 hosted profile 合并后，统一补回业务的 RTSP 执行策略及预览偏好；需要保存时给业务自己的设置模型补字段/迁移默认值。不要为了这两个字段修改 PackageCache 中的 SDK 类，也不要增加第二套设置 owner。保留原有后端、模型、人数、区域和宿主地址优先级。
- 现有 `HumanVisionSdkPreset.CreateOptions()` / `CreateOptionsForInput(kind, address)` 可生成深副本，`sdk.Configuration` / `ActiveConfiguration` 用于检查配置。保持 `InitializeOnStart=false` 的单 owner 显式启动方式；不要把多个协程/任务并发 `Initialize` 当成提高吞吐。
- 等待初始化完成后检查 `IsRunning`、`LastError`、`RuntimeProfile`、`ActiveAccelerationMode`。模型已安装、界面选中 GPU/NPU、`Running` 的瞬间截图都不能替代实际后端和新结果验证。异步冷启动期间正确显示“初始化中/等待首个结果”，不要连续重启输入。
- `StopSdk()` / `Shutdown()` 等待真实在途资源退役；沿用宿主退出与头像 GPU 回读屏障。超时不等于完成，不能强制销毁未完成 RT、重用槽位或卸载仍被使用的 native 库。

## 四、普通颜色 Texture 必须是当前码流原图

正式版普通 RTSP API `sdk.GetColorImageTex()`、`view.GetLiveTexture()`、`sdk.TryGetInputFrame(-1, out frame)` 返回**显式填写的当前码流解码后可见原尺寸**，包括既有旋转/镜像处理；不再把模型低档的小图当作彩色预览。原始可见尺寸与 codec 对齐尺寸要分清，例如画面 `1920×1080` 不应按 coded `1920×1088` 拉伸。

- 保留宿主给定的原始 URL。SDK 普通 RTSP 原图路径不因 `AutoSelectRtspStream` 或 `RequestedWidth/Height` 而静默切较小子码流/降分辨率。用户明确填写子码流 URL 时，就保留那个码流的原尺寸。
- 不要把默认值改成 `1920×1080` 就宣称实现“原图”；720p 应为 720p，4K 应按实际能力提供原尺寸，失败明确报告。不要在业务 RawImage 前另加缩小 Blit、`ReadPixels`、逐帧 CPU 拷贝或第二个解码器。保持宽高比，用实际 Texture 尺寸更新现有 Fit/letterbox 和区域叠加映射。
- SDK 内部已为普通 RTSP 转换成原图尺寸策略；不要向业务 `DemoModeSettings` 盲写内部 native 的 `0/0`，也不要修改模型 profile 的尺寸。低层 Input API 显式正尺寸上限另有原语义，不能拿它给普通 SDK 原图路径偷偷限到 720p/1080p。
- `TryGetAnalysisInputFrame` 是明确的分析图接口，允许模型尺寸，原生路径没有可借用的分析 Texture 时返回 `false`。`AnalysisInputSize` 是模型合同；`SubmittedAnalysisInputSize` 是实际送入识别尺寸，两者也可能不同。普通预览和人物坐标不得依赖分析 Texture 存在。
- 原图可选预览失败时显示 `sdk.PreviewError`，并记录 `LastError` / `RuntimeDiagnostics`；不能只显示“等待视频”或把模型调高来修清晰度。独立模式预览失败应保留健康的分析；兼容模式资源分配失败沿用其明确输入错误，不能伪装成功或替换成缩小图。

## 五、Setting 点线在 Update 直接取 SDK 坐标

本次重点适配现有 `VenueSettingsController` / `VenueSettingsSkeletonRenderer`，保留 UGUI 透明骨骼层与原布局。当前业务旧逻辑依赖 `TryGetAnalysisInputFrame` 和 `ResultSequence` 才重绘；移除这些日常绘制门槛，使用每帧仍有效的 SDK 查询。

```csharp
private void Update()
{
    // 接入现有字段/绘制池，不另外创建 SDK。
    if (preview != null) preview.texture = sdk != null ? sdk.GetColorImageTex() : null;
    ClearVisiblePose(); // 清本次有效标记；不是每帧销毁/重新创建 UI 对象。
    if (sdk == null) return;
    for (int slot = 0; slot < sdk.GetMaxBodies(); ++slot) {
        if (sdk.TryGetJointNormalizedPosition(slot, HumanVisionCanonicalJointId.WristLeft, out var uv))
            DrawExistingJoint(slot, uv); // 替换为现有池化点线绘制；查询 false 时不画。
    }
}
```

`ClearVisiblePose` / `DrawExistingJoint` 是业务绘制接点，不是新增 SDK API。实现时遍历所需的真实关节；也可复用预分配 `HumanVisionCanonicalJoint[32]` 调用 `CopySkeletonByIndex`，读取有效点的 `Position.Normalized`。SDK 检查有效性/置信度/年龄/来源，不能额外缓存过期点当有效点。

- 每个 Update 都可以重复读取最新有效观测，**不要求 ResultSequence 变化、FPS 达标、FrameId 相等、RuntimeProgress 可用、分析 Texture 可用或实时 Texture 已就绪**。骨骼开关是显示条件，预览纹理开关不能变成 SDK 取点条件；SDK 返回 `false` 就清除该点/线。
- 点/区域都是左上原点、Y 向下的归一化坐标。现有 UGUI Rect 的映射可用 `xMin + uv.x*width`、`yMax - uv.y*height`，对应的是视频实际显示 Rect，排除 letterbox 黑边。不要乘模型 `512/640/960` 尺寸再映到高清图，不重复镜像/旋转，不把二维世界平面映射宣称真实 3D。
- 槽位可以有空洞，循环上限用 `sdk.GetMaxBodies()`；身份使用 `GetUserIdByIndex` / `StableTrackId`，不要把索引等同人员身份。SDK 支持 1–8 人，但跨栏王既有四道、四头像等业务限制保留，不借升级扩成八道。
- **显示与动作消费分别保留原语义。**跑跳动作需要“一条新观测处理一次”时继续使用 `ResultUpdated` 或骨骼 metadata 序号去重及来源时间戳，不能把 Update 的重复读取算成新动作。断流、停止和换源/代次时清理业务动作缓存；不改变已校准动作阈值或计分规则。
- 检查 `PrefabPose.TryRead(view, ...)`、`TrackingCameraManager`、`ReadyManager.TestReadyPlayer` 中依赖 `TryGetFrame` / `view.TryGetJointPosition` 的日常分支，先列出具体调用方和可审查的最小适配差异。`PresentationMode=Disabled` 后它们不会产生配对数据；普通头像预览/举手检测应改读 SDK 原图及有效归一化点，动作可复用现有 `PrefabPose.TryReadResult(sdk, ...)`，保留同一条观测的 metadata。保持原动作算法、身份/区域校验、观测去重和计数时序，不新加 FrameId/FPS 门槛。不要让“先取得配对帧”成为发起一次精确拍照的前置条件；照片按需单独请求，仍走下一节。

## 六、精确头像/照片继续使用明确的同帧路径

检查 `AppRuntime` 当前持续设置的 `MatchedColor`：普通预览/点线改为 `HumanVisionRuntimePresentationMode.Disabled`，无需持续配对/合成纹理。Ready/头像真正需要同帧时，串行、按需临时开启 `MatchedColor`，使用 `UseAnalysisFrameForMatchedPreview=false`，取得当前来源代次有效的 `TryGetFrame(out HumanVisionPresentedFrame)` 后复制，再在 `finally` 恢复原显示模式。

- 不把“当前原图 + 最新旧骨骼”标成同帧照片。核对配对 API 的来源/代次、`SourceFrameId`、`ResultSequence`、图像尺寸，使用其匹配的色图与骨骼。`InputFrameId` 是原输入编号，`SourceFrameId` 是 Runtime 提交编号，兼容路径二者不必数值相等，不新增这种错误约束。epoch 的隔离由 SDK 配对合同完成，不编造 `HumanVisionPresentedFrame.ConfigEpoch` 属性。
- 保留现有 **500×500** 头像业务裁切、四槽写盘顺序、已登录头像缓存和账号协议；裁切按本次实际原图与有效人物坐标计算，不写死来源一定是 1920×1080。这不是新增分析 ROI，不裁剪模型识别输入。
- 返回的 Texture 是借用引用；保留照片先复制到业务拥有的独立 RT。GPU 复制/AsyncGPUReadback 真正完成后才释放资源。取消只结束调用方等待，不能销毁仍在回读的 RT，不关闭 fence、不用超时强释放。
- 必须回归“拍照完成/取消/超时后恢复普通预览”：同一 source/generation 下预览继续前进，不能残留受保护的 matched 槽使预览长期降速。正式版已修复这一 SDK 保护释放路径，业务需正确恢复模式，不靠重连或切模型掩盖问题。

## 七、后端、模型与游戏 GPU 的边界

- 后端偏好真实枚举为 `HumanVisionAccelerationMode.Cpu` / `Graphics` / `Neural`；等级使用 `ModelInputQuality.Low` / `Medium` / `High`。按已安装的能力/profile 和原打包选择启用，不因为文件存在就宣称 provider 可运行。没有对应资格时明确不可用，不悄悄改后端。
- 保留既有低/中/高 profile；不得新增/替换模型、降低置信度、拉长年龄阈值、关闭手点过滤，或默认启用实验 `UltraLow` 来凑 FPS。人体/手点缺失继续返回无效，不能补假 Hand/Handtip/Thumb。
- `HostedVisionPolicy` 与 AAR 的识别打包配置仍决定宿主会话的计算模式、模型等级及旧请求配置。不要让设备旧保存设置、Setting 高级开关或 `VisionGameSettings` 的自动 Neural 探测覆盖宿主 baked backend/model。原图 RTSP 的新尺寸合同独立生效，旧“输出尺寸固定”标签需避免误称它仍会缩小当前码流；保留其序列化兼容与非 RTSP 请求语义。
- SDK 独立模式把原生输入/解码、识别和 Unity 游戏帧循环分开，Unity 主线程仍负责展示和查询。`Task.Run` / 异步启动本身不等于 GPU 隔离，不在后台线程调用 Unity Texture/RawImage/GL API。
- 同一个设备的游戏与 GPU 推理仍共享 GPU/带宽/功耗/散热；NPU 模式的图像预处理也可能仍用 Vulkan。当前不能宣称 RGA 已接入，也不能宣称硬 GPU 隔离、完全不争抢、切档零中断或 RK3588 已达指定 FPS。只按本机实际后端、计数和联合游戏负载报告。

## 八、保留 AAR/宿主流程，构建的是可回退候选

1. 沿用 `Sensory Game/AAR/一键打包工具`、游戏目录、包名、宿主协议与本地构建链；默认配置路径当前为 `Assets/SensoryAarTool/Configuration/Vision_NPU_Low.asset`，但实际选中配置可能是 GPU 或其他资产，先读取项目级保存选择，不能只按文件名决定后端。
2. 计算模式、模型等级来自本次选中的 `AarVisionBuildConfig` 并生成构建快照。设置资产不会改写已构建 AAR，改变 baked 配置后需要重建。不要把 Unity 游戏渲染分辨率、Player/Quality Graphics 设置与模型尺寸或旧输入尺寸上限混为一谈。
3. 保持已验证的 ARM64、IL2CPP、minSdk 26+、Vulkan 优先及原网络权限/生命周期设置；根据现有工具实际校验构建，AAR 打包用工具支持的 JDK 17，不擅自替换整个 Unity 安装的 JDK。保留既有场景清单/顺序和多游戏宿主分发。
4. 构建前完成 Editor 编译与功能验证，先导出候选到独立目录。沿用发布行为配置 `hostTesting=false` 检查真实宿主协议；需要测试面板的构建另标 `hostTesting=true`，不能把测试面板当正式包。所有 AAR、打包快照、manifest、日志及 SHA-256 一并保留。
5. 读取 `E:/UnityProject/YS-Sensory game project/HostApp` 的现有构建说明，在隔离验证副本替换候选 AAR，按实际脚本（原流程为 `build.ps1 -Offline`）生成候选 APK。不得为了依赖冲突删掉宿主业务代码或直接覆盖正式目录中的生产 AAR。
6. 在用户已授权的测试 Editor/设备验证候选。没有目标 RK3588 或未获相应设备接入时完成其他可做的验证，列为未验，不以 Windows/OnePlus 或 SDK Demo 成绩代替跨栏王 RK3588 成绩。

## 九、验收必须覆盖完整业务与拍照后稳态

先给本次变更补针对性的回归并确认旧行为失败，再实现最小适配。复用现有测试，不为布局外观复制实现测试。至少验证：

- 包版本/锁定提交/本地资源哈希匹配、无重复 native；Editor 无编译错误，正常退出重入、Stop/Restart/换源/设置返回、宿主打开/返回/退出均不重复建会话。
- Setting 原图持续更新，骨骼/区域编号按同一个视频 Fit Rect 正确叠加；无人/点过龄会清除；preview 暂时不可用不挡有效骨骼查询；骨骼开关不停止识别。
- RTSP 断流重连和 1080p/720p（有条件再验证 4K）真实源切换，实际 Texture 尺寸符合当前显式 URL，model 等级变化不把颜色预览缩成模型小图；不靠自动换子码流改善结果。
- 原后端/模型与 1–8 SDK 容量合同可配置，业务四道/四头像不被扩大；场地区域、镜像、保存/加载与宿主 baked 约束不丢，`ExecutionPolicy/PreviewEnabled` 经每个转换边界仍正确。
- 举手加入、跑动、有效起跳/跨栏、分道、计分、完成一局、成绩/排行榜与宿主回调的旧业务回归；无重复动作消费和错代头像。
- 500×500 精确头像的同帧身份、原图来源、正常/取消/超时、场景退出时的在途读回屏障。**拍照前约 20 秒、完成后 5–35 秒、35–65 秒**记录同一 source/generation/epoch 的 Texture/源/识别/Game FPS；期间不重连、不换模型，不用重建源掩盖持续降速。记录单次照片峰值与后续稳态，不要求单帧采样值恒定。
- 对目标硬件按当前选定 CPU/GPU/NPU 和模型做可用性及实际后端验证；NPU 实测需日志确认 `backend.rknn`，GPU 为实际图形后端。无 RK3588 就写“未验证”，不自动认证真实双手/逐人 30 完整骨骼 FPS。

统计口径必须分开：

| 指标 | 正确来源与限制 |
| --- | --- |
| 源解码/获取/live 发布 | `TryGetSourceProgress(out HumanVisionSourceProgress)` 的累计 `DecodedFrames` / `AcquiredFrames` / `LivePublishedFrames` 求时间差，按 SourceId/Generation/ConfigEpoch 分段；返回 false 就显示未知。`SourceWidth/Height` 可能是 coded 尺寸，不代替实际 Texture 可见尺寸。 |
| Unity 可见 Texture FPS | `TryGetInputFrame` 的来源/代次/FrameId 不同才计新帧，除以真实窗口时间；重复 GUI/Update 读同 Texture 不加计数。 |
| 原生分析接纳/完成 | `TryGetRuntimeProgress` 的 `NativeInputFrames` / `NativeFreshFrames` 差分；新鲜无人结果也可能计完成，不等于完整逐人骨骼。 |
| 业务实际收到的新观测 | `ResultUpdated` 或结果序号去重，另记业务消费次数；`InputPublicationFps` / `AnalysisInputPublicationFps` / `FreshResultFps` 保原 API 语义，不能把输入接纳或 Unity 观察值冒充源解码帧率。 |
| 游戏与延迟 | 独立记录 Unity/Game FPS、帧耗时、SDK 推理耗时、结果年龄及设备温度。年龄不是已验证的传感器端到端延迟；精确拍照/配对 FPS 单列。 |

新累计计数不可用的平台/旧会话应标 unknown，不伪报全零。不要让诊断为了计数强制开启持续 MatchedColor、第二输入或每帧 readback。

## 十、交付与回退

交付一份自包含中文报告，包含：实际升级前后包版本与正式 tag commit、本地资源清单/哈希、修改文件及理由、后端/模型/执行策略实际值、原图与分析尺寸、自动测试命令/结果、设备型号和日志、业务逐项通过/失败/未验、同源拍照前后稳态数据、候选 AAR/APK 绝对路径及 SHA-256、完整回退步骤。

保留失败日志，不用“能编译”替代业务验收，不用 SDK Demo 在 OnePlus 上的结果替代本项目的验收。准备好候选后再由用户决定正式业务 AAR 的替换与交付；不得因为这次 SDK 正式发布就自动发布跨栏王 AAR。

执行时对照正式版本中的 [Runtime Prefab 与普通 Update 取点](../user-guide/UNITY_RUNTIME_PREFAB.md)、[SDK 配置/API](../user-guide/SDK_CONFIGURATION.md)、[本地 Android 部署](../user-guide/LOCAL_ANDROID_CPU_YOLO.md) 及实际源码。部署说明中旧 preview 章节仅作历史背景，本次正式资源/库必须以本次交付 manifest 为准；如旧章节与本文原图/独立 live 合同冲突，先核对正式 tag 的实际实现和发布说明，不恢复旧 5Hz CPU 预览或模型尺寸颜色图路线。
