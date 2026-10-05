using System;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidQualityGpuRoutingTests
    {
        [TestCase("android-ncnn-vulkan")]
        [TestCase("android-ncnn-vulkan-quality-low")]
        [TestCase("android-ncnn-vulkan-quality-high")]
        public void AdmittedQualityUsesGpuSubmissionWithoutCpuFallback(string profile)
        {
            Assert.True(HumanVisionAndroidFrameRoute.UsesGpu(profile));
            Assert.AreEqual(HumanVisionAndroidFrameRoute.FramePath.Gpu, HumanVisionAndroidFrameRoute.Select(profile));
            var target = new Submission();
            Assert.False(HumanVisionAndroidFrameRoute.Submit(profile, target, null, 123, 90, true));
            Assert.AreEqual(1, target.GpuCalls);
            Assert.AreEqual(0, target.CpuCalls);
        }

        [TestCase("android-ort-cpu")]
        [TestCase("android-ort-xnnpack")]
        public void ExplicitCpuModesKeepCpuSubmission(string profile)
        {
            Assert.False(HumanVisionAndroidFrameRoute.UsesGpu(profile));
            Assert.AreEqual(HumanVisionAndroidFrameRoute.FramePath.Cpu, HumanVisionAndroidFrameRoute.Select(profile));
            var target = new Submission();
            Assert.True(HumanVisionAndroidFrameRoute.Submit(profile, target, null, 123, 0, false));
            Assert.AreEqual(0, target.GpuCalls);
            Assert.AreEqual(1, target.CpuCalls);
        }

        [TestCase("android-ncnn-vulkan-quality-high-extra")]
        [TestCase("android-ncnn-vulkan-quality-unknown")]
        [TestCase("android-ncnn-vulkan-quality-LOW")]
        [TestCase("android-ort-cpu-quality-high")]
        [TestCase("auto")]
        [TestCase(null)]
        public void UnknownQualityNeverInfersGpuOrCpuFallback(string profile)
        {
            Assert.False(HumanVisionAndroidFrameRoute.UsesGpu(profile));
            var target = new Submission();
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidFrameRoute.Submit(profile, target, null, 1, 0, false));
            Assert.AreEqual(0, target.GpuCalls + target.CpuCalls);
        }

        private sealed class Submission : IAndroidFrameSubmission
        {
            internal int GpuCalls, CpuCalls;
            public bool SubmitGpuFrame(Texture texture, long timestampUs, int rotationDegrees, bool mirrored) { ++GpuCalls; return false; }
            public bool SubmitCpuFrame(Texture texture, long timestampUs) { ++CpuCalls; return true; }
        }
    }
}
