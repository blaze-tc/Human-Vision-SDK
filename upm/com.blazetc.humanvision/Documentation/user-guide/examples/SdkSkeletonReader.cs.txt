using HumanVision;
using HumanVision.Demo;
using UnityEngine;

// Attach to the same VisionRoot as SdkCameraQuickStart. Reads new body observations.
[RequireComponent(typeof(HumanVisionManager), typeof(VideoPlayerFrameSource))]
public sealed class SdkSkeletonReader : MonoBehaviour
{
    [Range(0, 1)] public float minimumConfidence = 0.35f;
    public int BodyCount { get; private set; }
    public long FirstTrackId { get; private set; } = -1;
    public bool HasLeftWrist { get; private set; }
    public Vector2 LeftWristNormalized { get; private set; }

    private HumanVisionManager manager;
    private VideoPlayerFrameSource bridge;
    private long lastSequence = -1;
    private float nextLogTime;

    private void OnEnable()
    {
        manager = GetComponent<HumanVisionManager>();
        bridge = GetComponent<VideoPlayerFrameSource>();
        lastSequence = -1;
        manager.ResultUpdated += OnResult;
    }

    private void OnDisable()
    {
        if (manager != null) manager.ResultUpdated -= OnResult;
        ClearObservation();
    }

    private void Update()
    {
        // Do not leave consumer state valid after input stop or source detachment.
        if (manager == null || !manager.IsInitialized ||
            !bridge.CanPresentResult(manager.SourceFrameId)) ClearObservation();
    }

    private void OnResult(long sequence)
    {
        if (!manager.IsInitialized || !bridge.CanPresentResult(manager.SourceFrameId))
        {
            ClearObservation();
            return;
        }
        // A hand-only update may reuse the body sequence. Count a body observation once.
        if (sequence == lastSequence) return;
        lastSequence = sequence;
        ClearObservation();
        BodyCount = manager.BodyCount;
        for (int i = 0; i < BodyCount; i++)
        {
            HumanVisionBody body = manager.Bodies[i];
            HumanVisionCanonicalJoint wrist = body.CanonicalJoints[
                (int)HumanVisionCanonicalJointId.WristLeft];
            if (i == 0)
            {
                FirstTrackId = body.StableTrackId;
                HasLeftWrist = wrist.Position.Valid &&
                    wrist.Position.Confidence >= minimumConfidence;
                if (HasLeftWrist) LeftWristNormalized = wrist.Position.Normalized;
            }

            // Read any canonical point in the same way; copy values for history.
            HumanVisionCanonicalJoint knee = body.CanonicalJoints[
                (int)HumanVisionCanonicalJointId.KneeLeft];
            if (Time.unscaledTime >= nextLogTime)
            {
                Debug.Log($"sequence={sequence}, frame={manager.SourceFrameId}, " +
                    $"track={body.StableTrackId}, " +
                    $"wristValid={wrist.Position.Valid}, " +
                    $"wristConfidence={wrist.Position.Confidence:F2}, " +
                    $"wrist={wrist.Position.Normalized}, " +
                    $"kneeValid={knee.Position.Valid}, " +
                    $"observedUs={wrist.ObservationTimestampUs}", this);
            }
        }
        if (Time.unscaledTime >= nextLogTime)
        {
            if (BodyCount == 0) Debug.Log($"sequence={sequence}: no bodies", this);
            nextLogTime = Time.unscaledTime + 1f;
        }
    }

    private void ClearObservation()
    {
        BodyCount = 0;
        FirstTrackId = -1;
        HasLeftWrist = false;
        LeftWristNormalized = Vector2.zero;
    }
}
