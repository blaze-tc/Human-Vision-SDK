using System;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
namespace HumanVision.Input
{
    // Main-thread owner of exactly three output textures. Native converter is serialized and nonblocking.
    internal sealed class AndroidRtspGpuSource
    {
        private readonly SourceRetirement retirement = new SourceRetirement();
        private readonly SourceGeneration timeline;
        private readonly InputRetirementRegistration registration;
        private readonly RenderTexture[] textures = new RenderTexture[3];
        private readonly IntPtr[] pointers = new IntPtr[3];
        private readonly ulong[] tokens = new ulong[3], sequences = new ulong[3];
        private readonly bool[] destroy = new bool[3];
        private readonly byte[] error = new byte[1024];
        private IntPtr handle, renderEvent;
        private ulong nativeGeneration, sequence, bindingGeneration;
        private long frameId;
        private uint width, height;
        private bool closing, rebinding, mirror;
        private int latestSlot = -1;
        internal InputSourceState State { get; private set; } = InputSourceState.Stopped;
        internal string LastError { get; private set; } = string.Empty;
        internal Texture CurrentTexture => closing ? null : timeline.CurrentTexture;
        internal bool Pending => handle != IntPtr.Zero || retirement.PendingResourceCount != 0;
        internal IntPtr Handle => handle;
        internal AndroidRtspGpuSource()
        {
            timeline = new SourceGeneration(retirement);
            registration = new InputRetirementRegistration(retirement, Progress, () => Pending);
        }
        internal bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) => timeline.TryGetLatestFrame(after, out frame);
        internal bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) => timeline.TryAcquireSourceCopyLease(in frame, out lease);
        internal void Open(HumanVisionSourceSettings settings, int timeout, int delay)
        {
            try { OpenCore(settings, timeout, delay); }
            catch (Exception ex) { Fail(ex is InvalidOperationException ? ex.Message : "Android RTSP initialization failed; verify input plugin and hardware capabilities."); throw; }
        }
        internal void Fail(string message)
        {
            Close(); State = InputSourceState.Error; LastError = message;
        }
        private void OpenCore(HumanVisionSourceSettings settings, int timeout, int delay)
        {
            if (Pending) throw new InvalidOperationException("RTSP GPU source is still retiring; wait for Closing to finish before reopening.");
            if (SystemInfo.graphicsDeviceType != GraphicsDeviceType.Vulkan) throw new InvalidOperationException("Android RTSP requires Vulkan and qualified AHB/sync-fd support.");
#if UNITY_ANDROID && !UNITY_EDITOR
            using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                if (version.GetStatic<int>("SDK_INT") < 29) throw new InvalidOperationException("This Android hardware codec qualification requires API29 device capability query; native ABI remains API26.");
            string selected = null;
            using (var list = new AndroidJavaObject("android.media.MediaCodecList", 0))
                foreach (var codec in list.Call<AndroidJavaObject[]>("getCodecInfos"))
                using (codec) {
                    if (codec.Call<bool>("isEncoder") || !codec.Call<bool>("isHardwareAccelerated")) continue;
                    foreach (string type in codec.Call<string[]>("getSupportedTypes")) if (type == "video/avc") { selected = codec.Call<string>("getName"); break; }
                    if (selected != null) break;
                }
            if (selected == null) throw new InvalidOperationException("No hardware H.264 decoder is available; software fallback is disabled.");
            NativeInputBindings.HV_Input_SelectHardwareCodec(selected);
#endif
            var url = NativeInputBindings.Utf8(settings.Location);
            try {
                var options = new NativeInputBindings.Options { Size = (uint)Marshal.SizeOf<NativeInputBindings.Options>(), Version = 1,
                    Url = url, MaxWidth = (uint)settings.RequestedWidth, MaxHeight = (uint)settings.RequestedHeight,
                    TimeoutMs = (uint)timeout, ReconnectDelayMs = (uint)delay, TransportTcp = 1 };
                int result = NativeInputBindings.HV_Input_Open(ref options, out handle);
                if (result != 0) throw new InvalidOperationException("RTSP GPU initialization failed (" + result + "); a prior input owner may still be retiring.");
            } finally { Marshal.FreeHGlobal(url); }
            renderEvent = NativeInputBindings.HV_Input_GetRenderEventFunc();
            nativeGeneration = bindingGeneration = sequence = 0; latestSlot = -1; closing = rebinding = false; mirror = settings.DisplayMirror;
            State = InputSourceState.Opening; LastError = "Waiting for RTSP connection, first keyframe and actual decoder geometry.";
            timeline.BeginGeneration();
        }
        internal void Tick()
        {
            retirement.Poll(); Progress();
            if (handle == IntPtr.Zero || closing) return;
            GL.IssuePluginEvent(renderEvent, 0x485649);
            NativeInputBindings.HV_Input_GetState(handle, out var state);
            if (state == 4 || state == 5 || state == 0) {
                NativeInputBindings.HV_Input_GetLastError(handle, error, (uint)error.Length);
                int size = Array.IndexOf(error, (byte)0);
                string message = size > 0 ? Encoding.UTF8.GetString(error, 0, size) : "RTSP decoder stopped before a usable GPU frame was published.";
                Close(); State = InputSourceState.Error; LastError = message; return;
            }
            if (state == 3) { State = InputSourceState.Reconnecting; LastError = "RTSP disconnected; waiting for reconnect and a new keyframe."; timeline.Close(); }
            if (!rebinding && NativeInputBindings.HV_Input_GetGpuGeometry(handle, out var w, out var h, out var generation) == 0 && generation != nativeGeneration) {
                if (w == 0 || h == 0 || w > 4096 || h > 4096) throw new InvalidOperationException("Actual decoder output geometry is unsupported.");
                width = w; height = h; bindingGeneration = generation; rebinding = true;
                NativeInputBindings.HV_Input_RetireGpuTargets(handle);
                timeline.BeginGeneration(); latestSlot = -1;
            }
            if (rebinding) {
                if (NativeInputBindings.HV_Input_GpuTargetsRetired(handle) == 0 || retirement.PendingResourceCount != 0) return;
                DestroyRetiredTextures();
                for (int i = 0; i < 3; ++i) {
                    int index = i;
                    textures[i] = new RenderTexture((int)width, (int)height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear)
                        { name = "HumanVision RTSP GPU slot " + i, enableRandomWrite = true, filterMode = FilterMode.Bilinear, wrapMode = TextureWrapMode.Clamp };
                    textures[i].Create();
                    // Unity initializes its owned image before AccessTexture/private queue writes.
                    var previous = RenderTexture.active; RenderTexture.active = textures[i]; GL.Clear(false, true, Color.clear); RenderTexture.active = previous;
                    pointers[i] = textures[i].GetNativeTexturePtr();
                    tokens[i] = retirement.Register(timeline.Generation, textures[i], () => { destroy[index] = true; });
                    sequences[i] = 0;
                }
                int bound = NativeInputBindings.HV_Input_BindGpuTargets(handle, pointers, width, height, bindingGeneration, 0, mirror ? 1 : 0);
                if (bound != 0) throw new InvalidOperationException("RTSP GPU target binding failed after actual target retirement.");
                nativeGeneration = bindingGeneration; rebinding = false;
            }
            if (NativeInputBindings.HV_Input_PollGpuFrame(handle, sequence, out var gpu) == 0) {
                var info = gpu.Frame;
                if (info.Generation != nativeGeneration || gpu.Slot >= 3 || info.DecodeMode != 2 || info.RowOrigin != 0) throw new InvalidOperationException("RTSP GPU frame metadata contract mismatch.");
                int slot = (int)gpu.Slot;
                State = InputSourceState.Streaming; LastError = string.Empty;
                var frame = new HumanVisionTextureFrame(timeline.SourceId, timeline.Generation, ++frameId, textures[slot], InputMonotonicClock.NowUs,
                    info.PtsValid == 1 ? info.PresentationTimestampUs : -1, 0, mirror, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Unknown,
                    FrameTimestampKind.LocalDecode, tokens[slot], info.DecodedTimestampUs, FrameClockDomain.SourceLocalMonotonic, info.ClockId);
                if (!timeline.TryPublish(in frame)) throw new InvalidOperationException("RTSP GPU frame publication rejected invalid metadata.");
                latestSlot = slot; sequences[slot] = info.Sequence; sequence = info.Sequence;
            }
            for (int i = 0; i < 3; ++i) if (i != latestSlot && sequences[i] != 0 && !retirement.HasPendingCopies(textures[i])) {
                NativeInputBindings.HV_Input_ReleaseGpuSlot(handle, (uint)i, sequences[i]); sequences[i] = 0;
            }
        }
        internal void Close()
        {
            if (handle == IntPtr.Zero) return;
            closing = true; rebinding = false; State = InputSourceState.Closing; LastError = "Retiring queued GPU source copies.";
            NativeInputBindings.HV_Input_Close(handle);
            timeline.Close(); InputRetirementPump.Watch(registration);
        }
        private void DestroyRetiredTextures()
        {
            for (int i = 0; i < 3; ++i) if (destroy[i]) {
                FrameTextureNormalizer.Destroy(textures[i]); textures[i] = null; pointers[i] = IntPtr.Zero; tokens[i] = 0; destroy[i] = false;
            }
        }
        private void Progress()
        {
            if (handle == IntPtr.Zero) return;
            if (closing || rebinding) {
                GL.IssuePluginEvent(renderEvent, 0x485649);
                if (NativeInputBindings.HV_Input_GpuTargetsRetired(handle) != 0) DestroyRetiredTextures();
            }
            if (closing && retirement.PendingResourceCount == 0 && NativeInputBindings.HV_Input_GpuRetired(handle) != 0 && NativeInputBindings.HV_Input_Release(handle) == 0) {
                handle = IntPtr.Zero; DestroyRetiredTextures();
                if (State != InputSourceState.Error) { State = InputSourceState.Stopped; LastError = string.Empty; }
            }
        }
    }
}
