# Unity 总控使用（preview.5）

先安装 Input **0.1.0-preview.3**，等待编译，再安装 SDK **0.4.0-preview.5**。两包使用同一个 Git 标签 `v0.4.0-preview.5`。旧 API 保留。

## 一份组件即可启动

Hierarchy 右键 **Human Vision → Create SDK**，或者给空物体添加 **Human Vision SDK**。在 Inspector 选择摄像头、视频或 RTSP，设置人数（1–8）和区域，然后 Play。默认自动初始化。视频需填写路径，RTSP 需填写地址；停止、查询和调整均调用该组件。

自行管理启动时关闭 `InitializeOnStart`，等待初始化后检查状态：

```csharp
private IEnumerator Start()
{
    sdk.InitializeOnStart = false; // 更稳妥：在 Awake 或 Inspector 关闭
    sdk.ResultUpdated += ReadNewSkeleton;
    yield return sdk.Initialize(); // 自动 Prepare → Runtime → 输入
    if (!sdk.IsRunning) Debug.LogError(sdk.LastError);
}

private void ReadNewSkeleton(long sequence)
{
    // index 是槽位，允许空位；不能用实际人数作为循环上限。
    for (int index = 0; index < sdk.GetMaxBodies(); index++)
    {
        if (!sdk.IsUserDetected(index)) continue;
        long id = sdk.GetUserIdByIndex(index); // 身份保存用 StableTrackId
        if (sdk.TryGetJointScreenPosition(index, HumanVisionCanonicalJointId.WristLeft, out var pixel))
            Debug.Log($"{id}: 左腕屏幕坐标 {pixel}");
    }
}

private IEnumerator Stop()
{
    sdk.ResultUpdated -= ReadNewSkeleton;
    yield return sdk.StopSdk(); // 解绑 → 等待在途资源退役 → 关闭输入 → 释放 Runtime
}
```

可直接复制的完整脚本见 [HumanVisionGameplayExample.cs](examples/HumanVisionGameplayExample.cs)。只需要空物体和这个脚本；无需先创建 Canvas。

## 人数、区域和输入调整

```csharp
var recognition = sdk.Configuration.Recognition;
recognition.MaxBodies = 2;
recognition.UseRegions = true;
recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
if (!sdk.TryApplyConfiguration(recognition)) Debug.LogError(sdk.LastError);
// 一起提交，避免开启区域时先改人数导致区域数量不匹配。

if (sdk.TryGetRegionOccupancy(0, out bool hasPerson))
    Debug.Log(hasPerson ? "区域0有人" : "区域0无人");
else
    Debug.Log("未知：没有有效的新结果、停止、重连或结果过期");
```

识别配置同步提交，输入配置需要异步重启。`TryApplyConfiguration(HumanVisionSdkOptions)` 的 `true` 仅表示请求接受；等 `Busy` 结束并检查 `IsRunning` 和 `LastError`。协程 `Initialize(options)` 可直接等待。本次预检失败保留原输入；实际打开新输入失败会给出错误，不能把旧输入当作新配置成功。

## 坐标和结果合同

- 图像像素和归一化坐标以左上为原点，Y 向下。区域 `Rect` 同样如此，归一化范围 0–1，不允许重叠。
- `ScreenTarget` 指向实际视频 RawImage；屏幕坐标以屏幕左下为原点，排除 letterbox 空白。留空使用整个屏幕。
- `WorldPlane` 和 `WorldPlaneSize` 定义世界中的 XY 平面。世界位置是画面投影到该平面的虚拟坐标；RGB 模型不提供测量深度。
- 32 个 `HumanVisionCanonicalJointId` 是语义槽位。缺失、不可靠、过期的点查询返回 `false`，不能用默认零向量当有效点。目前随包 profile 不推理真实 Hand/Handtip/Thumb；API 不生成假手点。
- 输入像素已经做过旋转和镜像，调用者不要重复翻转。
- `ResultUpdated` 只对应新的原始观测；`TryGetSampledBodyByIndex` 单独读取平滑/预测数据。观测时间和预测时长仍保留。
- `HumanVisionBody` 和内部数组由 SDK 重用；在回调内立即消费，保留历史请用调用者预分配的 32 点数组 `CopySkeletonByIndex`。
- 所有 API 在 Unity 主线程调用。手点与人体分别检查时效，默认人体 1000ms，手点 200ms；均可配置。

停止/禁用/销毁组件会清除有效查询；禁用或销毁时由独立 Runtime Host 等待 GPU 在途资源退役。`Shutdown()` 与 `StopSdk()` 相同，不删除用户物体。

更多见 [API_REFERENCE](API_REFERENCE.md) 和 [设置场景 Demo](SETTINGS_DEMO.md)。
