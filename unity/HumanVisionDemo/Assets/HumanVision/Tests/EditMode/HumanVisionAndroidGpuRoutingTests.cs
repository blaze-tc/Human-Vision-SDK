using System;
using System.IO;
using System.Runtime.InteropServices;
using NUnit.Framework;
using UnityEngine;
using HumanVision.Demo;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidGpuRoutingTests
    {
        [TestCase("android-ncnn-vulkan", 0)]
        [TestCase("android-ort-xnnpack", 1)]
        [TestCase("android-ort-cpu", 1)]
        public void ExplicitModeSelectsExactlyOneSubmissionPath(string mode, int expected)
        {
            Assert.AreEqual(expected, (int)HumanVisionAndroidFrameRoute.Select(mode));
            var target = new FakeFrameSubmission();
            Assert.True(HumanVisionAndroidFrameRoute.Submit(mode, target, null, 17, 90, true));
            Assert.AreEqual(expected == 0 ? 1 : 0, target.GpuCalls);
            Assert.AreEqual(expected == 1 ? 1 : 0, target.CpuCalls);
        }

        [Test]
        public void UnknownModeNeverSelectsCpuFallback()
        {
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidFrameRoute.Select("auto"));
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidFrameRoute.Select("android-nnapi-nohands"));
        }

        [Test]
        public void GpuFailureIsReturnedWithoutCpuFallback()
        {
            var target = new FakeFrameSubmission { GpuResult = false };
            Assert.False(HumanVisionAndroidFrameRoute.Submit("android-ncnn-vulkan", target, null, 17, 0, false));
            Assert.AreEqual(1, target.GpuCalls);
            Assert.AreEqual(0, target.CpuCalls);
        }

        [Test]
        public void BridgePressureIsAReportedDropButOtherErrorsRemainFailures()
        {
            Assert.True(HumanVisionAndroidGpuResult.IsPressureDrop(1));
            Assert.False(HumanVisionAndroidGpuResult.IsPressureDrop(0));
            Assert.False(HumanVisionAndroidGpuResult.IsPressureDrop(-6));
        }

        [Test]
        public void ManagedGpuStructsMatchFrozenNativeLayout()
        {
            Assert.AreEqual(48, Marshal.SizeOf<AndroidGpuSubmissionNative>());
            Assert.AreEqual(56, Marshal.SizeOf<AndroidGpuSubmissionClockNative>());
            Assert.AreEqual(48, Marshal.OffsetOf<AndroidGpuSubmissionClockNative>(
                nameof(AndroidGpuSubmissionClockNative.CaptureSteadyUs)).ToInt32());
            Assert.AreEqual(128, Marshal.SizeOf<AndroidGpuBridgeStatusNative>());
        }

        [Test]
        public void GpuCameraAlwaysUsesLivePreviewEvenWhenLegacySmoothPreviewIsOff()
        {
            Assert.True(VideoPlayerFrameSource.UseLivePreview(false, true));
            Assert.False(VideoPlayerFrameSource.UseLivePreview(false, false));
        }

        [Test]
        public void GpuSubmissionRejectsTextureDifferentFromTheLeasedSource()
        {
            var leased = new RenderTexture(8, 8, 0);
            var other = new RenderTexture(8, 8, 0);
            try {
                Assert.DoesNotThrow(() => HumanVisionAndroidGpuFrameBridge.ValidateSource(leased, leased));
                Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidGpuFrameBridge.ValidateSource(leased, other));
            } finally { UnityEngine.Object.DestroyImmediate(leased); UnityEngine.Object.DestroyImmediate(other); }
        }

        [Test]
        public void UnconfiguredBridgeAndOtherNativeErrorsRemainVisibleInDiagnostics()
        {
            var unavailable = new AndroidGpuBridgeStatusNative { CopyPath = 0 };
            string pending = HumanVisionAndroidGpuFrameBridge.FormatDiagnostics(-6, unavailable,
                "Android Vulkan producer bridge is not configured");
            StringAssert.Contains("not configured", pending);
            StringAssert.Contains("-6", pending);
            string failure = HumanVisionAndroidGpuFrameBridge.FormatDiagnostics(-6, unavailable,
                "AHB import failed");
            StringAssert.Contains("AHB import failed", failure);
            StringAssert.Contains("-6", failure);
        }

        [Test]
        public void RuntimeReconfigurationRebindsTheStillLiveSource()
        {
            string session = File.ReadAllText(Path.GetFullPath(Path.Combine(UnityEngine.Application.dataPath,
                "HumanVision/Runtime/HumanVisionRuntimeSession.cs")));
            int reconfigure = session.IndexOf("public void ReconfigureMaxBodies", StringComparison.Ordinal);
            int preserve = session.IndexOf("RenderTexture activeGpuSource = _gpuSourceTexture;", reconfigure, StringComparison.Ordinal);
            int endOld = session.IndexOf("_gpuBridge?.Dispose();", preserve, StringComparison.Ordinal);
            int beginNew = session.IndexOf("BeginGpuSourceLease(activeGpuSource)", endOld, StringComparison.Ordinal);
            Assert.Greater(preserve, reconfigure);
            Assert.Greater(endOld, preserve);
            Assert.Greater(beginNew, endOld);
        }

        [Test]
        public void GpuBridgeContainsNoFullFrameCpuReadback()
        {
            string root = Path.GetFullPath(Path.Combine(UnityEngine.Application.dataPath, "HumanVision/Runtime/Android"));
            string source = File.ReadAllText(Path.Combine(root, "HumanVisionAndroidGpuFrameBridge.cs"));
            foreach (string forbidden in new[] { "AsyncGPUReadback", "GetPixels", "ReadPixels", "byte[]" })
                StringAssert.DoesNotContain(forbidden, source);
            string demo = File.ReadAllText(Path.GetFullPath(Path.Combine(UnityEngine.Application.dataPath,
                "HumanVision/Demo/VideoPlayerFrameSource.cs")));
            StringAssert.Contains("HumanVisionAndroidFrameRoute.Submit(manager.ActiveRuntimeProfile, this", demo);
            StringAssert.Contains("SubmitExternalGpuTexture", demo);
            string live = File.ReadAllText(Path.GetFullPath(Path.Combine(UnityEngine.Application.dataPath,
                "HumanVision/Demo/Live/HumanVisionLiveSource.cs")));
            int releaseMethod = live.IndexOf("private void ReleaseOrientedTexture()", StringComparison.Ordinal);
            int end = live.IndexOf("EndAndroidGpuSourceLease()", releaseMethod, StringComparison.Ordinal);
            int release = live.IndexOf("_oriented.Release()", releaseMethod, StringComparison.Ordinal);
            Assert.GreaterOrEqual(releaseMethod, 0);
            Assert.Greater(end, releaseMethod);
            Assert.Greater(release, end);
            Assert.AreEqual(1, Count(live, "_oriented.Release()"));
            Assert.AreEqual(3, Count(live, "ReleaseOrientedTexture();"));
        }

        private static int Count(string source, string token)
        {
            int count = 0, offset = 0;
            while ((offset = source.IndexOf(token, offset, StringComparison.Ordinal)) >= 0) { count++; offset += token.Length; }
            return count;
        }

        private sealed class FakeFrameSubmission : IAndroidFrameSubmission
        {
            internal int GpuCalls, CpuCalls;
            internal bool GpuResult = true;
            public bool SubmitGpuFrame(UnityEngine.Texture texture, long timestampUs, int rotationDegrees, bool mirrored)
            { GpuCalls++; return GpuResult; }
            public bool SubmitCpuFrame(UnityEngine.Texture texture, long timestampUs)
            { CpuCalls++; return true; }
        }
    }
}
