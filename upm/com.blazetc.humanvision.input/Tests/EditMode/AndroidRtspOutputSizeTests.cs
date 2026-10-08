using System;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Input.Tests
{
    /// <summary>RTSP 分辨率控制的是本地输出上限；保持视频比例，不放大低分辨率源。</summary>
    public sealed class AndroidRtspOutputSizeTests
    {
        private static Vector2Int Select(int width, int height, int maximumWidth, int maximumHeight)
        {
            var method = typeof(AndroidRtspGpuSource).GetMethod("SelectOutputSize", BindingFlags.Static | BindingFlags.NonPublic);
            Assert.That(method, Is.Not.Null, "Android RTSP must apply the requested output bounds before allocating/binding its GPU targets.");
            try { return (Vector2Int)method.Invoke(null, new object[] { width, height, maximumWidth, maximumHeight }); }
            catch (TargetInvocationException e) { throw e.InnerException; }
        }

        [TestCase(3840, 2160, 1280, 720, 1280, 720)]
        [TestCase(3840, 2160, 1920, 1080, 1920, 1080)]
        [TestCase(3840, 2160, 640, 480, 640, 360)]
        [TestCase(3840, 2160, 3840, 2160, 3840, 2160)]
        [TestCase(640, 360, 1920, 1080, 640, 360)]
        [TestCase(1280, 720, 1280, 720, 1280, 720)]
        [TestCase(1920, 1080, 1920, 720, 1280, 720)]
        [TestCase(1080, 1920, 1280, 720, 405, 720)]
        [TestCase(641, 481, 640, 480, 639, 480)]
        [TestCase(3840, 2160, 1, 1, 1, 1)]
        public void RequestedBoundsLimitGpuOutputWithoutStretchOrUpscale(int width, int height, int maximumWidth, int maximumHeight, int expectedWidth, int expectedHeight)
        {
            var output = Select(width, height, maximumWidth, maximumHeight);
            Assert.That(output, Is.EqualTo(new Vector2Int(expectedWidth, expectedHeight)));
            Assert.That(output.x, Is.InRange(1, Math.Min(width, maximumWidth)));
            Assert.That(output.y, Is.InRange(1, Math.Min(height, maximumHeight)));
        }

        [TestCase(0, 720, 1280, 720)]
        [TestCase(1280, 0, 1280, 720)]
        [TestCase(1280, 720, 0, 720)]
        [TestCase(1280, 720, 1280, -1)]
        public void InvalidGeometryIsRejectedBeforeAllocatingTextures(int width, int height, int maximumWidth, int maximumHeight)
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => Select(width, height, maximumWidth, maximumHeight));
        }
    }
}
