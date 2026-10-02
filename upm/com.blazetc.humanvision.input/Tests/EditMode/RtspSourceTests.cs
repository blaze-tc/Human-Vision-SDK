using System;
using System.Runtime.InteropServices;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Input.Tests
{
    public class RtspSourceTests
    {
        [Test] public void FailedBufferGrowthPreservesOwnershipUntilClose()
        {
            var go = new GameObject("RTSP controlled allocation failure");
            var source = go.AddComponent<RtspFrameSource>();
            typeof(UnityTextureFrameSource).GetMethod("Awake", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(source, null);
            int frees = 0;
            var pointer = new IntPtr(1234); // Counting allocator identity only: never passed to native/Unity.
            source.AllocateRgba = size => { if (size > 16) throw new OutOfMemoryException("controlled allocator failure"); return pointer; };
            source.FreeRgba = value => { Assert.AreEqual(pointer, value); frees++; };
            try {
                source.EnsureRgbaCapacity(16);
                Assert.Throws<OutOfMemoryException>(() => source.EnsureRgbaCapacity(32));
                int freesBeforeFail = frees;
                typeof(RtspFrameSource).GetMethod("Fail", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
                    .Invoke(source, new object[] { "RTSP frame upload failed; verify native plugin and available texture memory." });
                Assert.AreEqual(0, freesBeforeFail, "Failed replacement allocation must leave the old pointer owned and live");
                Assert.AreEqual(1, frees, "Actual Fail/Close must retire the valid old allocation exactly once");
                Assert.AreEqual(InputSourceState.Error, source.State);
                source.Close();
                Assert.AreEqual(1, frees, "Repeated Close must never repeat the allocation retirement");
                Assert.AreEqual(IntPtr.Zero, typeof(RtspFrameSource).GetField("rgba", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(source));
                Assert.AreEqual(0u, typeof(RtspFrameSource).GetField("capacity", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(source));
            } finally {
                UnityEngine.Object.DestroyImmediate(go);
                TestContext.WriteLine("Controlled buffer free count after destroy=" + frees);
            }
        }
        [Test] public void NativeLayoutsMatchVersionOne()
        {
            Assert.AreEqual(96, Marshal.SizeOf<NativeInputBindings.FrameInfo>());
            Assert.AreEqual(48, Marshal.SizeOf<NativeInputBindings.ClockInfo>());
            Assert.AreEqual(40, Marshal.SizeOf<NativeInputBindings.Options>());
            Assert.AreEqual(64, Marshal.OffsetOf<NativeInputBindings.FrameInfo>("ClockId").ToInt32());
            var clock = NativeInputBindings.ClockInfo.Create();
            Assert.AreEqual(0, NativeInputBindings.HV_Input_QueryClock(ref clock));
            Assert.AreNotEqual(0, clock.ClockId);
            Assert.AreEqual(1, clock.ClockDomain);
            Assert.Greater(clock.TicksPerSecond, 0);
        }
        [Test] public void InvalidSettingsFailWithoutEchoingCredentials()
        {
            var go = new GameObject("invalid RTSP");
            try {
                var source = go.AddComponent<RtspFrameSource>();
                // EditMode does not run MonoBehaviour.Awake; initialize the real source lifecycle explicitly.
                typeof(UnityTextureFrameSource).GetMethod("Awake", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(source, null);
                source.Open(new RtspSourceSettings { Location = "http://user:secret@localhost/path" });
                Assert.AreEqual(InputSourceState.Error, source.State);
                StringAssert.DoesNotContain("secret", source.LastError);
                StringAssert.DoesNotContain("user", source.LastError);
                StringAssert.Contains("rtsp", source.LastError);
            } finally { UnityEngine.Object.DestroyImmediate(go); }
        }
        [Test] public void MissingInputPluginIsActionable()
        {
            Assert.IsFalse(System.IO.Directory.Exists(System.IO.Path.Combine(Application.dataPath, "Plugins")),
                "This test must run in the runner's separate project without native plugins");
            var go = new GameObject("missing RTSP plugin");
            try {
                var source = go.AddComponent<RtspFrameSource>();
                typeof(UnityTextureFrameSource).GetMethod("Awake", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(source, null);
                source.Open(new RtspSourceSettings { Location = "rtsp://user:secret@127.0.0.1:1/fixture" });
                Assert.AreEqual(InputSourceState.Error, source.State);
                StringAssert.Contains("Install humanvision_input.dll", source.LastError);
                StringAssert.DoesNotContain("secret", source.LastError);
                Assert.IsNull(source.CurrentTexture);
            } finally { UnityEngine.Object.DestroyImmediate(go); }
        }
    }
}
