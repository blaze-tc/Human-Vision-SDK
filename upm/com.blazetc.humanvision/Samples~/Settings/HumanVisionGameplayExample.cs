using System.Collections;
using HumanVision;
using UnityEngine;

/// <summary>最小完整示例：一个空物体挂本脚本即可。输入参数通过自动补齐的 SDK Inspector 设置。</summary>
[RequireComponent(typeof(HumanVisionSdk))]
public sealed class HumanVisionGameplayExample : MonoBehaviour
{
    private HumanVisionSdk sdk;
    private readonly HumanVisionCanonicalJoint[] snapshot = new HumanVisionCanonicalJoint[32];
    private void Awake() { sdk = GetComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false; }
    private IEnumerator Start()
    {
        sdk.ResultUpdated += ReadSkeleton;
        yield return sdk.Initialize();
        if (!sdk.IsRunning) Debug.LogError(sdk.LastError, this);
    }
    private void ReadSkeleton(long sequence)
    {
        for (int index = 0; index < sdk.GetMaxBodies(); index++) {
            if (!sdk.CopySkeletonByIndex(index, snapshot, out var metadata)) continue;
            if (sdk.TryGetJointWorldPosition(index, HumanVisionCanonicalJointId.WristLeft, out var point))
                Debug.Log("用户 " + metadata.StableTrackId + " 左腕平面世界位置 " + point);
            // snapshot 可复制到自己的历史存储；本数组下一次读取会覆盖。
        }
    }
    /// <summary>业务停止时可 yield return StopSdk()；禁用/销毁总控也有安全退役兜底。</summary>
    public IEnumerator StopSdk() { sdk.ResultUpdated -= ReadSkeleton; yield return sdk.StopSdk(); }
    private void OnDestroy() { if (sdk != null) sdk.ResultUpdated -= ReadSkeleton; }
}
