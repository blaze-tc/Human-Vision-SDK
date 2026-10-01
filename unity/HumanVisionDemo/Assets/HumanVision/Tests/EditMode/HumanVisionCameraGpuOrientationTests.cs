using System;
using System.IO;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;
using HumanVision.Demo;

namespace HumanVision.Tests
{
    public sealed class HumanVisionCameraGpuOrientationTests
    {
        [Test]
        public void GpuCameraNormalizesRowsWithoutChangingUprightPreview()
        {
            var normalize = typeof(HumanVisionLiveSource).GetMethod("NormalizeGpuSourceRows", BindingFlags.NonPublic | BindingFlags.Static);
            Assert.NotNull(normalize, "The displayed bottom-origin texture cannot be submitted unchanged to the top-origin GPU model");
            var marker = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
            var preview = new RenderTexture(2, 2, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            var submitted = new RenderTexture(2, 2, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            var readback = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
            RenderTexture previous = RenderTexture.active;
            try {
                marker.filterMode = preview.filterMode = submitted.filterMode = FilterMode.Point;
                marker.SetPixels(new[] { Color.red, Color.green, Color.blue, Color.white }); marker.Apply();
                preview.Create(); submitted.Create(); Graphics.Blit(marker, preview);
                normalize.Invoke(null, new object[] { preview, submitted });
                RenderTexture.active = preview; readback.ReadPixels(new Rect(0, 0, 2, 2), 0, 0); readback.Apply();
                var upright = readback.GetPixels();
                RenderTexture.active = submitted; readback.ReadPixels(new Rect(0, 0, 2, 2), 0, 0); readback.Apply();
                var normalized = readback.GetPixels();
                for (int y = 0; y < 2; y++) for (int x = 0; x < 2; x++)
                    Assert.AreEqual(upright[(1 - y) * 2 + x], normalized[y * 2 + x], "Only row order changes; left/right and preview stay intact");
                Assert.False(submitted.sRGB, "The Vulkan source must use an admitted UNORM format");
            } finally {
                RenderTexture.active = previous;
                UnityEngine.Object.DestroyImmediate(marker); UnityEngine.Object.DestroyImmediate(readback);
                UnityEngine.Object.DestroyImmediate(preview); UnityEngine.Object.DestroyImmediate(submitted);
            }
        }

        [Test]
        public void LiveSourceLeasesNormalizedTextureAndPassesUprightPreviewSeparately()
        {
            string source = File.ReadAllText(Path.Combine(Application.dataPath, "HumanVision/Demo/Live/HumanVisionLiveSource.cs"));
            StringAssert.Contains("BeginAndroidGpuSourceLease(_gpuSource)", source);
            StringAssert.Contains("NormalizeGpuSourceRows(_oriented, _gpuSource)", source);
            StringAssert.Contains("SubmitExternalTexture(_gpuSource, timestamp, rotation, _settings.mirror, _oriented)", source);
            int release = source.IndexOf("private void ReleaseOrientedTexture()", StringComparison.Ordinal);
            Assert.Less(source.IndexOf("EndAndroidGpuSourceLease()", release, StringComparison.Ordinal), source.IndexOf("_gpuSource.Release()", release, StringComparison.Ordinal));
            foreach (string forbidden in new[] { "AsyncGPUReadback", "ReadPixels", "GetPixels" }) StringAssert.DoesNotContain(forbidden, source);
        }

        [Test]
        public void ExplicitGpuPreviewDrivesPresentationAndAspectWhileSourceGeometryMatches()
        {
            var go = new GameObject("preview contract");
            var display = new GameObject("display", typeof(RectTransform), typeof(RawImage), typeof(AspectRatioFitter));
            var preview = new RenderTexture(8, 4, 0);
            var submitted = new RenderTexture(8, 4, 0);
            try {
                var bridge = go.AddComponent<VideoPlayerFrameSource>();
                var manager = go.AddComponent<HumanVisionManager>();
                Type type = typeof(VideoPlayerFrameSource);
                display.GetComponent<AspectRatioFitter>().aspectMode = AspectRatioFitter.AspectMode.FitInParent;
                type.GetField("manager", BindingFlags.NonPublic | BindingFlags.Instance).SetValue(bridge, manager);
                type.GetField("targetDisplay", BindingFlags.NonPublic | BindingFlags.Instance).SetValue(bridge, display.GetComponent<RawImage>());
                type.GetField("aspectRatioFitter", BindingFlags.NonPublic | BindingFlags.Instance).SetValue(bridge, display.GetComponent<AspectRatioFitter>());
                // Exercise the real GPU branch with no native session. Submission is
                // rejected, while independent preview and source dimensions still update.
                var method = type.GetMethod("SubmitExternalGpuTexture", BindingFlags.NonPublic | BindingFlags.Instance);
                var parameters = method.GetParameters();
                Assert.AreEqual(typeof(Texture), parameters[parameters.Length - 1].ParameterType);
                object[] arguments = parameters.Length == 6
                    ? new object[] { submitted, 1L, 180, false, 0L, preview }
                    : new object[] { submitted, 1L, 180, false, preview };
                Assert.False((bool)method.Invoke(bridge, arguments));
                Assert.AreSame(preview, bridge.PresentationTexture);
                Assert.AreSame(preview, display.GetComponent<RawImage>().texture);
                Assert.AreEqual(2f, display.GetComponent<AspectRatioFitter>().aspectRatio);
                Assert.AreEqual(submitted.width, bridge.SourceWidth);
                Assert.AreEqual(submitted.height, bridge.SourceHeight);
                string source = File.ReadAllText(Path.Combine(Application.dataPath, "HumanVision/Demo/VideoPlayerFrameSource.cs"));
                StringAssert.Contains("PresentLiveTexture(previewTexture != null ? previewTexture : texture)", source);
                StringAssert.Contains("SourceWidth = texture.width; SourceHeight = texture.height;", source);
            } finally {
                UnityEngine.Object.DestroyImmediate(go); UnityEngine.Object.DestroyImmediate(display);
                UnityEngine.Object.DestroyImmediate(preview); UnityEngine.Object.DestroyImmediate(submitted);
            }
        }
    }
}
