# 常驻 SDK Prefab 同帧图像与骨骼

在 Unity Package Manager 中打开 **Human Vision SDK → Samples**，导入 **Runtime Prefab Acceptance** 并打开
`RuntimePrefabAcceptance.unity`。进入 Play Mode，输入可访问的 RTSP 地址并连接。也可在 Inspector 绑定 Startup Preset，或通过项目默认资产配置摄像头、RTSP 或视频。左侧是实时输入和识别区域预览，连接前也能核对编号及布局。右侧可以
切换游戏端重绘与 SDK 合成。离线包的样例位于 `Assets/HumanVision/Samples/RuntimePrefab`。
运行时展开 Hierarchy 的 `API Redraw - Scene Rig → Skeleton Objects` 可选中 Mesh 关节点与 LineRenderer。
Android 使用 Vulkan、ARM64、IL2CPP，Player Settings 的 Internet Access 设为 Require。

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
        if (!view.Sdk.IsRunning && !view.Sdk.Busy)
            yield return view.Sdk.InitializeFromPreset(preset, InputKind.Rtsp, rtspUrl);
        if (!view.Sdk.IsRunning)
            Debug.LogError(view.Sdk.LastError);
    }

    private void Update()
    {
        if (view == null || !view.TryGetFrame(out var frame)) {
            preview.texture = null;
            return;
        }
        preview.texture = frame.OverlayTexture;
        for (int slot = 0; slot < view.Sdk.GetMaxBodies(); slot++) {
            if (view.TryGetBodyId(slot, out long playerId) &&
                view.TryGetJointPosition(slot, HumanVisionCanonicalJointId.Nose, out Vector3 nose))
                UseJoint(playerId, nose);
        }
    }

    private void UseJoint(long playerId, Vector3 nose) { /* 游戏使用此位置 */ }
}
```

如果希望只在新识别帧上做动作判断，在场景脚本启用时订阅 `FramePresented`，禁用时取消订阅。事件在 SDK 主线程提交整组绘制后触发；游戏的 `Update()` 读取最近一次完成发布的配对，轮询可在两个新结果之间读到相同序号。
可用 `SourceId`、`SourceGeneration`、`SourceFrameId`、`ResultSequence` 区分新观察，换源后清理游戏历史。处理游戏时间窗时使用帧号与时间戳，不能把同一个姿态算作多次独立观测。场景脚本若需要在自己的 LateUpdate 里读取当轮最新结果，可设置执行顺序晚于 200；事件没有此要求。

| 入口 | 用途 |
| --- | --- |
| `view.Sdk` | 现有 SDK API：`Initialize`、`InitializeFromPreset`、`RestartSdk`、`StopSdk`、`GetMaxBodies`、`TrySetMaxBodies`，并读取 `State` / `LastError`。开启区域时必须同时匹配区域数量，详见[配置](SDK_CONFIGURATION.md)。 |
| `TryGetFrame` | 返回同一组 `ColorTexture`（原色画面）、`OverlayTexture`（同一画面加骨骼）、`BodyCount`、原输入 ID、提交 ID、结果序号、来源及代次。已确认无人是有效画面，`BodyCount == 0`。 |
| `TryGetBodyId` | 按槽位读取稳定人体 ID。槽位可不连续，遍历上限使用 `Sdk.GetMaxBodies()`。 |
| `TryGetJointPosition` / `TryGetJointTransform` | 读取实际参与绘制的关节点对象及其 Unity 世界位置。返回 false 表示没有有效点。 |
| `GetLiveTexture` | 最新原色预览，可在首个识别结果前使用，也可能比骨骼更新。 |
| `yield return view.Unload()` | 等待输入和 native 会话释放，再销毁常驻 Prefab。正常场景切换不调用；应用明确退出识别时调用。 |

正交相机与关节根共用一个 XY 平面：中心为原点，高度为 1 个 Unity 单位，宽度等于画面宽高比。Y 轴向上。位置是二维投影；不会因为有 Unity `Vector3` 而获得深度。镜像和旋转由输入处理一次。相机默认禁用常规渲染、剔除层为零；SDK 用相机的投影把画面和池化点线批量写到独立 RenderTexture。游戏只有在把纹理接给 `RawImage` 等对象后才显示。不要改动相机、池化关节点或骨线对象。

返回的纹理及 Transform 由 SDK 持有并复用；跨帧留图或历史轨迹需要自行复制像素或坐标。识别忙时仍按 SDK 的最新帧策略跳过旧输入；这个接口防止画面和骨骼来自不同帧。等 GPU 副本、找不到对应副本、过期、换源、重启时，`TryGetFrame` 可暂时返回 false。没有 GPU fence 的环境 `SupportsMatchedFrames` 为 false，仍能使用原有输入/骨骼 API。应用被 OS 挂起时遵循原有 Unity/平台生命周期。

实现参考 [MediaPipeUnityPlugin 的异步 Pose Runner](https://github.com/homuler/MediaPipeUnityPlugin/blob/master/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkerRunner.cs) 和 [主线程 AnnotationController](https://github.com/homuler/MediaPipeUnityPlugin/blob/master/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Annotation/AnnotationController.cs)。本 SDK 额外校验输入和结果的实际帧身份。

## 按识别区域读取

在同一个项目或应用的 Settings 样例中启用区域，编辑区域后点击 **Save**。Runtime Prefab 场景启动时会加载
保存且已启用的区域布局。样例绑定的 Startup Preset 和项目默认资产优先。没有启用的区域配置时，样例使用四个等宽区域。
在 Demo 的「区域数量」下拉框可选择 1–8 人。更改人数后，Demo 为当前会话生成对应的等宽布局，并同时应用人数上限与区域数组。HUD 分别显示输入、新识别、配对帧率和阶段耗时。CPU、GPU、NPU 选择以平台和已安装资源为准。

开启 `Sdk.RegionsEnabled` 后，`view.TryGetBodyId(regionIndex, ...)` 和
`view.TryGetJointPosition(regionIndex, joint, ...)` 的槽位就是零起始区域 index。
区域坐标归一化到原图，以左上角为原点。左侧输入画面叠加编号，颜色对应右侧游戏重绘的骨骼。连接前以 16:9 预览布局，连接后按实际输入宽高比绘制。
空区域的查询返回 false，后续区域的 index 保持固定。稳定玩家 ID 可用于区分同一区域先后进入的人。

```csharp
// 在游戏的 Update 中使用样例 RuntimeSkeletonReader。
if (reader.TryRead(view, out var frame))
{
    const int regionIndex = 1; // 第二个配置区域
    preview.texture = frame.ColorTexture;
    long playerId = reader.GetUserIdByRegionIndex(regionIndex);
    if (reader.TryGetJointPositionByRegionIndex(regionIndex, HumanVisionCanonicalJointId.Nose, out var nose))
        UseJoint(playerId, nose);
}
```

## 观察骨骼跟随

对照 HUD 的输入发布 FPS 和新识别 FPS，差值为输入发布减新识别。持续的正差表示有更多输入发布帧
未得到新识别，短时间窗口可因流水线跨窗出现负值。即使平均差接近零，低采样频率、最长配对间隔偏大、
结果偏旧或关节短暂失效也会影响动作观感。样例显示最长配对间隔和 Update 取数统计，配合右侧同帧画面观察。
左侧最新输入可比右侧更新。结果序号可用于识别每次新的原始观察，Unity 重读频率反映游戏能取到数据的频率。

OnePlus 9 Pro 的 Exercise2 RTSP 短测中，输入发布和新识别平均均为 14.49 FPS，游戏更新平均 54.33 FPS。
33 个采样窗口内，游戏 1,826 次 Update 均取得纹理与有效关节，得到 486 组不同配对。
这组数据对应本地验收 APK 和约 33 秒的设备观察。详见[发布验证](../reports/2026-11-06-runtime-prefab-preview9.md)。
