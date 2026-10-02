using System;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

namespace HumanVision.Input
{
    /// <summary>Independent Windows RTSP decoder. Main-thread RGBA upload; latest complete native copy wins.</summary>
    public sealed class RtspFrameSource : UnityTextureFrameSource
    {
        private HumanVisionSourceSettings androidSettings;
        private int androidTimeout, androidDelay;
        private bool androidPaused, androidResume;
        private AndroidRtspGpuSource android;
        public override Texture CurrentTexture => android != null ? android.CurrentTexture : base.CurrentTexture;
        public override bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) { if (android != null) return android.TryGetLatestFrame(after, out frame); return base.TryGetLatestFrame(after, out frame); }
        public override bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) { if (android != null) return android.TryAcquireSourceCopyLease(in frame, out lease); return base.TryAcquireSourceCopyLease(in frame, out lease); }
        internal IntPtr AndroidHandle => android == null ? IntPtr.Zero : android.Handle;
        private IntPtr handle, rgba;
        private uint capacity;
        private Texture2D upload;
        private ulong nativeSequence, nativeGeneration;
        private readonly IntPtr[] retiring = new IntPtr[8];
        private readonly byte[] errorBuffer = new byte[1024];
        // Cold, internal test seam for the actual unmanaged ownership transition; reused on every growth/close.
        internal Func<int, IntPtr> AllocateRgba = Marshal.AllocHGlobal;
        internal Action<IntPtr> FreeRgba = Marshal.FreeHGlobal;
        private uint previousState = uint.MaxValue;
        protected override bool HasPendingWorkerRetirement
        {
            get { if (android != null && android.Pending) return true; for (int i = 0; i < retiring.Length; ++i) if (retiring[i] != IntPtr.Zero) return true; return false; }
        }
        public override void Open(HumanVisionSourceSettings settings)
        {
            CheckThread();
            Close(); LastError = string.Empty;
            if (settings == null) { State = InputSourceState.Error; LastError = "RTSP settings are required."; return; }
            var rtsp = settings as RtspSourceSettings;
            int timeout = rtsp == null ? 5000 : rtsp.OpenTimeoutMs;
            int delay = rtsp == null ? 500 : rtsp.ReconnectDelayMs;
            if (!Uri.TryCreate(settings.Location, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp" ||
                settings.RequestedWidth < 1 || settings.RequestedWidth > 4096 || settings.RequestedHeight < 1 || settings.RequestedHeight > 4096 ||
                timeout < 100 || timeout > 60000 || delay < 0 || delay > 60000)
            { State = InputSourceState.Error; LastError = "Use a valid rtsp:// address, dimensions 1–4096, timeout 100–60000ms and reconnect delay 0–60000ms."; return; }
#if UNITY_ANDROID && !UNITY_EDITOR
            try {
                if (rtsp != null && rtsp.Transport != RtspTransport.Tcp) throw new InvalidOperationException("Android RTSP currently supports H.264 over TCP only.");
                if (android == null) android = new AndroidRtspGpuSource();
                androidSettings = settings; androidTimeout = timeout; androidDelay = delay; androidPaused = androidResume = false; android.Open(settings, timeout, delay); State = android.State; LastError = android.LastError;
            } catch (Exception ex) { State = InputSourceState.Error; LastError = ex is InvalidOperationException ? ex.Message : "Android RTSP initialization failed; install the qualified ARM64 input plugin and FFmpeg dependencies."; }
            return;
#elif !UNITY_STANDALONE_WIN && !UNITY_EDITOR_WIN
            State = InputSourceState.Error; LastError = "RTSP input currently requires the Windows x64 input plugin; Android hardware input is not enabled.";
            return;
#else
            try {
                PollRetirement();
                // Reserve one bounded retirement slot before starting a worker.
                bool available = false;
                for (int i = 0; i < retiring.Length; ++i) if (retiring[i] == IntPtr.Zero) { available = true; break; }
                if (!available) throw new InvalidOperationException("RTSP worker retirement capacity exhausted; wait for pending closes before reopening.");
                BeginOutput(); DisplayMirror = settings.DisplayMirror; State = InputSourceState.Opening;
                var pointer = NativeInputBindings.Utf8(settings.Location);
                try {
                    var options = new NativeInputBindings.Options { Size = (uint)Marshal.SizeOf<NativeInputBindings.Options>(), Version = 1,
                        Url = pointer, MaxWidth = (uint)settings.RequestedWidth, MaxHeight = (uint)settings.RequestedHeight,
                        TimeoutMs = (uint)timeout, ReconnectDelayMs = (uint)delay, TransportTcp = rtsp == null || rtsp.Transport == RtspTransport.Tcp ? 1u : 0u };
                    if (NativeInputBindings.HV_Input_Open(ref options, out handle) != 0)
                        throw new InvalidOperationException("RTSP input initialization failed; check native plugin ABI and settings.");
                } finally { Marshal.FreeHGlobal(pointer); }
                nativeSequence = nativeGeneration = 0; previousState = uint.MaxValue;
            } catch (DllNotFoundException) { Fail("Install humanvision_input.dll and its qualified FFmpeg dependency DLLs for Windows x64."); }
            catch (EntryPointNotFoundException) { Fail("Install the version-1 humanvision_input plugin with all HV_Input exports."); }
            catch (BadImageFormatException) { Fail("Install the Windows x64 humanvision_input plugin and matching x64 dependencies."); }
            catch (Exception ex) { Fail(ex is InvalidOperationException ? ex.Message : "RTSP input initialization failed."); }
#endif
        }
        private void Fail(string message) { Close(); State = InputSourceState.Error; LastError = message; }
        private void Update()
        {
            PollRetirement();
            if (android != null) {
                try {
                    android.Tick();
                    if (androidResume && !android.Pending) { androidResume = false; android.Open(androidSettings, androidTimeout, androidDelay); }
                    State = android.State; LastError = android.LastError;
                }
                catch (Exception ex) { android.Fail(ex is InvalidOperationException ? ex.Message : "RTSP GPU frame publication failed."); State = android.State; LastError = android.LastError; }
                return;
            }
            if (handle == IntPtr.Zero) return;
            try {
                if (NativeInputBindings.HV_Input_GetState(handle, out var state) != 0) { Fail("RTSP state query failed."); return; }
                if (state != previousState) {
                    previousState = state;
                    State = state == 1 ? InputSourceState.Opening : state == 2 ? InputSourceState.Streaming :
                        state == 3 ? InputSourceState.Reconnecting : state == 4 ? InputSourceState.Error : InputSourceState.Closing;
                    if (state == 3 || state == 4) {
                        RetireOutput();
                        NativeInputBindings.HV_Input_GetLastError(handle, errorBuffer, (uint)errorBuffer.Length);
                        int length = Array.IndexOf(errorBuffer, (byte)0);
                        LastError = length <= 0 ? "RTSP connection unavailable; reconnecting." : Encoding.UTF8.GetString(errorBuffer, 0, length);
                    }
                }
                var hint = NativeInputBindings.FrameInfo.Create();
                if (NativeInputBindings.HV_Input_PollFrame(handle, nativeSequence, ref hint) != 0) return;
                if (!Valid(hint)) { Fail("RTSP plugin returned unsupported frame geometry or metadata."); return; }
                EnsureRgbaCapacity(hint.RgbaBytes);
                var actual = NativeInputBindings.FrameInfo.Create();
                // Poll is only a capacity hint: copy requires the same sequence and atomically returns its metadata.
                int copied = NativeInputBindings.HV_Input_CopyRgba(handle, hint.Sequence, rgba, capacity, ref actual);
                if (copied == 1) return;
                if (copied != 0 || !Valid(actual) || actual.Sequence != hint.Sequence || actual.RgbaBytes > capacity)
                { Fail("RTSP exact frame copy failed; plugin pixels and metadata must match."); return; }
                if (actual.Generation != nativeGeneration) { BeginOutput(); nativeGeneration = actual.Generation; }
                bool linear = actual.ColorSpace != 1;
                if (upload == null || upload.width != actual.Width || upload.height != actual.Height || upload.isDataSRGB == linear) {
                    FrameTextureNormalizer.Destroy(upload);
                    upload = new Texture2D((int)actual.Width, (int)actual.Height, TextureFormat.RGBA32, false, linear)
                        { name = "HumanVision RTSP upload", filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp };
                }
                upload.LoadRawTextureData(rgba, (int)actual.RgbaBytes); upload.Apply(false, false);
                State = InputSourceState.Streaming; LastError = string.Empty;
                // Native first row is top-left. LoadRawTextureData interprets it bottom-left; one GPU vertical flip corrects it.
                Publish(upload, 0, true, actual.PtsValid == 1 ? actual.PresentationTimestampUs : -1, ActiveGeneration,
                    FrameTimestampKind.LocalDecode, actual.DecodedTimestampUs, FrameClockDomain.SourceLocalMonotonic, actual.ClockId,
                    actual.ColorSpace == 1 ? FrameTextureNormalizer.EncodingFor(upload) : FrameColorSpace.Unknown);
                nativeSequence = actual.Sequence;
            } catch (Exception) { Fail("RTSP frame upload failed; verify native plugin and available texture memory."); }
        }
        private void OnApplicationPause(bool paused)
        {
            if (android == null || androidSettings == null) return;
            if (paused) {
                androidPaused = android.Handle != IntPtr.Zero && android.State != InputSourceState.Closing && android.State != InputSourceState.Error;
                if (androidPaused) { android.Close(); State = InputSourceState.Closing; Debug.Log("HVInputGate production_pause_closed=true"); }
            } else if (androidPaused) { androidPaused = false; androidResume = true; Debug.Log("HVInputGate production_resume_requested=true"); }
        }
        internal void EnsureRgbaCapacity(uint required)
        {
            if (capacity >= required) return;
            // Commit ownership only after allocation succeeds: failure leaves the old pointer valid for Close.
            var replacement = AllocateRgba(checked((int)required));
            if (rgba != IntPtr.Zero) FreeRgba(rgba);
            rgba = replacement; capacity = required;
        }
        private static bool Valid(NativeInputBindings.FrameInfo info)
        {
            return info.Size == 96 && info.Version == 1 && info.Sequence != 0 && info.Generation != 0 &&
                info.Width > 0 && info.Width <= 4096 && info.Height > 0 && info.Height <= 4096 &&
                info.StrideBytes == info.Width * 4 && info.RgbaBytes == info.StrideBytes * info.Height &&
                info.ClockDomain == 1 && info.ClockId != 0 && info.TimestampKind == 1 && info.DecodedTimestampUs >= 0 &&
                info.RowOrigin == 1 && info.DecodeMode == 1 && info.ColorSpace <= 1 && info.PtsValid <= 1;
        }
        public override void Close()
        {
            CheckThread(); State = InputSourceState.Closing;
            if (android != null) { androidResume = androidPaused = false; android.Close(); State = android.State; LastError = android.LastError; WatchWorkerRetirement(); return; }
            if (handle != IntPtr.Zero) {
                NativeInputBindings.HV_Input_Close(handle);
                for (int i = 0; i < retiring.Length; ++i) if (retiring[i] == IntPtr.Zero) { retiring[i] = handle; handle = IntPtr.Zero; break; }
                WatchWorkerRetirement();
            }
            RetireOutput(); FrameTextureNormalizer.Destroy(upload); upload = null;
            if (rgba != IntPtr.Zero) FreeRgba(rgba);
            rgba = IntPtr.Zero; capacity = 0;
            PollRetirement();
        }
        protected override void OnRetirementProgress()
        {
            for (int i = 0; i < retiring.Length; ++i)
                if (retiring[i] != IntPtr.Zero && NativeInputBindings.HV_Input_Release(retiring[i]) == 0) retiring[i] = IntPtr.Zero;
            base.OnRetirementProgress();
        }
    }
}
