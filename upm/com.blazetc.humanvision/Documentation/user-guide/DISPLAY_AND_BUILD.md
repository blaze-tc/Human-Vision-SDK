# 可选：示例界面、预览显示与平台构建

适用 SDK **0.4.1** / Input **0.1.1**，以下以 Unity 2021.3.45f1 和英文编辑器菜单为例。最低支持 Unity 2021.3。[文档首页](README.md)

本页是可选的显示与构建教程。学习初始化和骨骼读取请先看[代码入门引导](FIRST_INSTALL.md)；该引导只需一个空物体和一份脚本。本页的Canvas、RawImage、Overlay、示例界面和打包步骤用于后续需要显示画面或构建程序时查阅。

## 1. 准备环境并创建项目

1. 在 Unity Hub 安装 Unity `2021.3.45f1`。测试 Windows 时安装 Windows Build Support；要打 Android，再安装 Android Build Support 及其 SDK、NDK、OpenJDK。
2. 安装 Git，并在终端执行 `git --version`，确认能输出版本。使用 Git 安装方式时 Unity 必须能找到 Git；安装 Git 后重启 Hub/Editor。
3. 准备可用的 USB 摄像头或电脑内置摄像头。先用系统相机应用确认画面正常，再关闭占用相机的软件。
4. 打开 Unity Hub → **New project**，选择 **3D Core**，名称填写 `HumanVisionFirstUse`，保存到自己有写入权限的目录，点击 **Create project**。
5. 等 Unity 打开。选择 **Window → General → Console**，确认没有红色编译错误。
6. 在 Project 窗口的 Assets 下创建 `Scenes` 和 `Scripts` 文件夹。选择 **File → Save As**，把初始场景保存为 `Assets/Scenes/FirstVision.unity`。

检查：Hierarchy、Inspector、Project、Console 四个窗口可见，初始场景已保存。SDK 的基础例子使用 Unity 自带 UGUI 和协程，无需安装其他游戏框架。

## 2. 安装 Input 和 SDK 两个包

Input 负责获取图像，SDK 负责人体识别。必须先装 Input，再装 SDK，两者都使用下面的固定标签。

### 2.1 推荐方式：Git URL

1. 选择 **Window → Package Manager**。
2. 点击左上角 **+ → Add package from git URL…**。
3. 粘贴 Input 地址，点击 **Add**，等待安装和编译结束：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.1
   ```

4. 确认列表出现 **Human Vision Input**，版本为 `0.1.1`。
5. 再点击 **+ → Add package from git URL…**，安装 SDK：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.1
   ```

6. 确认列表出现 **Human Vision SDK**，版本为 `0.4.1`，Console 无编译错误。
7. 检查 `Packages/manifest.json` 中两个包均固定为 `v0.4.1`。Unity 生成的 `Packages/packages-lock.json` 里两条 Git `hash` 应与发布附件 `source-snapshot.json` 中的 `source_commit` 一致。

地址中的 `?path=` 指向同一仓库里的不同包；不要只粘贴仓库首页地址。需要联网下载包，但运行本地相机识别不需要从互联网下载模型。

### 2.2 离线方式：只选其中一种

下载配套的 Input 和 SDK 附件并按 `SHA256SUMS.txt` 校验。选择一种安装方式。[版本发布页](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.1)

| 方式 | 第一步：Input | 第二步：SDK | Unity 操作 |
| --- | --- | --- | --- |
| UPM 压缩包 | `com.blazetc.humanvision.input-0.1.1.tgz` | `com.blazetc.humanvision-0.4.1.tgz` | Package Manager → **+ → Add package from tarball…**，依次选择两个文件 |
| Assets 导入包 | `HumanVisionInput-0.1.1.unitypackage` | `HumanVisionSDK-0.4.1.unitypackage` | **Assets → Import Package → Custom Package…**，先导入 Input，等编译结束，再导入 SDK |

Assets 导入方式的文件显示在 Assets 下，不会像 UPM 一样列为两个已安装包。不要同时安装 Git、tgz 和 unitypackage 的重复副本；重复程序集/同名类错误先检查重复安装。安装用户无需自行编译 C++。

## 3. 安装模型与运行配置

1. 等两个包编译完成，确认顶部菜单出现 **HumanVision**。
2. 点击 **HumanVision → Install Packaged Models**。
3. 等复制和资源导入完成。
4. 在 Project 中查看 `Assets/StreamingAssets/HumanVision/Runtime`，确认有 `index.json` 及索引引用的 profiles、modelpacks 和模型文件。
5. 查看 Console：若有缺失文件或校验错误，先解决，再继续。

检查：运行数据位于 **Assets/StreamingAssets**，会进入构建。只安装脚本、只复制单个模型或只看到 SDK 菜单都不算完成。

首次运行还会通过 `HumanVisionRuntimeData.Prepare` 校验并提取这些资源到可读缓存目录。后面初始化必须使用它回调的 `RuntimeRoot`；不要直接写死自己电脑上的路径。

## 4. 用 Runtime Prefab Acceptance 验证摄像头

1. 在 Package Manager 选 **Human Vision SDK → Samples → Runtime Prefab Acceptance → Import**。打开 `RuntimePrefabAcceptance.unity`。离线安装后，场景在 `Assets/HumanVision/Samples/RuntimePrefab`。
2. 进入 Play Mode，打开「输入连接」的「本地相机」标签。刷新并选择设备，允许相机权限，按需要请求 1280×720、30 FPS。
3. 点击「应用并连接」。观察右侧实际预览、骨骼和左侧有效骨骼 FPS。在 Windows 可先选择 CPU，在 Android 公共包选择 GPU，实际后端和档位以运行日志为准。
4. 点击「一键采集日志」生成 JSONL 并获取路径。需要复测时，停止并重连，核对下次打开能读取上一次成功的输入。[场景操作与骨骼查询](UNITY_RUNTIME_PREFAB.md)

## 5. 切换 RTSP 或测试本地视频

- **RTSP** 在同一个验收场景打开「输入连接」的 RTSP 标签，填写设备可访问的完整码流地址。手机访问电脑时填写电脑的局域网地址，手机上的 `127.0.0.1` 指手机自身。核对实际执行路径、原图尺寸、Texture 更新 FPS 和有效骨骼 FPS。
- **本地视频** 用 **SDK Settings** Sample 的 Video 模式，或在业务代码中用 `InitializeFromPreset(configuration, InputKind.Video, address)`。填写目标设备可读的文件或资源位置。[设置场景](SETTINGS_DEMO.md) · [初始化参数](SDK_CONFIGURATION.md)

## 6. 从空场景手动搭建自己的预览

本节至第 11 节保留直接组装 Manager 和兼容输入桥的 Windows x64 CPU 例子。新业务优先使用 `HumanVisionSdk` 总控。Android 首次接入使用第 4 节验收场景和第 13 节构建配置。[总控 API](SDK_CONFIGURATION.md)

### 6.1 创建 Canvas 和预览区域

1. 退出 Play，双击 `Assets/Scenes/FirstVision.unity`。
2. 在 Hierarchy 空白处右键 **UI → Canvas**，名称设为 `VisionCanvas`。Unity 通常同时创建 EventSystem，保留它。
3. 选中 Canvas，Inspector 的 **Render Mode** 设为 **Screen Space - Overlay**。
4. Canvas Scaler 的 **UI Scale Mode** 设为 **Scale With Screen Size**，Reference Resolution 为 `1280 × 720`，Match 为 `0.5`。
5. 右键 Canvas → **Create Empty**，命名 `PreviewArea`。它应有 RectTransform。
6. 选中 PreviewArea，RectTransform 的 Anchor Min 为 `(0,0)`、Anchor Max 为 `(1,1)`，Left/Right/Top/Bottom 全部为 `0`，Scale 为 `(1,1,1)`。
7. 右键 PreviewArea → **UI → Raw Image**，命名 `Preview`。Texture 留空，Color 设为白色，关闭 Raycast Target。
8. 选中 Preview，点击 **Add Component → Aspect Ratio Fitter**，Aspect Mode 设为 **Fit In Parent**，Aspect Ratio 初始 `1.777778`。运行时桥会按实际画面更新比例。
9. 确认 Preview 的 Scale 为 `(1,1,1)`。比例组件负责保持完整画面，不需要手动把视频拉伸到变形。

### 6.2 创建骨骼叠加层

1. 右键 Preview → **Create Empty**，命名 `SkeletonOverlay`，作为 Preview 的子对象。
2. 确认 SkeletonOverlay 有 RectTransform；Anchor Min `(0,0)`、Anchor Max `(1,1)`，Left/Right/Top/Bottom 全部 `0`，Pivot `(0.5,0.5)`，Scale `(1,1,1)`。
3. 点击 **Add Component**，搜索并添加 **Canvas Renderer**。
4. 再添加 **Human Vision Overlay**（代码类名 `HumanVisionOverlay`）。不需要额外添加 Image 或 RawImage。
5. 关闭 Overlay 的 Raycast Target。Manager/Frame Source 由下一节脚本配置，暂时留空。

Overlay 跟随已经保持宽高比的 Preview，骨骼才能和图像对齐。它负责绘制结果，不负责初始化、采集或推理。

### 6.3 创建识别根对象

1. 在 Hierarchy 空白处右键 **Create Empty**，命名 `VisionRoot`。
2. 下一节将两个示例脚本放入 `Assets/Scripts`，然后添加到 VisionRoot。
3. 最终层级应为：

   ```text
   FirstVision
   ├── Main Camera
   ├── Directional Light
   ├── VisionRoot
   ├── VisionCanvas
   │   └── PreviewArea
   │       └── Preview                 RawImage + AspectRatioFitter
   │           └── SkeletonOverlay     CanvasRenderer + HumanVisionOverlay
   └── EventSystem
   ```

4. **Ctrl+S** 保存场景。

## 7. 初始化代码：准备资源、创建会话、打开相机

1. 打开完整示例文件 [SdkCameraQuickStart.cs](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/docs/user-guide/examples/SdkCameraQuickStart.cs)，复制**整个文件**。
2. 在 `Assets/Scripts` 创建 C# 文件 `SdkCameraQuickStart.cs`，用完整示例替换自动生成的内容并保存。文件名必须与类名一致。
3. 等 Unity 编译结束，将脚本拖到 VisionRoot。`RequireComponent` 会自动添加 HumanVisionManager、VideoPlayerFrameSource，以及桥所需的 VideoPlayer；无需再添加相机门面。
4. 在 `Sdk Camera Quick Start` 组件中绑定：

   | Inspector 字段 | 拖入的对象/组件 |
   | --- | --- |
   | Preview | Hierarchy 中的 Preview（RawImage） |
   | Preview Fitter | 同一个 Preview（AspectRatioFitter） |
   | Overlay | SkeletonOverlay（HumanVisionOverlay） |
   | Max Bodies | 首次填写 `1` |
   | Camera Device | 留空使用默认相机，或填写设备真实名称 |

5. HumanVisionManager 的 **Initialize On Start** 保持关闭；本示例统一负责初始化。
6. 在复制到项目的 `SdkCameraQuickStart.cs` 中，将历史 `windows-pc-cpu` 改为本版随包的 `windows-yolo-cpu-low`。确认场景只有一个负责采集和初始化的根对象，再保存。

完整文件已包括引用、组件、错误处理、预览绑定和停止方法。核心初始化顺序如下（这是完整文件的解释片段，不需要另外创建第三个脚本）：

```csharp
string root = null, error = null;
yield return HumanVisionRuntimeData.Prepare(
    value => root = value, value => error = value);
if (string.IsNullOrEmpty(root)) {
    Debug.LogError(error);
    yield break;
}
if (!Manager.TryInitialize(new HumanVisionConfig {
    RuntimeRoot = root,
    Profile = "windows-yolo-cpu-low",
    MaxBodies = 1
})) {
    Debug.LogError(Manager.LastError);
    yield break;
}
// 完整例子随后 Configure 预览/骨骼，Open 相机，并 BindUnifiedSource。
```

`Prepare` 成功只表示运行资源准备好；`TryInitialize=true` 表示会话创建好；相机的 `Open` 是异步开始，后续状态为 `Streaming` 才表示有源画面。`BindUnifiedSource` 让桥自动提交新帧，Manager 自动轮询完成结果。应用不用在 Update 中阻塞等待推理。

要测试 Windows DirectML，可将此例子的 Profile 改为 `windows-yolo-directml-low`。重新进入 Play，检查实际 Profile 和错误。

## 8. 骨骼调用代码：人数、身份和关节点

1. 打开完整示例 [SdkSkeletonReader.cs](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/docs/user-guide/examples/SdkSkeletonReader.cs)，复制整个文件到 `Assets/Scripts/SdkSkeletonReader.cs`。
2. 等编译结束，把脚本拖到**同一个 VisionRoot**。它会找到已有 Manager/Bridge，不需要手动绑定。
3. Minimum Confidence 首次保持 `0.35`。
4. 保存场景，点击 Play；站到镜头前。
5. Console 应先出现 `SDK initialized: windows-yolo-cpu-low`，然后输出输入状态和骨骼日志，包含人物身份、左腕与左膝有效性和观察时间。
6. 没有人时 `BodyCount=0` 是正常结果。左腕被遮挡时有效性可能为 false；不能把默认 `(0,0)` 当作观测坐标。

关节点读取的核心如下，完整示例通过 `ResultUpdated` 回调执行，并去掉重复身体序号：

```csharp
for (int i = 0; i < manager.BodyCount; i++) {
    HumanVisionBody body = manager.Bodies[i];
    long personId = body.StableTrackId;
    HumanVisionCanonicalJoint wrist = body.CanonicalJoints[
        (int)HumanVisionCanonicalJointId.WristLeft];
    if (wrist.Position.Valid && wrist.Position.Confidence >= 0.35f) {
        Vector2 pixel = wrist.Position.Pixel;
        Vector2 normalized = wrist.Position.Normalized;
        long observedUs = wrist.ObservationTimestampUs;
        // 在这里复制有效数值，供自己的交互逻辑使用。
    }
}
```

- `BodyCount` 是这次结果的人数；只读 `Bodies[0..BodyCount)`，数组长度是容量。
- `StableTrackId` 是人物身份；`i` 只是当前数组位置，不保证同一人每帧都在同一位置。
- `WristLeft` 是人物的左手腕；把枚举换成 `ShoulderLeft`、`HipLeft`、`KneeLeft` 等即可读取其他点。完整 32 槽位表见 [API 关节点表](API_REFERENCE.md#53-32语义关节点逐项)。
- `Normalized` 是图像坐标，左上 `(0,0)`、右下 `(1,1)`，Y 向下；`Pixel` 是对应图像像素。它们不是米制 3D，也不能直接当世界位置。
- `Valid` 表示有可用点；`Confidence` 表示置信度；`IsDerived` 表示语义映射/派生点。32 个槽位不保证全部有效，真实手点与身体手腕要区分。
- SDK 会复用身体与关节数组。不要把 `Bodies` 数组引用保存下来当历史快照；只复制需要的数值。

完整 Reader 提供 `BodyCount`、`FirstTrackId`、`HasLeftWrist`、`LeftWristNormalized` 供其他脚本读取，并在停止输入后清空状态。它演示关节读取；实际多人交互应按 StableTrackId/区域绑定角色，并增加自己的结果年龄、丢失、遮挡和换人策略。

## 9. 多人和区域调用示例

先退出 Play，将 QuickStart 的 Max Bodies 从 `1` 改成 `2`，再 Play，让两个人完整进入画面。Reader 会遍历两个身体；`FirstTrackId` 仅保存当前数组第一项，用于演示，不能当固定玩家身份。

如果需要左右两个区域，在初始化成功后设置下列区域。**这是可选扩展片段**，区域索引从 0 开始，界面编号通常从 1 开始：

```csharp
Rect[] regions = {
    new Rect(0f, 0f, 0.5f, 1f),   // 图像左半边，区域0
    new Rect(0.5f, 0f, 0.5f, 1f)  // 图像右半边，区域1
};
long regionRevision = 1;
if (!manager.TrySetRegions(regions, regionRevision))
    Debug.LogError(manager.LastError);
// 复用这个缓冲区，不要每帧 new。初始化容量为2。
int[] assignments = new int[2];
```

在新结果回调中读取：

```csharp
if (manager.TryCopyRegionAssignments(assignments, out long revision) &&
    revision == regionRevision) {
    for (int i = 0; i < manager.BodyCount; i++) {
        int regionIndex = assignments[i]; // -1 表示没有匹配区域
        long trackId = manager.Bodies[i].StableTrackId;
    }
}
```

区域是推理结果的空间分配规则；当前发布路线不会因为画了小框就只计算框内像素。更改区域时递增 revision，容量和区域缓冲区保持一致。当前区域设置不绘制区域框，若需要可视化编辑，使用自带示例的区域 UI。

## 10. 停止、释放和场景切换

1. 在 VisionCanvas 下创建 **UI → Button**，命名 `StopVisionButton`，文字改为“停止识别”。将它放在能点击的位置，作为 PreviewArea 的同级对象并置于其后，避免被预览遮挡。
2. 选中按钮，Inspector → **Button → On Click()** 点击 **+**。
3. 把 VisionRoot 拖到对象栏；函数下拉选择 **SdkCameraQuickStart → StopVision()**。
4. Play 中点击按钮，等待 Console 显示 `Input and SDK stopped`。预览和骨骼清空，相机停止、会话释放。
5. 这个最小示例停止后不提供重启按钮；退出并重新进入 Play 即可重启。

停止顺序：停止使用结果/隐藏 Overlay → `DetachUnifiedSource` → 等 `UnifiedRetirementPending=false` → 源 `Close` → Manager `Shutdown`。等待是协程让出帧，不是阻塞主线程。

如果需要离开场景，在自己的协程中先等待完整例子提供的方法，再卸载场景：

```csharp
yield return quickStart.StopVisionRoutine();
UnityEngine.SceneManagement.SceneManager.LoadScene("NextScene");
```

`quickStart` 是 Inspector 绑定的 `SdkCameraQuickStart` 引用，`NextScene` 要先加入构建场景。不要先 Destroy/禁用整个 VisionRoot 再尝试启动停止协程；OnDestroy 里的最后解绑不能替代正常导航时的资源退休等待。Reader 在禁用时取消 ResultUpdated 订阅。

## 11. 独立输入预览的最小调用

仅想显示输入、不做骨骼识别时，可给空对象添加 `WebCameraFrameSource`，给 RawImage 添加 `FramePreview`，调用：

```csharp
source.Open(new HumanVisionSourceSettings {
    Kind = InputKind.WebCamera,
    RequestedWidth = 1280, RequestedHeight = 720,
    RequestedFramesPerSecond = 30
});
framePreview.Bind(source);
// 关闭独立预览（没有绑定SDK识别桥的情况）：
framePreview.Bind(null);
source.Close();
```

这里 `source` 和 `framePreview` 是已绑定的组件引用，片段解释输入 API，不是完整新脚本。FramePreview 自动 LateUpdate 刷新；不会产生 Bodies。完整骨骼场景中桥已经更新 RawImage，不需要再重复添加 FramePreview。

## 12. 构建 Windows x64 程序

1. 退出 Play，保存场景。
2. 打开 **File → Build Settings**，选择 **PC, Mac & Linux Standalone**，Target Platform 为 Windows，Architecture 为 **x86_64**，必要时点 Switch Platform。
3. 选择实际要运行的场景，点 **Add Open Scenes**。
4. 将首个运行场景放在第 0 项。构建本教程的最小例子时选择 `FirstVision`，构建验收样例时选择 `RuntimePrefabAcceptance`。
5. 确认 StreamingAssets 的 Runtime 资源存在。保留包提供的原生 DLL 及依赖，不只复制单个 humanvision.dll。
6. 点击 **Build**，输出到单独目录，如 `Builds/Windows`。
7. 从输出目录启动 EXE，允许相机访问，重新检查画面、人体骨骼、人数与错误；Editor 成功不自动等于独立程序成功。

## 13. 新项目构建 Android

Android 使用第 4 节的 Runtime Prefab Acceptance。

1. 准备 Android ARM64 设备；系统 API 至少 26，GPU/驱动支持所选 Vulkan 路线。安装 Hub 的 Android 模块。
2. 打开 **Build Settings → Android → Switch Platform**，等重新导入完成。
3. 打开 **Player Settings → Other Settings**：Scripting Backend 选 **IL2CPP**；Target Architectures 勾选 **ARM64**、取消 ARMv7；Minimum API Level 设为 **Android 8.0 / API 26** 或更高。
4. 在 Graphics APIs 中关闭 **Auto Graphics API**，将 **Vulkan** 设为首选；首次验证可仅保留 Vulkan。
5. 打开 **Edit → Project Settings → Human Vision → Android Runtime**，选 **NCNN Vulkan**。构建元数据会限定运行 Profile；不要只在代码里改字符串假装安装了另一后端。
6. 确认已执行 **Install Packaged Models**，添加 `RuntimePrefabAcceptance.unity` 为构建首场景。使用 RTSP 时将 Player Settings 的 Internet Access 设为 Require。
7. 填写自己的 Package Name，连接设备后 **Build And Run**。
8. 手机上打开「输入连接」，允许相机权限或填写设备可访问的 RTSP 地址。检查 `Streaming`、实际 `backend.ncnn.vulkan`、原图尺寸和有效骨骼。相机兼容 GPU 路径优先测试横向 16:9 的实际源输出。输入图像合同不满足时查看日志。
9. 测试所需的本地相机和 RTSP 路线。视频用 SDK Settings 或自己的视频接入场景验证。
10. 分别测试拒绝权限后重开、前后台、停止/重连、重新启动读取保存配置。实际持续人数、吞吐和温度需要真机测量，参见[平台文档](PLATFORM_TEST_RESULTS.md)。

Android Vulkan 模型输入分别是低 `512×288`、中 `640×384`、高 `960×576`。实际源尺寸与模型输入分开记录。Android CPU 和 RK3588 NPU 部署需使用匹配的本地 SDK 和资源。[本地 Android 部署](LOCAL_ANDROID_CPU_YOLO.md)

## 14. 常见问题与第一次完成检查

| 现象 | 先检查什么 | 怎样处理 |
| --- | --- | --- |
| 找不到 HumanVision 菜单 | Console 红色错误、包是否重复 | 修正编译错误；确认两个包安装完成 |
| Git 安装找不到 Input 版本 | 是否只安装了SDK | 按第2节显式先安装Input Git子包 |
| Prepare/初始化失败 | Runtime/index.json、索引文件完整性、LastError | 重新执行 Install Packaged Models；使用Prepare成功回调的根目录 |
| DLL/入口加载失败 | 目标是否x64，依赖是否保留，混装旧包 | 检查插件与依赖，移除重复旧副本后重新导入 |
| 相机一直Opening/报错 | 权限、设备名称、其他软件占用 | 允许系统权限、关闭占用软件、留空默认设备再重开 |
| 有画面无骨骼 | 实际后端、模型档位、输入状态和有效骨骼 FPS | 让人物完整入镜，分别查看 SDK 初始化、输入和识别诊断 |
| 有结果无可见骨骼 | Overlay/CanvasRenderer、引用、位置、启用状态 | 按第6～7节复查，确认Overlay在Preview子层且铺满 |
| 骨骼与画面错位 | RawImage比例、重复镜像/旋转 | 同一预览下绘制；使用Input已定向图像，不额外反转坐标 |
| 手端点无效 | 当前Profile是否开启真实手任务 | 当前随包配置关闭真实手任务；不要用腕点偏移补造观察 |
| 手机上找不到电脑视频/RTSP | 文件位置/主机地址/服务/网络 | 使用设备可读位置和LAN地址，先确认服务可访问 |
| 显示很流畅但识别率低 | 是否在统计重复显示帧 | 按新ResultSequence计数，并按每个人有效点统计 |

第一次接入完成应能逐项确认：双包版本正确、运行资源齐全、摄像头真实画面、人体框与骨骼可见、Reader读到有效点、停止释放成功、目标平台独立构建复查通过。接口详细参数、默认值、返回值和所有语义点见 [API调用与说明](API_REFERENCE.md)。

示例是读取与显示入门；当前模型包的评估/分发资格见[第三方说明](../../THIRD_PARTY_NOTICES.md)。本指南与平台报告不声称已达到连续8人每人30个新完整骨骼结果/秒。
