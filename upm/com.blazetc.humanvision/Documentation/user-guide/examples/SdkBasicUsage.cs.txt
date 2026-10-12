using System.Collections;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

// Attach to one empty GameObject. No UI references are required.
[RequireComponent(typeof(HumanVisionManager), typeof(VideoPlayerFrameSource))]
public sealed class SdkBasicUsage : MonoBehaviour
{
    private HumanVisionManager manager;
    private VideoPlayerFrameSource bridge;
    private IHumanVisionFrameSource source;
    private long lastSequence = -1;
    private float nextLogTime;
    private string lastInputError;
    private bool stopping, stopped;

    private void Start()
    {
        manager = GetComponent<HumanVisionManager>();
        bridge = GetComponent<VideoPlayerFrameSource>();
        bridge.Configure(manager, null, null);
        StartCoroutine(Initialize());
    }

    private IEnumerator Initialize()
    {
        string root = null, error = null;
        yield return HumanVisionRuntimeData.Prepare(
            value => root = value, value => error = value);
        if (stopping) yield break;
        if (string.IsNullOrEmpty(root))
        {
            Debug.LogError("SDK resources: " + error, this);
            yield break;
        }

        string profile = Application.platform == RuntimePlatform.Android
            ? "auto" : "windows-pc-cpu";
        if (!manager.TryInitialize(new HumanVisionConfig
        {
            RuntimeRoot = root,
            Profile = profile,
            MaxBodies = 1
        }))
        {
            Debug.LogError("SDK initialize: " + manager.LastError, this);
            yield break;
        }

        manager.ResultUpdated += OnResult;
        source = gameObject.AddComponent<WebCameraFrameSource>();
        source.Open(new HumanVisionSourceSettings
        {
            Kind = InputKind.WebCamera,
            DeviceName = "",
            RequestedWidth = 1280,
            RequestedHeight = 720,
            RequestedFramesPerSecond = 30
        });
        bridge.BindUnifiedSource(source);
        Debug.Log("SDK initialized: " + manager.ActiveRuntimeProfile, this);
    }

    private void OnResult(long sequence)
    {
        if (stopping || sequence == lastSequence ||
            !bridge.CanPresentResult(manager.SourceFrameId)) return;
        lastSequence = sequence;

        // Teaching logs are limited to once a second. Use joint values here on each new result.
        bool log = Time.unscaledTime >= nextLogTime;
        if (log) Debug.Log("Detected bodies: " + manager.BodyCount, this);
        for (int i = 0; i < manager.BodyCount; i++)
        {
            HumanVisionBody body = manager.Bodies[i];
            HumanVisionCanonicalJoint wrist = body.CanonicalJoints[
                (int)HumanVisionCanonicalJointId.WristLeft];
            if (!wrist.Position.Valid || wrist.Position.Confidence < 0.35f) continue;
            Vector2 position = wrist.Position.Normalized;
            // Add your interaction here; copy values instead of retaining the body array.
            if (log) Debug.Log($"ID={body.StableTrackId}, left wrist={position}", this);
        }
        if (log) nextLogTime = Time.unscaledTime + 1f;
    }

    private void Update()
    {
        if (source == null || stopping) return;
        string error = string.IsNullOrEmpty(source.LastError) ? bridge.LastError : source.LastError;
        if (string.IsNullOrEmpty(error) || error == lastInputError) return;
        lastInputError = error;
        Debug.LogError("SDK input: " + error, this);
    }

    // Call and await before scene unload: yield return starter.StopSdk();
    public IEnumerator StopSdk()
    {
        if (stopping)
        {
            while (!stopped) yield return null;
            yield break;
        }
        stopping = true;
        if (manager != null) manager.ResultUpdated -= OnResult;
        if (bridge != null)
        {
            bridge.DetachUnifiedSource();
            while (bridge.UnifiedRetirementPending) yield return null;
        }
        if (source != null) source.Close();
        if (manager != null) manager.Shutdown();
        stopped = true;
    }

    private void OnDestroy()
    {
        // Final Editor teardown fallback; normal scene navigation must await StopSdk first.
        if (manager != null) manager.ResultUpdated -= OnResult;
        if (bridge != null) bridge.DetachUnifiedSource();
        if (source != null) source.Close();
    }
}
