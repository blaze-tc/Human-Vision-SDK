# 第一次安装使用引导

适用当前发布：SDK **0.4.0-preview.4** / Input **0.1.0-preview.2**，Unity **2021.3+**，Windows x64 / Android ARM64。[文档首页](README.md)

本页教你在自己的脚本里完成三件事：**初始化SDK、输入图像、读取骨骼**。先以Windows摄像头为例，创建一个空物体并挂一份脚本即可。

最省事的做法：安装资源后，将完整的[SdkBasicUsage.cs](examples/SdkBasicUsage.cs)复制到`Assets/Scripts/SdkBasicUsage.cs`，在Hierarchy创建空物体并挂上它，然后Play。下面按这个脚本的执行顺序解释各段代码；片段都属于同一个类，不用创建多份脚本。

## 1. 安装包和模型

1. 打开**Window → Package Manager → + → Add package from git URL…**，先安装Input：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.4
   ```

2. 等安装和编译完成，再以同样方式安装SDK：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.4
   ```

3. 确认Package Manager显示Input `0.1.0-preview.2`与SDK `0.4.0-preview.4`，Console无编译错误。Git安装需要系统已安装Git；两包都用完整地址。
4. 点击**HumanVision → Install Packaged Models**。确认`Assets/StreamingAssets/HumanVision/Runtime/index.json`及运行资源已经生成。
5. 创建`Assets/Scripts/SdkBasicUsage.cs`并复制完整示例。Hierarchy右键**Create Empty**，命名`Human Vision Starter`，把脚本拖到物体上。没有需要手动拖入的UI引用。自动添加的Manager保持**Initialize On Start**关闭，由示例代码初始化。

离线安装可在[发布页](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.4)下载Input、SDK对应的tgz或unitypackage，按Input→SDK顺序导入，选一种方式即可。详细安装选项见[安装说明](../UPM_INSTALLATION.md)。

## 2. 脚本需要哪些引用和成员

```csharp
using System.Collections;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

// 这两个组件会随本脚本自动添加，无需自己在Inspector中配置。
[RequireComponent(typeof(HumanVisionManager), typeof(VideoPlayerFrameSource))]
public sealed class SdkBasicUsage : MonoBehaviour
{
    private HumanVisionManager manager;       // 初始化与身体结果
    private VideoPlayerFrameSource bridge;    // 把输入图像自动提交给SDK
    private IHumanVisionFrameSource source;   // 相机/视频/RTSP输入

    // 后面的Start、Initialize、OnResult和StopSdk放在这个类中。
}
```

`HumanVisionManager`是你最常用的SDK入口。这里的`VideoPlayerFrameSource`保留了历史类名，也能桥接摄像头输入。`Configure(manager, null, null)`表示本例只取数据，不绑定画面显示。

## 3. void Start中写什么

```csharp
private void Start()
{
    manager = GetComponent<HumanVisionManager>();
    bridge = GetComponent<VideoPlayerFrameSource>();
    bridge.Configure(manager, null, null);
    StartCoroutine(Initialize());
}
```

Start取得组件并启动初始化协程。准备模型资源需要等待，所以从`void Start()`调用`StartCoroutine`。不能在Prepare尚未完成时直接初始化。

## 4. 怎样初始化SDK

```csharp
private IEnumerator Initialize()
{
    string root = null, error = null;
    yield return HumanVisionRuntimeData.Prepare(
        value => root = value, value => error = value);
    if (string.IsNullOrEmpty(root)) {
        Debug.LogError(error);
        yield break;
    }

    if (!manager.TryInitialize(new HumanVisionConfig {
        RuntimeRoot = root,
        Profile = "windows-pc-cpu",
        MaxBodies = 1
    })) {
        Debug.LogError(manager.LastError);
        yield break;
    }

    manager.ResultUpdated += OnResult;
    // 初始化成功后，接上下一节的打开输入代码。
}
```

| 代码/API | 作用 |
| --- | --- |
| `HumanVisionRuntimeData.Prepare` | 校验并准备随包运行资源，成功回调返回根目录root |
| `RuntimeRoot = root` | 告诉SDK到哪里读取已经准备好的运行配置/模型；用回调路径，不写死电脑路径 |
| `Profile = "windows-pc-cpu"` | 首次Windows接入使用CPU配置；DirectML可使用`windows-pc-directml` |
| `MaxBodies = 1` | 最大人数容量；要读多人改为2～8，不是当前检测人数 |
| `TryInitialize` | 创建识别会话；true表示成功，false时读LastError并停止后续打开输入 |
| `ResultUpdated += OnResult` | 订阅完成结果，在OnResult中读取身体/关节 |

完整文件额外处理停止时正在准备资源的情况，并在Android上使用`Profile="auto"`读取APK构建配置。**初始化成功仅表示会话就绪；还需要输入图像才能产生骨骼。**

## 5. 怎样让SDK得到摄像头图像

把这段放在Initialize成功后、订阅ResultUpdated之后：

```csharp
source = gameObject.AddComponent<WebCameraFrameSource>();
source.Open(new HumanVisionSourceSettings {
    Kind = InputKind.WebCamera,
    DeviceName = "",                  // 空字符串使用默认相机
    RequestedWidth = 1280,
    RequestedHeight = 720,
    RequestedFramesPerSecond = 30
});
bridge.BindUnifiedSource(source);
```

Open开始打开摄像头，可能需要等系统权限和首帧；`source.State == InputSourceState.Streaming`说明输入开始输出，失败看`source.LastError`。尺寸/FPS是请求值，实际由设备决定。

BindUnifiedSource之后，桥会自动提交新图像，Manager自动轮询完成结果。**不用自己在Update中等待推理，也不用每帧手动调用SubmitFrame。**

## 6. 怎样读取人数、人物ID和骨骼关节

```csharp
private void OnResult(long sequence)
{
    if (!bridge.CanPresentResult(manager.SourceFrameId)) return;
    int count = manager.BodyCount;
    for (int i = 0; i < count; i++) {
        HumanVisionBody body = manager.Bodies[i];
        long personId = body.StableTrackId;
        HumanVisionCanonicalJoint wrist = body.CanonicalJoints[
            (int)HumanVisionCanonicalJointId.WristLeft];
        if (!wrist.Position.Valid || wrist.Position.Confidence < 0.35f) continue;

        Vector2 pixel = wrist.Position.Pixel;
        Vector2 normalized = wrist.Position.Normalized;
        // 在这里使用personId和坐标，编写自己的交互逻辑。
    }
}
```

| 读取内容 | 含义 |
| --- | --- |
| `manager.BodyCount` | 这次结果的身体数量；0表示本次未检测到人 |
| `manager.Bodies[i]` | 这次结果中的第i个身体；只遍历BodyCount，不遍历数组容量 |
| `body.StableTrackId` | 跟踪身份；Bodies的数组位置可能改变，不能把i当固定玩家ID |
| `CanonicalJoints[WristLeft]` | 人物左手腕，取枚举对应槽位；换成KneeLeft/ShoulderRight等即可读其他点 |
| `Position.Valid` | 该关节点是否可用；为false时不要使用其默认坐标 |
| `Position.Confidence` | 置信度；0.35是本例的读取过滤阈值，可按用途调整 |
| `Position.Pixel` | 图像平面像素坐标 |
| `Position.Normalized` | 图像平面归一化坐标，左上(0,0)、右下(1,1)，Y向下 |

例如读取左膝，把枚举替换为：

```csharp
var knee = body.CanonicalJoints[(int)HumanVisionCanonicalJointId.KneeLeft];
if (knee.Position.Valid && knee.Position.Confidence >= 0.35f) {
    Vector2 kneePosition = knee.Position.Normalized;
}
```

`body`是上面循环取得的身体；这个片段也放在结果回调中。全部关节及字段含义见[API关节点表](API_REFERENCE.md#53-32语义关节点逐项)。

完整文件按身体sequence去重，并把演示日志限制为每秒一次；实际交互代码仍可在每个新结果里读值。SDK会复用身体/关节数组，保留历史时复制所需数值。坐标是RGB图像坐标，不是米制3D；当前随包配置关闭真实Hand/Handtip/Thumb任务，手腕与真实手端点要区分。

## 7. 运行后怎样判断用对了

1. 关闭占用相机的软件，Play，允许相机权限。
2. Console看到`SDK initialized: windows-pc-cpu`表示会话初始化成功。
3. 站到摄像头前，头、躯干、手臂尽量清晰入镜；Console应出现`Detected bodies: 1`和有效左腕的`ID=... left wrist=(...)`日志。
4. 无人时BodyCount=0正常；有身体但左腕被遮挡时，可能没有左腕日志。
5. 只有初始化日志、没有结果时，先检查`source.State`、`source.LastError`、`bridge.LastError`和`manager.LastError`；没有绑定输入不会产生骨骼。

本例先通过日志学习API。需要显示相机和骨骼时，可继续看[可选显示/示例/打包教程](DISPLAY_AND_BUILD.md)，或使用SDK的统一示例。

## 8. 不再使用时怎样关闭

完整文件提供`StopSdk()`协程。自己的场景退出逻辑先等待它完成：

```csharp
yield return starter.StopSdk(); // starter是绑定的SdkBasicUsage组件引用
// 再切换场景或销毁这个物体。
```

里面的必要顺序是：

```csharp
manager.ResultUpdated -= OnResult;
bridge.DetachUnifiedSource();
while (bridge.UnifiedRetirementPending) yield return null;
source.Close();
manager.Shutdown();
```

先取消结果订阅，解绑并等待源复制退休，再关闭相机和会话。完整文件处理空引用与重复停止。正常切场景时先等StopSdk；OnDestroy的最后解绑是退出编辑器Play时的收尾，不替代上述等待。

## 9. 可选：Hierarchy右键创建启动物体

如果想避免每次创建空物体再拖脚本：

1. 先把完整`SdkBasicUsage.cs`放到`Assets/Scripts`。
2. 再把[SdkBasicUsageMenu.cs](examples/Editor/SdkBasicUsageMenu.cs)放到`Assets/Editor`。目录必须叫Editor，让Unity只在编辑器编译它。
3. 编译结束，在Hierarchy右键选择**Human Vision → SDK Starter**，或顶部**GameObject → Human Vision → SDK Starter**。
4. 自动生成的物体已挂上SdkBasicUsage与所需组件，Play就会执行本页的初始化/相机/结果流程。

这个菜单由你复制的示例编辑器脚本提供；只安装preview.4包还不会出现它。它不创建Canvas或预览界面。

## 10. 换输入和平台时查哪里

- Video：用`VideoFrameSource`，Open设置`Kind=InputKind.Video`和可读取的`Location`，再绑定同一个桥。
- RTSP：用`RtspFrameSource`与`RtspSourceSettings`，填写可访问的H.264/TCP地址，再绑定桥。参数和状态见[输入API](API_REFERENCE.md#6-统一输入包api)。
- Android：构建设置选择IL2CPP/ARM64/API26+、Vulkan、Human Vision的NCNN Vulkan路线；完整例子用auto读取APK配置。当前GPU输入要求定向后横向16:9。操作细节见[Android构建](DISPLAY_AND_BUILD.md#13-新项目构建-android)。
- 多人/区域、质量、统计、完整返回值与错误说明：查[API文档](API_REFERENCE.md)。首次接入先完成上面的单人流程即可。
