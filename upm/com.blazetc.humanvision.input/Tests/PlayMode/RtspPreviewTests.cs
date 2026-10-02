using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace HumanVision.Input.Tests
{
    // Test-only owned processes. No service discovery, global process kill, or remote camera.
    public class RtspPreviewTests
    {
        [Serializable] public class Fixture { public string server, config, ffmpeg, video, evidence; public int port; }
        private Fixture fixture;
        private Process server, publisher;
        private StreamWriter serverLog, publisherLog;
        private GameObject owner;
        private RtspFrameSource source;
        private string Url => "rtsp://127.0.0.1:" + fixture.port + "/fixture";
        private Process Start(string executable, string arguments, string log, out StreamWriter writer)
        {
            writer = new StreamWriter(Path.Combine(fixture.evidence, log), true) { AutoFlush = true };
            var sink = writer;
            var info = new ProcessStartInfo(executable, arguments) { UseShellExecute = false, CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardError = true, RedirectStandardOutput = true };
            var process = new Process { StartInfo = info };
            process.OutputDataReceived += (_, e) => { if (e.Data != null) lock(sink) sink.WriteLine(e.Data); };
            process.ErrorDataReceived += (_, e) => { if (e.Data != null) lock(sink) sink.WriteLine(e.Data); };
            process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine();
            File.AppendAllText(Path.Combine(fixture.evidence, "owned-pids.txt"), process.Id + "|" + process.StartTime.ToUniversalTime().Ticks + "|" + executable + "\n");
            return process;
        }
        private IEnumerator StartServer()
        {
            server = Start(fixture.server, "\"" + fixture.config + "\"", "server.log", out serverLog);
            yield return new WaitForSecondsRealtime(0.7f);
            publisher = Start(fixture.ffmpeg, "-hide_banner -nostdin -re -stream_loop -1 -i \"" + fixture.video +
                "\" -an -c:v copy -f rtsp -rtsp_transport tcp " + Url, "publisher.log", out publisherLog);
            yield return new WaitForSecondsRealtime(0.7f);
            Assert.IsFalse(server.HasExited, "Owned RTSP server failed");
            Assert.IsFalse(publisher.HasExited, "Owned H264 TCP publisher failed");
        }
        private void StopServer()
        {
            Stop(ref publisher); Stop(ref server);
            publisherLog?.Dispose(); serverLog?.Dispose(); publisherLog = serverLog = null;
        }
        private static void Stop(ref Process process)
        {
            if (process == null) return;
            if (!process.HasExited) { process.Kill(); process.WaitForExit(); }
            process.Dispose(); process = null;
        }
        [UnitySetUp] public IEnumerator SetUp()
        {
            fixture = JsonUtility.FromJson<Fixture>(File.ReadAllText(Path.Combine(Application.streamingAssetsPath, "rtsp-fixture.json")));
            yield return StartServer();
            owner = new GameObject("RTSP input-only preview");
            source = owner.AddComponent<RtspFrameSource>();
        }
        [UnityTearDown] public IEnumerator TearDown()
        {
            if (owner != null) UnityEngine.Object.Destroy(owner);
            StopServer();
            yield return null;
        }
        private IEnumerator WaitForFrame(long after, ulong newerGeneration = 0)
        {
            float end = Time.realtimeSinceStartup + 18;
            while (Time.realtimeSinceStartup < end) {
                if (source.TryGetLatestFrame(after, out var frame) && frame.Generation > newerGeneration) yield break;
                yield return null;
            }
            Assert.Fail("No RTSP frame: " + source.State + " " + source.LastError);
        }
        [UnityTest] public IEnumerator PreviewWithoutInferencePackage()
        {
            Assert.IsNull(Type.GetType("HumanVision.HumanVisionManager, HumanVision.Runtime"));
            source.Open(new RtspSourceSettings { Location = Url, RequestedWidth = 1920, RequestedHeight = 1080 });
            yield return WaitForFrame(0);
            Assert.IsTrue(source.TryGetLatestFrame(0, out var frame));
            Assert.AreEqual(640, frame.Width); Assert.AreEqual(360, frame.Height);
            Assert.AreEqual(FrameTimestampKind.LocalDecode, frame.TimestampKind);
            Assert.AreEqual(FrameClockDomain.SourceLocalMonotonic, frame.SourceClockDomain);
            Assert.AreNotEqual(0, frame.SourceClockId);
            Assert.AreEqual(FrameColorSpace.Unknown, frame.ColorSpace);
            Assert.AreEqual(FrameRowOrigin.UnityBottomLeft, frame.RowOrigin);
            Assert.GreaterOrEqual(frame.PresentationTimestampUs, 0);
            var imageGo = new GameObject("raw image", typeof(RectTransform), typeof(RawImage), typeof(FramePreview));
            var preview = imageGo.GetComponent<FramePreview>(); preview.Bind(source);
            yield return null;
            Assert.AreSame(source.CurrentTexture, imageGo.GetComponent<RawImage>().texture);
            UnityEngine.Object.Destroy(imageGo);
            var originalTexture = frame.Texture;
            yield return WaitForFrame(frame.FrameId);
            Assert.AreSame(originalTexture, source.CurrentTexture);
            AssertPixels(false);
            Assert.IsTrue(source.TryGetLatestFrame(0, out frame));
            Assert.IsTrue(source.TryAcquireSourceCopyLease(in frame, out var lease));
            var fence = new HeldFence(); lease.RetireAfter(fence);
            source.Close();
            Assert.IsNull(source.CurrentTexture);
            Assert.Greater(source.PendingRetirementCount, 0);
            owner.SetActive(false);
            yield return new WaitForSecondsRealtime(0.2f);
            Assert.AreEqual(InputSourceState.Closing, source.State);
            fence.complete = true;
            float end = Time.realtimeSinceStartup + 6;
            while (source.State != InputSourceState.Stopped && Time.realtimeSinceStartup < end) yield return null;
            Assert.AreEqual(InputSourceState.Stopped, source.State);
            File.AppendAllText(Path.Combine(fixture.evidence, "frames.txt"), "preview requested=1920x1080 actual=640x360 sourceClock=" + frame.SourceClockId + " sourceUs=" + frame.SourceTimestampUs + " publishedUs=" + frame.PublishedTimestampUs + " PTS=" + frame.PresentationTimestampUs + "\n");
        }
        private class HeldFence : ISourceCopyFence { public bool complete; public bool IsComplete => complete; }
        [UnityTest] public IEnumerator WarmedNativeUploadDoesNotAllocateOrWaitForConsumer()
        {
            source.Open(new RtspSourceSettings { Location = Url });
            yield return WaitForFrame(0);
            yield return new WaitForSecondsRealtime(0.3f);
            // Measure only the real production poll/copy/upload call; coroutine/test assertions remain outside.
            var update = (Action)Delegate.CreateDelegate(typeof(Action), source,
                typeof(RtspFrameSource).GetMethod("Update", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic));
            source.enabled = false;
            // OnDisable closes; reopen with manual main-thread production ticks.
            source.Open(new RtspSourceSettings { Location = Url });
            float end = Time.realtimeSinceStartup + 10;
            while (source.CurrentTexture == null && Time.realtimeSinceStartup < end) { update(); yield return null; }
            Assert.IsNotNull(source.CurrentTexture);
            for (int i = 0; i < 8; ++i) { update(); yield return null; }
            source.TryGetLatestFrame(0, out var before);
            long allocations = 0; int frames = 0; long last = before.FrameId;
            end = Time.realtimeSinceStartup + 2;
            while (Time.realtimeSinceStartup < end) {
                long start = GC.GetAllocatedBytesForCurrentThread(); update();
                allocations += GC.GetAllocatedBytesForCurrentThread() - start;
                if (source.TryGetLatestFrame(last, out var next)) { frames++; last = next.FrameId; }
                yield return null;
            }
            Assert.Greater(frames, 20, "Real native decoding/publication must continue without an adapter/consumer");
            Assert.AreEqual(0, allocations, "Warmed main-thread poll/copy/upload must reuse all managed buffers");
            source.TryGetLatestFrame(before.FrameId, out var after);
            Assert.AreEqual(before.Generation, after.Generation);
            Assert.AreSame(before.Texture, after.Texture);
            File.AppendAllText(Path.Combine(fixture.evidence, "frames.txt"), "warmedUpload frames=" + frames + " managedBytes=" + allocations + " withoutConsumer=2s publicationAgeUs=" + (InputMonotonicClock.NowUs-after.PublishedTimestampUs) + "\n");
        }
        [UnityTest] public IEnumerator DestroyedSourceRetiresNativeWorkerWithoutTexture()
        {
            // Own a loopback socket that accepts TCP but never replies to RTSP: native Open really stalls.
            var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
            listener.Start();
            int port = ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
            var pending = typeof(RtspFrameSource).GetField("retiring", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
            try {
                source.Open(new RtspSourceSettings { Location = "rtsp://127.0.0.1:" + port + "/stalled", OpenTimeoutMs = 5000 });
                yield return new WaitForSecondsRealtime(.05f);
                Assert.IsNull(source.CurrentTexture);
                source.enabled = false; // real cancellation + Release BUSY, with no source textures/fences
                var handles = (IntPtr[])pending.GetValue(source);
                bool busy = false; for (int i = 0; i < handles.Length; ++i) busy |= handles[i] != IntPtr.Zero;
                Assert.IsTrue(busy, "Controlled stalled read must prove native Release BUSY, not just a zero-texture Close");
                UnityEngine.Object.Destroy(owner);
                yield return null;
                float end = Time.realtimeSinceStartup + 6;
                bool complete = false;
                while (Time.realtimeSinceStartup < end) {
                    complete = true; for (int i = 0; i < handles.Length; ++i) complete &= handles[i] == IntPtr.Zero;
                    if (complete) break; yield return null;
                }
                Assert.IsTrue(complete, "Persistent main-thread registration must poll native Release after destruction even with zero textures");
                File.AppendAllText(Path.Combine(fixture.evidence, "frames.txt"), "native-only destroyedSource ReleaseBUSY=true allHandlesRetired=true\n");
            } finally {
                listener.Stop();
            }
        }
        [UnityTest] public IEnumerator ConnectionLossReconnectsWithoutBlockingControls()
        {
            source.Open(new RtspSourceSettings { Location = Url, DisplayMirror = true, OpenTimeoutMs = 1200, ReconnectDelayMs = 200 });
            yield return WaitForFrame(0);
            source.TryGetLatestFrame(0, out var before); AssertPixels(true);
            var nativeBefore = NativeLatest();
            StopServer();
            int controls = 0; float maxUpdate = 0;
            float end = Time.realtimeSinceStartup + 8;
            while (source.State != InputSourceState.Reconnecting && Time.realtimeSinceStartup < end) {
                float start = Time.realtimeSinceStartup; controls++; yield return null;
                maxUpdate = Mathf.Max(maxUpdate, Time.realtimeSinceStartup - start);
            }
            Assert.AreEqual(InputSourceState.Reconnecting, source.State);
            Assert.Greater(controls, 0);
            // Keep the owned server offline for a full second while main-thread controls keep ticking.
            end = Time.realtimeSinceStartup + 1;
            while (Time.realtimeSinceStartup < end) {
                float start = Time.realtimeSinceStartup; controls++; yield return null;
                maxUpdate = Mathf.Max(maxUpdate, Time.realtimeSinceStartup - start);
            }
            Assert.Greater(controls, 10);
            Assert.IsNull(source.CurrentTexture, "Disconnected generations cannot expose stale preview pixels");
            yield return StartServer();
            yield return WaitForFrame(before.FrameId, before.Generation);
            source.TryGetLatestFrame(before.FrameId, out var after);
            var nativeAfter = NativeLatest();
            Assert.AreEqual(before.SourceId, after.SourceId);
            Assert.Greater(after.Generation, before.Generation); Assert.Greater(after.FrameId, before.FrameId);
            Assert.Greater(nativeAfter.Generation, nativeBefore.Generation);
            Assert.Greater(nativeAfter.Sequence, nativeBefore.Sequence);
            AssertPixels(true);
            var watch = Stopwatch.StartNew(); source.Close(); watch.Stop();
            Assert.Less(watch.ElapsedMilliseconds, 100, "Close must only request worker cancellation");
            File.AppendAllText(Path.Combine(fixture.evidence, "frames.txt"), "Streaming->Reconnecting->Streaming generation=" + before.Generation + "->" + after.Generation + " frame=" + before.FrameId + "->" + after.FrameId + " nativeGeneration=" + nativeBefore.Generation + "->" + nativeAfter.Generation + " nativeSequence=" + nativeBefore.Sequence + "->" + nativeAfter.Sequence + " controls=" + controls + " maxRenderInterval=" + maxUpdate + " closeMs=" + watch.ElapsedMilliseconds + "\n");
        }
        private NativeInputBindings.FrameInfo NativeLatest()
        {
            var handle = (IntPtr)typeof(RtspFrameSource).GetField("handle", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(source);
            var info = NativeInputBindings.FrameInfo.Create();
            Assert.AreEqual(0, NativeInputBindings.HV_Input_PollFrame(handle, 0, ref info));
            return info;
        }
        private void AssertPixels(bool mirror)
        {
            var target = (RenderTexture)source.CurrentTexture;
            var previous = RenderTexture.active;
            var readback = new Texture2D(target.width, target.height, TextureFormat.RGBA32, false, true);
            try {
                RenderTexture.active = target;
                readback.ReadPixels(new Rect(0, 0, target.width, target.height), 0, 0); readback.Apply();
                // encoded video corners: TL red, TR green, BL blue, BR white
                var tl = readback.GetPixel(mirror ? 624 : 16, 344);
                var bl = readback.GetPixel(mirror ? 624 : 16, 16);
                Assert.Greater(tl.r, .7f); Assert.Less(tl.g, .2f); Assert.Less(tl.b, .2f);
                Assert.Greater(bl.b, .7f); Assert.Less(bl.r, .2f);
                var tr = readback.GetPixel(mirror ? 16 : 624, 344);
                Assert.Greater(tr.g, .35f); Assert.Less(tr.r, .2f); Assert.Less(tr.b, .2f);
            } finally { RenderTexture.active = previous; UnityEngine.Object.Destroy(readback); }
        }
    }
}
