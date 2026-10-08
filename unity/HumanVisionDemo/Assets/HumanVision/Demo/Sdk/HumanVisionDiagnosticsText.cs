using System.Text;
using HumanVision.Interop;

namespace HumanVision.Demo
{
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
