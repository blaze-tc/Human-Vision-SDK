using System;
using System.Runtime.InteropServices;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class AndroidGpuSourceRetirementTests
    {
        [Test] public void NativeTokenHasExactV2Layout()
        {
            Assert.That(Marshal.SizeOf<AndroidGpuSourceRetirementNative>(), Is.EqualTo(24));
            Assert.That(Marshal.OffsetOf<AndroidGpuSourceRetirementNative>("Generation").ToInt32(), Is.EqualTo(8));
            Assert.That(Marshal.OffsetOf<AndroidGpuSourceRetirementNative>("CopyToken").ToInt32(), Is.EqualTo(16));
        }
        [Test] public void EmptyCompletedRetirementRequiresNoNativeCall()
        {
            var fence = new HumanVisionAndroidSourceRetirement(default, null);
            Assert.That(fence.IsComplete, Is.True);
            Assert.That(fence.IsComplete, Is.True);
        }
        [Test] public void RealUnsupportedNativePollPreservesSourceAndError()
        {
            var texture = new RenderTexture(2, 2, 0);
            try
            {
                var token = new AndroidGpuSourceRetirementNative { Size = 24, Version = 2, Generation = 1, CopyToken = 1 };
                var fence = new HumanVisionAndroidSourceRetirement(token, texture);
                var error = Assert.Throws<HumanVisionException>(() => { var complete = fence.IsComplete; });
                Assert.That(error.ResultCode, Is.EqualTo(-6));
                Assert.That(texture, Is.Not.Null);
                // A failed poll never becomes a successful retirement on a later poll.
                Assert.Throws<HumanVisionException>(() => { var complete = fence.IsComplete; });
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }
    }
}
