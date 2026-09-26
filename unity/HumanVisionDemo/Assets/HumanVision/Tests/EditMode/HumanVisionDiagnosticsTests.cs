using System.Runtime.InteropServices;
using System.Text;
using System.IO;
using HumanVision.Demo;
using HumanVision.Interop;
using NUnit.Framework;

namespace HumanVision.Tests
{
    public sealed class HumanVisionDiagnosticsTests
    {
        [Test]
        public void AdditiveStatsLayoutDoesNotChangeV1()
        {
            Assert.AreEqual(80, Marshal.SizeOf<RuntimeStatsNative>());
            Assert.AreEqual(248, Marshal.SizeOf<RuntimeStatsV2Native>());
            Assert.AreEqual(8, Marshal.OffsetOf<RuntimeStatsV2Native>(
                nameof(RuntimeStatsV2Native.FreshObservationFrames)).ToInt32());
        }

        [Test]
        public void HudLabelsIdentifyFreshFramesAndActualBackend()
        {
            var text = new StringBuilder();
            HumanVisionDiagnosticsText.Append(text, new RuntimeStatsV2Native {
                FreshObservationFrames = 3, FreshObservationFps = 30,
                DetectorIntervalFrames = 4, CopyPath = 2,
                CaptureProvenance = 1,
                SensorCaptureAgeP50Ms = float.NaN,
                SensorCaptureAgeP95Ms = float.NaN
            }, "Profile=android-ncnn-vulkan\nActual backend=backend.ncnn.vulkan\n");
            StringAssert.Contains("Fresh observations/s: 30.0  (frames 3)", text.ToString());
            StringAssert.Contains("Unity-observed→publish lower bound P50/P95", text.ToString());
            StringAssert.Contains("Sensor capture age P50/P95: unavailable", text.ToString());
            StringAssert.Contains("Detector interval/age: 4 frames", text.ToString());
            StringAssert.Contains("Pose P50/P95", text.ToString());
            StringAssert.Contains("GPU/pose drops", text.ToString());
            StringAssert.Contains("Copy path: color attachment", text.ToString());
            StringAssert.Contains("Actual backend: backend.ncnn.vulkan", text.ToString());
            StringAssert.Contains("Source seen/rate limited", text.ToString());
            StringAssert.Contains("Detector attempted/completed/late/discarded", text.ToString());
        }

        [Test]
        public void UnknownClockDoesNotClaimUnityObservationOrSensorAge()
        {
            var text = new StringBuilder();
            HumanVisionDiagnosticsText.Append(text, new RuntimeStatsV2Native {
                CaptureProvenance = 0,
                SensorCaptureAgeP50Ms = float.NaN,
                SensorCaptureAgeP95Ms = float.NaN
            }, "");
            StringAssert.Contains("Clock provenance: unknown", text.ToString());
            StringAssert.Contains("Sensor capture age P50/P95: unavailable", text.ToString());
            Assert.False(text.ToString().Contains("Unity-observed→publish lower bound"));
        }

        [Test]
        public void CameraSceneRendersTheV2Diagnostics()
        {
            string scene = File.ReadAllText(Path.GetFullPath(Path.Combine(
                UnityEngine.Application.dataPath,
                "HumanVision/Demo/Live/HumanVisionSceneControls.cs")));
            StringAssert.Contains("HumanVisionDiagnosticsText.Append(text, pipeline.RuntimeStatsV2", scene);
        }
    }
}
