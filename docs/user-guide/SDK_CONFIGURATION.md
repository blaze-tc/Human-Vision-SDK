# ScriptableObject 配置与统一 API

适用当前预发布版本：SDK **0.4.0-preview.7** / Input **0.1.0-preview.5**。
这是当前版本的接入说明；历史 Release 的验证结果不自动适用于本次变更。

## 最小接入

Project 右键 **Create → Human Vision → SDK Configuration** 创建 `HumanVisionSdkPreset`。
配置图像尺寸、模型等级、CPU/GPU/NPU、人数和区域。地址可以留空，在初始化代码中提供。
菜单 **HumanVision → Create SDK** 自动创建总控并绑定配置资产（优先使用当前选中的配置资产，否则复用 Assets/HumanVisionSdk 下的默认资产）。也可以给空物体添加 `HumanVisionSdk`，关闭 `InitializeOnStart`，或直接使用
[完整脚本](examples/HumanVisionGameplayExample.cs)（自动补齐总控）。

```csharp
using System.Collections;
using HumanVision;
using HumanVision.Input;
using UnityEngine;

public class VisionStartup : MonoBehaviour
{
    public HumanVisionSdk sdk;
    public HumanVisionSdkPreset configuration;

    void Awake() { sdk.InitializeOnStart = false; }
    IEnumerator Start()
    {
        // 三种方式任选一种。摄像头为空设备名时选择默认设备。
        yield return sdk.InitializeFromPreset(configuration, InputKind.WebCamera, "");
        // yield return sdk.InitializeFromPreset(configuration, InputKind.Rtsp, "rtsp://host:554/live");
        // yield return sdk.InitializeFromPreset(configuration, InputKind.Video,
        //     Application.streamingAssetsPath + "/video-1.mp4");
        if (!sdk.IsRunning) Debug.LogError(sdk.LastError);
    }
}
```

初始化前还需修改本次人数、镜像等参数时，取得独立副本：

```csharp
var request = configuration.CreateOptionsForInput(InputKind.Rtsp, rtspAddress);
request.Recognition.MaxBodies = 4;
request.Recognition.UseRegions = true;
request.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(4);
request.Input.Mirror = true;
yield return sdk.Initialize(request);
```

也可以在总控的 `Configuration Asset` 中拖入资产，直接 `yield return sdk.Initialize()`。
未指定资产的旧场景继续读取内嵌配置。运行中的设置、`Configuration`、`ActiveConfiguration`
及资产 `CreateOptions()` 都是深副本，不会因修改数组/地址而污染资产。
`SetOptions()` 是显式修改资产内存的工具 API；编辑器保存还需 `SetDirty/SaveAssets`。

## 配置项

| 配置 | 含义 / 默认 | 生效方式 |
| --- | --- | --- |
| `Input.RequestedWidth/Height` | 1280×720；Inspector 提供 640×480、1280×720、1920×1080、3840×2160 | 初始化/重启 |
| `Input.RequestedFramesPerSecond` | 请求 30 FPS；不能增加上游真实帧率 | 初始化/重启 |
| `InputQuality` | 模型 Low/Medium/High；以 `GetInputQualityCapabilities()` 为准 | 重新初始化 |
| `AccelerationMode` | `Cpu` / `Graphics`（GPU）/ `Neural`（NPU） | 重新初始化；资产自动统一旧 Windows CPU 开关 |
| `Recognition.MaxBodies` | 槽位上限 1–8，默认 4；不是实际人数 | `TrySetMaxBodies`；NPU 容量变更用完整配置重新初始化 |
| `Recognition.UseRegions/Regions` | 默认关闭；开启时区域数等于容量、不可重叠 | `TryApplyConfiguration` 或区域 API |
| `MinimumJointConfidence` | 0.35；低分关节返回无效 | 应用识别配置 |
| `MaximumResultAgeMilliseconds` | 身体 1000 ms；过期不再确认有人 | 应用识别配置 |
| `MaximumHandAgeMilliseconds` | 手关节 200 ms；不是人体新结果帧率 | 应用识别配置 |
| `Input.Mirror` | 水平镜像；骨骼映射同步处理 | 重新初始化 |
| `RtspTcp` | 默认 TCP，false 为 UDP | 重新初始化 |
| `InputOpenTimeoutSeconds` | 默认 15 秒；APK 视频首次缓存另有有界准备等待 | 初始化 |
| `RuntimeRoot` | 留空自动准备安装资源 | 初始化 |
| `OpenInputOnInitialize` | 默认 true；false 只准备 Runtime，成功为 Ready | 初始化 |
| `ScreenTarget` / `WorldPlane` / `WorldPlaneSize` | 显示图像矩形 / 世界映射平面及大小 | 总控属性，可运行时修改 |

相机尺寸是硬件请求；RTSP 尺寸是解码后的本地输出上限；视频按源文件尺寸播放。
这些设置与模型分析尺寸不同。Android GPU 的低/中/高通常为 512×288、640×384、960×576；
CPU 和 NPU 必须以当前模型包的能力说明为准。NPU 仅支持已接入的 RK3588 路线，
不能在 OnePlus 上验证 RK3588 NPU；不支持的模式会报错，不静默切换。
旧代码直接创建 `HumanVisionSdkOptions` 时仍须注意 `UseWindowsCpu` 兼容开关；推荐从资产创建副本。

## 主要 API

所有 API 在 Unity 主线程调用。协程需 `yield return` 或 `StartCoroutine` 执行。

| API | 合同 |
| --- | --- |
| `Initialize()` / `Initialize(options)` | 准备资源、初始化并按配置打开输入；完成后检查 `IsRunning/LastError` |
| `InitializeFromPreset(asset, kind, address)` | 从资产读取默认值，代码覆盖本次源类型/地址 |
| `StartSdk()` / `RestartSdk()` | 显式启动输入（含仅准备过 Runtime 的情况）/ 按当前配置重新预检并重建；忙时拒绝重复操作 |
| `StopSdk()` / `Shutdown()` | 关闭、等待在途资源安全退役；不删除用户物体。`Destroy(sdk.gameObject)` 或禁用总控也自动清理 |
| `Configuration` / `ActiveConfiguration` | 草稿/最近成功应用配置的副本；后者未初始化为 null，停止后可用于重启 |
| `TryApplyConfiguration(options)` | 异步接受请求；true 不等于启动成功，观察状态和错误 |
| `GetMaxBodies()` / `TrySetMaxBodies(n)` | 容量；开启区域时建议一次提交完整人数+区域配置 |
| `GetUsersCount()` / `IsUserDetected(index)` | 当前有效人数 / 槽位是否有人；槽位可有空缺 |
| `GetUserIdByIndex()` / `GetUserIndexById()` | 稳定身份映射；未找到为 0 / -1 |
| `TryGetJointImagePosition()` | 图像像素坐标，左上原点 |
| `TryGetJointNormalizedPosition()` | 图像归一化坐标，左上原点；已包含显示变换 |
| `TryGetJointScreenPosition()` | Unity 屏幕像素，左下原点，使用实际图像 `ScreenTarget` |
| `TryGetJointWorldPosition()` | 映射到指定平面的 Unity Vector3；**不是实测人体三维深度** |
| `CopySkeleton2DByIndex()` / `CopySkeletonWorldByIndex()` | 复制 32 个坐标、有效标志、来源信息；数组预分配至少 32 项 |
| `CopySkeletonByIndex()` | 复制完整关节结构，包括置信度、观测时间、派生标志 |
| `TryGetDirectionBetweenJoints()` / `TryGetAngleAtJoint()` | 平面关节方向和角度（度） |
| `TrySetRegions()` / `TrySetRegionsEnabled()` | 复制区域 / 显式启用；不会暗中改人数或均分 |
| `GetRegionCount()` / `TryGetRegion()` / `CopyRegions()` | 读取配置区域（左上原点，0–1） |
| `TryGetRegionOccupancy(index, out occupied)` | 返回 false 表示未知；返回 true 时 occupied 区分有人/无人 |
| `IsRegionOccupied(index)` | 只能确认是否有人；未知也返回 false |
| `GetColorImageTex()` | 三种输入共用的最新预览纹理，SDK 持有，调用方不要销毁 |
| `GetColorImageWidth/Height()` | 实际纹理尺寸；无图像为 0 |
| `TryGetInputFrame(afterId, out frame)` | 最新不同预览帧及来源身份/时间/尺寸；换源后重置 afterId=-1 |
| `InputPublicationFps` / `FreshResultFps` / `Stats` | 输入发布率 / 新结果率 / 阶段统计；不等于逐人完整骨骼 FPS |
| `State/Busy/IsInitialized/IsRunning/HasFreshResult` | 生命周期与结果时效 |
| `ResultSequence/SourceFrameId/SourceTimestampUs/ResultAgeMilliseconds` | 原始结果序号及实际来源时序；不要与 Unity 渲染帧号混淆 |

`ResultUpdated` 仅新原始结果触发，包括明确无人结果；不是每个 Unity 帧回调。
此外提供 `Initialized`、`Stopped`、`ErrorOccurred`、`UserEntered/Exited`、`RegionOccupancyChanged`。
在 `ResultUpdated` 中遍历 `0..GetMaxBodies()-1`，用 `StableTrackId` 保存身份。
骨骼复制返回 true 只表示身体可用，仍要检查每点有效标志；无效/过期身体返回 false。
图像纹理和 `TryGetBodyByIndex` 的身体对象均为借用，纹理内容会更新；跨帧保存必须自己复制。
异步骨骼可能早于当前预览画面，使用结果来源帧/时间判断，不能凭一次读取假定二者同帧。
关闭区域绑定时，占用查询描述人员槽位，不代表某个空间矩形是否有人；需要空间区域判定时先配置并启用 `RegionsEnabled`。
当前 `SupportsMetric3D=false`；可返回 Vector3 平面映射，但没有真实距离/深度承诺。

## 唯一设置样例

菜单 **HumanVision → Create SDK settings demo assets** 创建 Settings 场景、Prefab 和配置资产。
已有同名场景不覆盖用户布局。设置界面第一次启动读取总控配置；已有用户保存文件优先。三种输入继承资产图像默认值，并独立保存。Settings 的应用操作始终打开输入；只准备 Runtime 的选项用于代码初始化。
SDK 仅从资产读取一次作为默认草稿；改变项目资产后，重新进入 Play Mode 或先停止后重新赋值。
旧场景可在总控上拖入新资产。只保留 Settings 样例入口，三种输入均在同一界面切换。
兼容旧公开类型与共享输入/渲染组件；删除的是其他样例资源和菜单，不改变 V1 ABI。
日志与复制日志文件夹能力继续保留，详见 [Settings 使用](SETTINGS_DEMO.md)。
