using System.Collections;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

// Windows x64 first-use example. Attach to VisionRoot in the tutorial scene.
[RequireComponent(typeof(HumanVisionManager), typeof(VideoPlayerFrameSource))]
public sealed class SdkCameraQuickStart : MonoBehaviour
{
    public RawImage preview;
    public AspectRatioFitter previewFitter;
    public HumanVisionOverlay overlay;
    [Range(1, 8)] public int maxBodies = 1;
    public string cameraDevice = "";

    public HumanVisionManager Manager { get; private set; }
    public VideoPlayerFrameSource Bridge { get; private set; }
    public string Status { get; private set; } = "Preparing";

    private WebCameraFrameSource source;
    private bool stopping;
    private bool stopCompleted;

    private IEnumerator Start()
    {
        Manager = GetComponent<HumanVisionManager>();
        Bridge = GetComponent<VideoPlayerFrameSource>();
        if (Application.platform != RuntimePlatform.WindowsEditor &&
            Application.platform != RuntimePlatform.WindowsPlayer)
        {
            Fail("This example uses Windows x64 CPU. Use the unified demos for Android.");
            yield break;
        }
        if (preview == null || previewFitter == null || overlay == null)
        {
            Fail("Assign Preview, Preview Fitter and Overlay in the Inspector.");
            yield break;
        }

        previewFitter.aspectMode = AspectRatioFitter.AspectMode.FitInParent;
        preview.raycastTarget = false;
        overlay.raycastTarget = false;
        overlay.enabled = false;
        Bridge.Configure(Manager, preview, previewFitter);

        string root = null, error = null;
        yield return HumanVisionRuntimeData.Prepare(
            value => root = value, value => error = value);
        if (stopping) yield break;
        if (string.IsNullOrEmpty(root))
        {
            Fail(error ?? "Runtime data preparation failed.");
            yield break;
        }
        if (!Manager.TryInitialize(new HumanVisionConfig
        {
            RuntimeRoot = root,
            Profile = "windows-pc-cpu",
            MaxBodies = maxBodies
        }))
        {
            Fail(Manager.LastError);
            yield break;
        }

        overlay.Configure(Manager, Bridge);
        overlay.ConfigureStyle(4.5f, 13.5f);
        overlay.enabled = true;
        source = gameObject.AddComponent<WebCameraFrameSource>();
        source.Open(new HumanVisionSourceSettings
        {
            Kind = InputKind.WebCamera,
            DeviceName = cameraDevice,
            RequestedWidth = 1280,
            RequestedHeight = 720,
            RequestedFramesPerSecond = 30,
            DisplayMirror = false
        });
        Bridge.BindUnifiedSource(source); // The bridge submits frames automatically.
        Status = "Opening camera";
        Debug.Log("SDK initialized: " + Manager.ActiveRuntimeProfile, this);
    }

    private void Update()
    {
        if (stopping || source == null) return;
        string error = source.LastError;
        if (string.IsNullOrEmpty(error)) error = Bridge.LastError;
        if (string.IsNullOrEmpty(error)) error = Manager.LastError;
        string next = string.IsNullOrEmpty(error) ? source.State.ToString() : error;
        if (Status == next) return;
        Status = next;
        if (!string.IsNullOrEmpty(error)) Debug.LogError(Status, this);
        else Debug.Log("Input: " + Status, this);
    }

    // Bind a UI Button to this method. This small example stops once; Play again to restart.
    public void StopVision()
    {
        if (!stopping) StartCoroutine(StopVisionRoutine());
    }

    // Before scene unload / Destroy: yield return quickStart.StopVisionRoutine().
    public IEnumerator StopVisionRoutine()
    {
        if (stopping)
        {
            while (!stopCompleted) yield return null;
            yield break;
        }
        stopping = true;
        Status = "Stopping";
        if (overlay != null) overlay.enabled = false;
        if (Bridge != null)
        {
            Bridge.DetachUnifiedSource();
            while (Bridge.UnifiedRetirementPending) yield return null;
        }
        if (source != null) source.Close();
        if (Manager != null) Manager.Shutdown();
        if (preview != null) preview.texture = null;
        Status = "Stopped";
        stopCompleted = true;
        Debug.Log("Input and SDK stopped", this);
    }

    private void Fail(string message)
    {
        Status = message;
        Debug.LogError(message, this);
    }

    private void OnDestroy()
    {
        // Editor teardown fallback. Normal navigation must await StopVisionRoutine first.
        if (Bridge != null) Bridge.DetachUnifiedSource();
        if (source != null) source.Close();
    }
}
