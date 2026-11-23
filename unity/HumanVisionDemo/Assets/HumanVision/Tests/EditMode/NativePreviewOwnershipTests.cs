using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using HumanVision.Demo;
using HumanVision.Input;
using HumanVision.Interop;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class NativePreviewOwnershipTests
    {
        private sealed class Reader : INativePreviewReader
        {
            internal ulong FrameId = 1;
            public string Diagnostics => "";
            public bool TryGetProgress(out HumanVisionRuntimeProgress progress) {
                progress = new HumanVisionRuntimeProgress { SourceId = 7, Generation = 9, ConfigEpoch = 3, Streaming = true, State = 2, InputFrameId = FrameId, NativeInputFrames = FrameId };
                return true;
            }
            public bool TryGetState(out uint state, out ulong frames) { state = 2; frames = FrameId; return true; }
            public int CopyPreview(ulong after, IntPtr buffer, uint capacity, ref RuntimePreviewNative info) {
                if (FrameId <= after) return RuntimeControlBindings.NoData;
                info.Width = info.Height = 2; info.StrideBytes = 8; info.RgbaBytes = 16;
                info.SourceId = 7; info.Generation = 9; info.ConfigEpoch = 3; info.FrameId = FrameId; info.TimestampUs = RuntimeBindings.HV_RuntimeClockUs();
                if (buffer == IntPtr.Zero || capacity < 16) return RuntimeControlBindings.BufferTooSmall;
                byte[] pixels = new byte[16];
                Color32 color = FrameId == 1 ? (Color32)Color.red : FrameId % 2 == 0 ? (Color32)Color.green : (Color32)Color.blue;
                for (int i = 0; i < 16; i += 4) { pixels[i] = color.r; pixels[i + 1] = color.g; pixels[i + 2] = color.b; pixels[i + 3] = 255; }
                Marshal.Copy(pixels, 0, buffer, pixels.Length); return 0;
            }
        }

        [Test] public void MatchedColorSurvivesMoreThanThreeLiveUploadsBeforeAnotherComposition()
        {
            Assert.That(SystemInfo.graphicsDeviceType, Is.Not.EqualTo(UnityEngine.Rendering.GraphicsDeviceType.Null), "Run this regression in a graphics-enabled Editor.");
            var owner = new GameObject("native preview ownership");
            var source = owner.AddComponent<HumanVisionNativeFrameSource>();
            var reader = new Reader(); source.Configure(reader, true, false);
            var clock = InputMonotonicClock.NowUs; Thread.Sleep(10);
            try {
                source.Tick();
                Assert.True(source.TryGetMatchedFrame(1, 7, 9, out var input));
                var presented = new HumanVisionPresentedFrame(7, 9, 1, 1, 1, 1, 0, input.Texture, null);
                Publish(source, input);
                Assert.That(GpuPixel(presented.ColorTexture).r, Is.GreaterThan(.95f));
                // This is the exact Texture object returned to RuntimeView. No new
                // composition/FramePresented occurs while live uploads keep advancing.
                for (ulong id = 2; id <= 9; ++id) { reader.FrameId = id; source.Tick(); }
                Assert.That(presented.InputFrameId, Is.EqualTo(1));
                Assert.That(presented.SourceFrameId, Is.EqualTo(1));
                var held = GpuPixel(presented.ColorTexture);
                Assert.That(held.r, Is.GreaterThan(.95f), "F1 identity must still refer to F1 pixels before the next composition.");
                Assert.That(held.g + held.b, Is.LessThan(.05f));
                Assert.True(source.TryGetLatestFrame(-1, out var live));
                Assert.That(live.FrameId, Is.EqualTo(9), "Holding a matched Unity color must not stop live/native input.");
                Assert.AreNotSame(presented.ColorTexture, live.Texture);

                Assert.True(source.TryGetMatchedFrame(9, 7, 9, out var next));
                Publish(source, next);
                for (ulong id = 10; id <= 15; ++id) { reader.FrameId = id; source.Tick(); }
                var nextHeld = GpuPixel(next.Texture);
                Assert.That(nextHeld.b, Is.GreaterThan(.95f), "Replacing the displayed pair protects its new slot.");
                Assert.That(nextHeld.r + nextHeld.g, Is.LessThan(.05f));
            }
            finally {
                // EditMode owns cleanup immediately; the production Close uses
                // deferred Unity destruction while the player is running.
                var slots = (Array)typeof(HumanVisionNativeFrameSource).GetField("slots", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(source);
                foreach (var slot in slots) {
                    var field = slot.GetType().GetField("Texture", BindingFlags.Instance | BindingFlags.NonPublic);
                    var texture = (Texture)field.GetValue(slot); if (texture != null) UnityEngine.Object.DestroyImmediate(texture); field.SetValue(slot, null);
                }
                UnityEngine.Object.DestroyImmediate(owner);
            }
        }
        private static void Publish(HumanVisionNativeFrameSource source, HumanVisionTextureFrame frame)
        {
            // Before the fix RuntimeView published directly, with no slot owner.
            // Keep the same behavioral regression runnable against that version.
            var protect = typeof(HumanVisionNativeFrameSource).GetMethod("ProtectMatchedFrame", BindingFlags.Instance | BindingFlags.NonPublic);
            if (protect != null) Assert.True((bool)protect.Invoke(source, new object[] { frame }));
        }
        private static Color GpuPixel(Texture source)
        {
            var target = RenderTexture.GetTemporary(2, 2, 0, RenderTextureFormat.ARGB32);
            var previous = RenderTexture.active; var copy = new Texture2D(2, 2, TextureFormat.RGBA32, false);
            try {
                Graphics.Blit(source, target); RenderTexture.active = target; copy.ReadPixels(new Rect(0, 0, 2, 2), 0, 0, false); return copy.GetPixel(0, 0);
            } finally { RenderTexture.active = previous; UnityEngine.Object.DestroyImmediate(copy); RenderTexture.ReleaseTemporary(target); }
        }
    }
}
