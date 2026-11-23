# 常驻 SDK Prefab 实时预览与 Update 取点

在 Unity Package Manager 中打开 **Human Vision SDK → Samples**，导入 **Runtime Prefab Acceptance** 并打开
`RuntimePrefabAcceptance.unity`。进入 Play Mode，输入可访问的 RTSP 地址并连接。也可在 Inspector 绑定 Startup Preset，或通过项目默认资产配置摄像头、RTSP 或视频。左侧集中放置选项、控制按钮和诊断日志，右侧保留一个实时预览：最新 Texture、识别区域框和编号、骨骼点线叠加在同一画面。骨骼在每次 `Update` 直接查询 SDK 当前有效坐标，不等待纹理配对，也不检查帧率或结果序号。新结果到来前可以重复读取仍有效的坐标；失效、无人或停止时查询返回 false，样例清除对应点线。点击「全屏预览」可选择等比显示或填满裁切。
「拍照（精确同帧）」等待当前输入的一组新鲜精确配对，并将原色帧保存到应用的 `runtime-photos` 目录。离线包的样例位于 `Assets/HumanVision/Samples/RuntimePrefab`。
可在该场景的 Inspector 填写 RTSP URL 作为启动时预填地址。绑定 Startup Preset 时使用预设输入。
Android 使用 Vulkan、ARM64、IL2CPP，Player Settings 的 Internet Access 设为 Require。
RTSP 样例保持填写的当前码流地址，只在识别内部按模型尺寸分路。预览下方显示当前码流、解码或探测尺寸和实际预览尺寸；[配置文档](SDK_CONFIGURATION.md#android-rtsp-低档)中的自动选流说明属于旧路径，当前原图模式不启用它。

普通 RTSP 颜色接口 `Sdk.GetColorImageTex()`、`Sdk.TryGetInputFrame()` 和 `view.GetLiveTexture()` 使用当前地址实际解码后的可见原尺寸。模型档位只决定 SDK 内部的识别图像尺寸，不能把小模型输入放大后当预览。原图模式保持填写的 RTSP 地址，不自动改成低码流；如需使用子码流，请直接填写该码流地址。请求宽高仍可作为采集配置保存，但不再作为普通 RTSP 颜色预览的缩放上限。明确命名的 Analysis 接口仍是识别输入，不用于普通视频显示。

原尺寸支持依赖配套 SDK 和 Input 原生库。当前实现支持每轴不超过4096的可见画面，包括3840×2160；设备导入、分配或既有精确抓拍预算不支持时明确返回错误，不把缩小图伪装成原图。独立模式的可选预览失败不会停止健康的分析；旧兼容路径如无法分配原图与分析所需资源，保留原有输入失败语义。查看 SDK 的 `PreviewError` 和输入错误，而不是通过提高模型等级修复清晰度。

在 Unity 选择 **HumanVision → Create SDK**，菜单会把带配置资产的 `HumanVisionRuntime` Prefab 放在场景根级，默认进入 Play Mode 时按配置启动。也可直接使用包中的 `Runtime/Demo/Resources/HumanVisionRuntime.prefab`，或者用代码调用 `HumanVisionRuntimeView.Create()`；后两种方式由调用方显式启动。项目只需创建一次。场景切换时保留，重复创建时使用原实例。

```csharp
using System.Collections;
using HumanVision;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

public sealed class VisionGameInput : MonoBehaviour
{
    public HumanVisionSdkPreset preset;
    public RawImage preview;
    public string rtspUrl;

    private HumanVisionRuntimeView view;

    private IEnumerator Start()
    {
        view = HumanVisionRuntimeView.Create();
        view.PresentationMode = HumanVisionRuntimePresentationMode.Disabled;
        if (!view.Sdk.IsRunning && !view.Sdk.Busy)
            yield return view.Sdk.InitializeFromPreset(preset, InputKind.Rtsp, rtspUrl);
        if (!view.Sdk.IsRunning)
            Debug.LogError(view.Sdk.LastError);
    }

    private void Update()
    {
        var sdk = view != null ? view.Sdk : null;
        if (preview != null) preview.texture = sdk != null ? sdk.GetColorImageTex() : null;
        ClearPreviewPoints();
        if (sdk == null) return;
        for (int slot = 0; slot < sdk.GetMaxBodies(); slot++) {
            if (sdk.TryGetJointNormalizedPosition(slot, HumanVisionCanonicalJointId.Nose, out var nose))
                DrawPreviewPoint(sdk.GetUserIdByIndex(slot), nose);
        }
    }

    private void ClearPreviewPoints() { /* 隐藏上一轮未更新的池化点 */ }
    private void DrawPreviewPoint(long playerId, Vector2 normalized) { /* 左上角原点，归一化坐标 */ }
}
```

设置 `PresentationMode` 为 `Disabled` 可停止精确配对纹理捕获和合成，实时输入及识别回调继续运行。拍照时临时改为 `MatchedColor`，在当前来源代次的 `TryGetFrame` 成功后复制原色纹理，再恢复原模式。需要持续精确叠画时使用 SDK 默认的 `MatchedOverlay`。
叠画和拍照可订阅 `FramePresented`，事件在 SDK 主线程提交配对后触发。`TryGetFrame` 在相邻两次发布之间可以返回同一序号。配对结果带 `SourceId`、`SourceGeneration`、`SourceFrameId` 与 `ResultSequence`。
普通游戏可以在 `Update` 中直接用 `Sdk.TryGetJointNormalizedPosition`、`TryGetJointImagePosition` 或 `TryGetJointWorldPosition` 读取关节。需要整个人体坐标时，用 `CopySkeleton2DByIndex` / `CopySkeletonWorldByIndex` 并复用自己的数组。SDK 内部统一处理来源、结果时效、关节置信度和有效性；业务无需先检查 FPS、FrameId、ResultSequence 或分析 Texture。`HasFreshResult` 表示结果仍有效，不表示本次 Update 刚产生新结果。

只有需要“每个新观测处理一次”时才选择 `Sdk.ResultUpdated`；它不依赖纹理配对。姿态角度、抬手状态等可以每次 Update 计算。动作计次使用状态变化和时间去抖；速度等时间序列算法使用实际观测时间差，不能把多次重读同一坐标当成多个新观测。不要把 Demo 的统计或精确同帧拍照条件复制为普通取点前置条件。

| 入口 | 用途 |
| --- | --- |
| `view.Sdk` | 现有 SDK API：`Initialize`、`InitializeFromPreset`、`RestartSdk`、`StopSdk`、`GetMaxBodies`、`TrySetMaxBodies`，并读取 `State` / `LastError`。开启区域时必须同时匹配区域数量，详见[配置](SDK_CONFIGURATION.md)。 |
| `view.Sdk.TryGetJointNormalizedPosition` / `TryGetJointImagePosition` / `TryGetJointWorldPosition` | 普通业务在 Update 直接取当前有效坐标；不依赖实时 Texture 或精确配对。World 为 Unity XY 平面映射，不是实测深度。 |
| `TryGetFrame` | 返回精确配对的 `ColorTexture`、`BodyCount` 和帧身份。`MatchedOverlay` 同时提供 `OverlayTexture`。`MatchedColor` 的 `OverlayTexture` 为空。已确认无人时 `BodyCount == 0`。 |
| `view.TryGetBodyId` | 按精确配对的展示槽位读取稳定人体 ID。普通业务使用 `Sdk.GetUserIdByIndex`。槽位可不连续，遍历上限使用 `Sdk.GetMaxBodies()`。 |
| `view.TryGetJointPosition` / `TryGetJointTransform` | 读取精确配对展示所用的关节点对象及其 Unity 世界位置。它们依赖 Presentation，普通业务优先用上面的 Sdk 取点接口。 |
| `GetLiveTexture` / `Sdk.TryGetInputFrame` | 借用最新原色预览，可在首个识别结果前使用。后者还返回来源、代次、帧号和尺寸。 |
| `yield return view.Unload()` | 等待输入和 native 会话释放，再销毁常驻 Prefab。正常场景切换不调用；应用明确退出识别时调用。 |

精确配对 RuntimeView 的正交相机与关节根共用一个 XY 平面：中心为原点，高度为 1 个 Unity 单位，宽度等于画面宽高比。普通 `Sdk.TryGetJointWorldPosition` 则映射到配置的 WorldPlane／WorldPlaneSize。两者都是二维投影，不会因为有 Unity `Vector3` 而获得深度。镜像和旋转由输入处理一次。RuntimeView 相机默认禁用常规渲染、剔除层为零；其内部把画面和池化点线批量写到独立 RenderTexture。不要改动其相机、池化关节点或骨线对象。

返回的纹理及 Transform 由 SDK 持有并复用；跨帧留图或历史轨迹需要自行复制像素或坐标。识别忙时仍按 SDK 的最新帧策略跳过旧输入；这个接口防止画面和骨骼来自不同帧。等 GPU 副本、找不到对应副本、过期、换源、重启时，`TryGetFrame` 可暂时返回 false。没有 GPU fence 的环境 `SupportsMatchedFrames` 为 false，仍能使用原有输入/骨骼 API。应用被 OS 挂起时遵循原有 Unity/平台生命周期。

实现参考 [MediaPipeUnityPlugin 的异步 Pose Runner](https://github.com/homuler/MediaPipeUnityPlugin/blob/master/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkerRunner.cs) 和 [主线程 AnnotationController](https://github.com/homuler/MediaPipeUnityPlugin/blob/master/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Annotation/AnnotationController.cs)。本 SDK 额外校验输入和结果的实际帧身份。

## 按识别区域读取

在同一个项目或应用的 Settings 样例中启用区域，编辑区域后点击 **Save**。Runtime Prefab 场景启动时会加载
保存且已启用的区域布局。样例绑定的 Startup Preset 和项目默认资产优先。没有启用的区域配置时，样例使用四个等宽区域。
在 Demo 的「人数上限」下拉框可选择 1–8 人。更改人数后，Demo 为当前会话生成对应的等宽布局，并同时应用人数上限与区域数组。HUD 分别显示源解码、Texture 更新、送入识别、识别完成和骨骼交付帧率。CPU、GPU、NPU 选择以平台和已安装资源为准。

开启 `Sdk.RegionsEnabled` 后，`Sdk.GetUserIdByIndex(regionIndex)` 和
`Sdk.TryGetJointNormalizedPosition(regionIndex, joint, ...)` 的槽位就是零起始区域 index。
区域坐标归一化到原图，以左上角为原点。区域框、编号和骨骼显示在同一个实时预览中。连接前以 16:9 布局，连接后按实际输入宽高比绘制。
空区域的查询返回 false，后续区域的 index 保持固定。稳定玩家 ID 可用于区分同一区域先后进入的人。

```csharp
// 在游戏的 Update 中读取第二个配置区域，不依赖预览是否开启。
const int regionIndex = 1;
if (view != null && view.Sdk.TryGetJointNormalizedPosition(
        regionIndex, HumanVisionCanonicalJointId.Nose, out var nose))
{
    long playerId = view.Sdk.GetUserIdByIndex(regionIndex);
    UseJoint(playerId, nose);
}
```

## 观察骨骼跟随

对照 HUD 的源解码、Texture 和识别完成 FPS。预览持续取最新输入，Update 读取最近的有效骨骼；骨骼延迟不会要求视频等待。
相机、模型和 Unity 可以有不同更新速率。Unity 多次读取同一份有效骨骼是正常行为，不能把读取次数当成模型推理次数。模型帧率统计不参与业务取点条件。

历史 preview.9 的 OnePlus 9 Pro Exercise2 RTSP 短测中，输入发布和新识别平均均为 14.49 FPS，游戏更新平均 54.33 FPS；这是旧发布路径的口径，不是当前源解码帧率。
33 个采样窗口内，游戏 1,826 次 Update 均取得纹理与有效关节，得到 486 组不同配对。
这组数据对应本地验收 APK 和约 33 秒的设备观察。详见[发布验证](../reports/2026-11-06-runtime-prefab-preview9.md)。
