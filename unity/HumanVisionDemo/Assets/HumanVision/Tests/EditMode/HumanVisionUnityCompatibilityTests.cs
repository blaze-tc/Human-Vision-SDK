using System;
using System.IO;
using System.Reflection;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionUnityCompatibilityTests
    {
        private Type Compatibility => typeof(FrameTextureNormalizer).Assembly.GetType("HumanVision.Input.HumanVisionUnityCompatibility");
        [Test] public void LegacyTextureApiIsNotRequiredByInputSources()
        {
            string repo = Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT");
            foreach (string file in Directory.GetFiles(repo + "/upm/com.blazetc.humanvision.input/Runtime", "*.cs", SearchOption.AllDirectories))
                Assert.False(File.ReadAllText(file).Contains(".isDataSRGB"), Path.GetFileName(file) + " requires a texture API absent in Unity 2021.3.18");
        }
        [Test] public void TextureEncodingDistinguishesSrgbAndLinearUploads()
        {
            Assert.NotNull(Compatibility, "Unity compatibility helper is missing.");
            var srgb = new Texture2D(4, 4, TextureFormat.RGBA32, false, false);
            var linear = new Texture2D(4, 4, TextureFormat.RGBA32, false, true);
            try {
                var query = Compatibility.GetMethod("IsSrgb");
                Assert.True((bool)query.Invoke(null, new object[] { srgb, true }));
                Assert.False((bool)query.Invoke(null, new object[] { linear, false }));
            } finally { UnityEngine.Object.DestroyImmediate(srgb); UnityEngine.Object.DestroyImmediate(linear); }
        }
        [Test] public void DefaultFontAndCanvasLabelLoadOnCurrentEditor()
        {
            Assert.NotNull(Compatibility);
            var font = (Font)Compatibility.GetProperty("DefaultFont").GetValue(null);
            Assert.NotNull(font);
            var root = new GameObject("font compatibility");
            try { Assert.That(InputPreviewCanvas.Label(root.transform, "Settings").font, Is.EqualTo(font)); }
            finally { UnityEngine.Object.DestroyImmediate(root); }
        }
    }
}
