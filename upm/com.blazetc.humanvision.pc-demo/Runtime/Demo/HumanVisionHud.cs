using System.IO;
using System.Text;
using UnityEngine;
using HumanVision.Interop;

namespace HumanVision.Demo
{
    [DefaultExecutionOrder(100)]
    [DisallowMultipleComponent]
    public sealed class HumanVisionHud : MonoBehaviour
    {
        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private VideoPlayerFrameSource frameSource;
        [SerializeField] private string onePersonVideo = "HumanVision/Media/d0_3_one_person.mp4";
        [SerializeField] private string multiPersonVideo = "HumanVision/Media/d0_3_two_people.mp4";

        private readonly StringBuilder _builder = new StringBuilder(768);
        private string _statusText = "HumanVision Demo starting...";
        private float _nextRefresh;

        public void Configure(
            HumanVisionManager visionManager,
            VideoPlayerFrameSource videoFrameSource,
            string onePersonRelativePath,
            string multiPersonRelativePath)
        {
            manager = visionManager;
            frameSource = videoFrameSource;
            onePersonVideo = onePersonRelativePath;
            multiPersonVideo = multiPersonRelativePath;
        }

        private void Update()
        {
            if (Time.unscaledTime < _nextRefresh)
            {
                return;
            }

            _nextRefresh = Time.unscaledTime + 0.25f;
            RefreshText();
        }

        private void OnGUI()
        {
            const float panelWidth = 430f;
            float panelHeight = manager != null && manager.UsesAndroidGpuFrames ? 585f : 285f;
            GUI.Box(new Rect(12f, 12f, panelWidth, panelHeight), "HumanVisionSDK D0.4");
            GUI.Label(new Rect(26f, 40f, panelWidth - 28f, panelHeight - 100f), _statusText);

            float controlsY = panelHeight - 55f;

            if (GUI.Button(new Rect(26f, controlsY, 34f, 26f), "-"))
            {
                manager?.TrySetMaxBodies(Mathf.Max(1, manager.MaxBodies - 1));
            }

            if (GUI.Button(new Rect(66f, controlsY, 34f, 26f), "+"))
            {
                manager?.TrySetMaxBodies(manager.MaxBodies + 1);
            }

            if (GUI.Button(new Rect(112f, controlsY, 86f, 26f), "1 Person"))
            {
                frameSource?.PlayRelativeVideo(onePersonVideo);
            }

            if (GUI.Button(new Rect(204f, controlsY, 86f, 26f), "2 People"))
            {
                frameSource?.PlayRelativeVideo(multiPersonVideo);
            }

            if (GUI.Button(new Rect(296f, controlsY, 120f, 26f), "Camera image"))
            {
                if (manager != null && manager.TrySetMaxBodies(8))
                    frameSource?.PlayRelativeVideo("HumanVision/Media/cameraImage.png");
            }

            GUI.Label(new Rect(26f, panelHeight - 23f, panelWidth - 28f, 22f),
                "MaxBodies changes at runtime; native DLL is not rebuilt.");
        }

        private void RefreshText()
        {
            _builder.Length = 0;
            if (manager == null || frameSource == null)
            {
                _builder.Append("Scene references are not configured.");
                _statusText = _builder.ToString();
                return;
            }

            HumanVisionStats stats = manager.Stats;
            float renderFps = Time.unscaledDeltaTime > 0f ? 1f / Time.unscaledDeltaTime : 0f;
            _builder.Append(frameSource.IsStillImage ? "Image (single inference): " : "Video: ");
            _builder.Append(string.IsNullOrWhiteSpace(frameSource.CurrentVideoPath)
                ? "not selected"
                : Path.GetFileName(frameSource.CurrentVideoPath));
            _builder.Append('\n');
            _builder.Append("Source: ").Append(frameSource.SourceWidth).Append('x').Append(frameSource.SourceHeight)
                .Append(" @ ").Append(frameSource.VideoFrameRate.ToString("F1")).Append(" fps\n");
            _builder.Append("Render: ").Append(renderFps.ToString("F1")).Append(" fps   Inference: ")
                .Append(stats.InferenceFps.ToString("F1")).Append(" fps\n");
            long resultAge = frameSource.LatestSubmittedFrameId >= manager.SourceFrameId && manager.SourceFrameId >= 0
                ? frameSource.LatestSubmittedFrameId - manager.SourceFrameId
                : -1;
            long videoDelay = frameSource.LatestSubmittedFrameId >= frameSource.PresentationFrameId &&
                frameSource.PresentationFrameId >= 0
                ? frameSource.LatestSubmittedFrameId - frameSource.PresentationFrameId
                : -1;
            _builder.Append("Video delay: ").Append(videoDelay)
                .Append(" frames   Result age: ").Append(resultAge).Append(" frames\n");
            _builder.Append("Bodies: ").Append(manager.BodyCount).Append(" / MaxBodies: ")
                .Append(manager.MaxBodies).Append("   Sequence: ").Append(manager.ResultSequence).Append('\n');
            _builder.Append("Detector: ").Append(stats.DetectionMs.ToString("F1")).Append(" ms   Pose: ")
                .Append(stats.PoseMs.ToString("F1")).Append(" ms\n");
            _builder.Append("Tracker: ").Append(stats.TrackingMs.ToString("F2")).Append(" ms   Total: ")
                .Append(stats.TotalMs.ToString("F1")).Append(" ms\n");
            _builder.Append("Submitted/Processed/Dropped: ").Append(stats.SubmittedFrames).Append(" / ")
                .Append(stats.ProcessedFrames).Append(" / ").Append(stats.DroppedFrames).Append('\n');
            _builder.Append("GPU readback pool drops/errors: ").Append(frameSource.ReadbackDrops).Append(" / ")
                .Append(frameSource.ReadbackErrors);
            if (manager.UsesAndroidGpuFrames)
                HumanVisionDiagnosticsText.Append(_builder, manager.RuntimeStatsV2, manager.RuntimeDiagnostics);
            if (manager.UsesRuntimeProfile && !manager.UsesAndroidGpuFrames)
                _builder.Append("\n").Append(manager.RuntimeDiagnostics);

            string error = !string.IsNullOrWhiteSpace(manager.LastError)
                ? manager.LastError
                : frameSource.LastError;
            if (!string.IsNullOrWhiteSpace(error))
            {
                _builder.Append("\nError: ").Append(error);
            }

            _statusText = _builder.ToString();
        }
    }

    internal static class HumanVisionDiagnosticsText
    {
        internal static void Append(StringBuilder builder, RuntimeStatsV2Native stats, string nativeDiagnostics)
        {
            builder.Append("\nFresh observations/s: ").Append(stats.FreshObservationFps.ToString("F1"))
                .Append("  (frames ").Append(stats.FreshObservationFrames).Append(")")
                .Append("\nGPU capture/s: ").Append(stats.GpuCaptureFps.ToString("F1"))
                .Append("  Output samples/s: ").Append(stats.OutputSamplingFps.ToString("F1"))
                .Append("\nSource seen/rate limited: ").Append(stats.SourceFramesSeen).Append(" / ")
                .Append(stats.SourceRateLimitedDrops)
                .Append(stats.CaptureProvenance == 1
                    ? "\nUnity-observed→publish lower bound P50/P95: "
                    : stats.CaptureProvenance == 2
                        ? "\nSensor capture→publish P50/P95: "
                        : "\nObservation→publish P50/P95 (clock unverified): ")
                .Append(stats.AgeP50Ms.ToString("F1")).Append(" / ")
                .Append(stats.AgeP95Ms.ToString("F1")).Append(" ms")
                .Append("\nClock provenance: ")
                .Append(stats.CaptureProvenance == 1 ? "Unity observed" : stats.CaptureProvenance == 2 ? "sensor verified" : "unknown")
                .Append("\nSensor capture age P50/P95: ");
            if (stats.CaptureProvenance == 2 &&
                !float.IsNaN(stats.SensorCaptureAgeP50Ms) && !float.IsNaN(stats.SensorCaptureAgeP95Ms))
                builder.Append(stats.SensorCaptureAgeP50Ms.ToString("F1")).Append(" / ")
                    .Append(stats.SensorCaptureAgeP95Ms.ToString("F1")).Append(" ms");
            else builder.Append("unavailable");
            builder
                .Append("\nDetector interval/age: ").Append(stats.DetectorIntervalFrames).Append(" frames / ")
                .Append(stats.DetectorAgeMs.ToString("F1")).Append(" ms")
                .Append("\nScheduled detector frame age P50/P95: ")
                .Append(stats.ScheduledDetectorFrameAgeP50Ms.ToString("F1")).Append(" / ")
                .Append(stats.ScheduledDetectorFrameAgeP95Ms.ToString("F1")).Append(" ms")
                .Append("\nDetector attempted/completed/late/discarded: ")
                .Append(stats.DetectorAttempted).Append(" / ").Append(stats.DetectorCompleted)
                .Append(" / ").Append(stats.DetectorLate).Append(" / ").Append(stats.DetectorDiscarded)
                .Append("\nPose P50/P95: ").Append(stats.PosePerBodyP50Ms.ToString("F1")).Append(" / ")
                .Append(stats.PosePerBodyP95Ms.ToString("F1")).Append(" ms/body")
                .Append("\nGPU/pose drops: ").Append(stats.GpuBridgeNoFreeSlotDrops).Append(" / ")
                .Append(stats.GpuBridgeSupersededReadyDrops).Append(" / ").Append(stats.PoseJobDrops)
                .Append("  copy/import errors ").Append(stats.GpuCopyErrors).Append(" / ")
                .Append(stats.GpuImportErrors)
                .Append("\nCopy path: ").Append(stats.CopyPath == 1 ? "blit" : stats.CopyPath == 2 ? "color attachment" : "unavailable")
                .Append("\nActual backend: ");
            const string marker = "Actual backend=";
            int start = nativeDiagnostics == null ? -1 : nativeDiagnostics.IndexOf(marker, System.StringComparison.Ordinal);
            if (start < 0) builder.Append("unavailable");
            else {
                start += marker.Length;
                int end = nativeDiagnostics.IndexOf('\n', start);
                builder.Append(nativeDiagnostics, start, (end < 0 ? nativeDiagnostics.Length : end) - start);
            }
        }
    }
}
