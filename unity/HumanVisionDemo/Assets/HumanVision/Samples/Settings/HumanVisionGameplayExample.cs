using System.Collections;
using HumanVision;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

/// <summary>一个空物体挂此脚本即可。创建配置资产并拖到 configuration；预览 RawImage 可选。</summary>
[RequireComponent(typeof(HumanVisionSdk))]
public sealed class HumanVisionGameplayExample : MonoBehaviour
{
    [SerializeField] private HumanVisionSdkPreset configuration;
    [SerializeField] private RawImage preview;
    private HumanVisionSdk sdk;
    private readonly Vector2[] points = new Vector2[32];
    private readonly bool[] valid = new bool[32];
    private void Awake()
    {
        sdk = GetComponent<HumanVisionSdk>();
        sdk.InitializeOnStart = false; // 由本脚本管理，避免重复启动。
        if (preview != null) sdk.ScreenTarget = preview.rectTransform;
    }
    private IEnumerator Start()
    {
        if (configuration == null) { Debug.LogError("请设置 SDK 配置资产。", this); yield break; }
        sdk.ResultUpdated += ReadSkeleton;
        // 空设备名使用默认相机；具体设备可传入 WebCamTexture.devices 中的 name。
        yield return sdk.InitializeFromPreset(configuration, InputKind.WebCamera, "");
        // RTSP: yield return sdk.InitializeFromPreset(configuration, InputKind.Rtsp, "rtsp://host:554/live");
        // Video: yield return sdk.InitializeFromPreset(configuration, InputKind.Video,
        //     Application.streamingAssetsPath + "/video-1.mp4");
        if (!sdk.IsRunning) Debug.LogError(sdk.LastError, this);
    }
    private void Update()
    {
        // 预览持续更新，不等待模型；借用纹理由 SDK 管理，不要 Destroy。
        if (preview != null) preview.texture = sdk.GetColorImageTex();
    }
    private void ReadSkeleton(long sequence)
    {
        // index 允许空位，应遍历容量，不能以实际人数作循环上限。
        for (int index = 0; index < sdk.GetMaxBodies(); index++) {
            if (!sdk.CopySkeleton2DByIndex(index, points, valid, out var source)) continue;
            int wrist = (int)HumanVisionCanonicalJointId.WristLeft;
            if (!valid[wrist]) continue;
            // points[wrist]：图像左上角为原点的像素坐标。
            // source.StableTrackId：身份；source.SourceFrameId：骨骼对应的输入帧。
            // 在此把数据交给游戏逻辑；跨回调保存时复制自己的缓冲，不保留 SDK 借用引用。
        }
    }
    /// <summary>退出业务前可等待资源释放；禁用/销毁 SDK 也会执行安全退役。</summary>
    public IEnumerator StopSdk() { yield return sdk.StopSdk(); }
    private void OnDestroy() { if (sdk != null) sdk.ResultUpdated -= ReadSkeleton; }
}
