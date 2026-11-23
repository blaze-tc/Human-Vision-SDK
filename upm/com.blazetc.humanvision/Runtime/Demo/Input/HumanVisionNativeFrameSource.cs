using System;
using System.Runtime.InteropServices;
using HumanVision.Input;
using HumanVision.Interop;
using UnityEngine;

namespace HumanVision.Demo
{
    // The upload consumer only needs completed, caller-owned preview copies.
    internal interface INativePreviewReader
    {
        bool TryGetProgress(out HumanVisionRuntimeProgress progress);
        bool TryGetState(out uint state, out ulong frames);
        string Diagnostics { get; }
        int CopyPreview(ulong after, IntPtr buffer, uint capacity, ref RuntimePreviewNative info);
    }
    internal interface INativeLiveAttachmentProvider
    {
        int Attach(ref RuntimeLiveConfigNative config, out RuntimeLiveBindingNative binding);
        INativeLiveConsumerApi CreateConsumer(RuntimeLiveBindingNative binding);
        bool TryGetSourceProgress(out HumanVisionSourceProgress progress);
    }
    // These are regular Unity-owned CPU-upload textures. No decoder image, AHB,
    // native analysis slot or native input lease survives until a Unity frame.
    [DefaultExecutionOrder(-150)]
    internal sealed class HumanVisionNativeFrameSource : MonoBehaviour, IHumanVisionFrameSource
    {
        private sealed class Slot { internal Texture2D Texture; internal HumanVisionTextureFrame Frame; internal bool Published; }
        private readonly Slot[] slots = { new Slot(), new Slot(), new Slot() };
        private HumanVisionManager manager;
        private INativePreviewReader reader;
        private bool previewEnabled, mirror, closed;
        private byte[] raw, upright;
        private GCHandle pinned;
        private int nextSlot, latestSlot = -1, protectedSlot = -1;
        private ulong after, boundSource, boundGeneration, boundEpoch;
        private long lastPublication;
        private NativeLiveFrameConsumer live;
        private string liveError = "";
        private bool realtime;
        private bool liveAttachPending;
        private RuntimeLiveConfigNative liveConfig;
        private INativeLiveAttachmentProvider liveProvider;
        internal string PreviewError => liveError.Length > 0 ? liveError : live?.LastError ?? "";
        internal void Configure(HumanVisionManager owner, bool preview, bool mirrored, int requestedWidth, int requestedHeight)
        {
            Configure(owner, preview, mirrored);
            if (Application.platform != RuntimePlatform.Android) return;
            ConfigureLive(new ManagerLiveAttachment(owner), preview, mirrored, 0, 0);
        }
        internal void ConfigureLive(INativeLiveAttachmentProvider provider, bool preview, bool mirrored, int requestedWidth, int requestedHeight)
        {
            liveProvider = provider; closed = false;
            realtime = true;
            liveConfig = new RuntimeLiveConfigNative { Size = (uint)Marshal.SizeOf<RuntimeLiveConfigNative>(), Version = 1,
                MaxWidth = (uint)requestedWidth, MaxHeight = (uint)requestedHeight, MirrorX = mirrored ? 1u : 0u, Enabled = preview ? 1u : 0u };
            liveAttachPending = true; TryAttachLive();
        }
        private void TryAttachLive()
        {
            if (!liveAttachPending || closed || liveProvider == null) return;
            int result = liveProvider.Attach(ref liveConfig, out var binding);
            if (result == RuntimeControlBindings.Busy || result == RuntimeControlBindings.NoData) return;
            liveAttachPending = false;
            if (result != RuntimeControlBindings.Ok) { liveError = "Original-resolution GPU preview unavailable (" + result + "); install an Input plugin supporting source-original output. Decoder and analysis remain active."; return; }
            var backend = liveProvider.CreateConsumer(binding);
            live = new NativeLiveFrameConsumer(backend, binding.SourceId, liveConfig.Enabled != 0);
        }
        private sealed class ManagerLiveAttachment : INativeLiveAttachmentProvider {
            private readonly HumanVisionManager owner;
            internal ManagerLiveAttachment(HumanVisionManager manager) { owner = manager; }
            public int Attach(ref RuntimeLiveConfigNative config, out RuntimeLiveBindingNative binding) => owner.AttachLivePreview(ref config, out binding);
            public INativeLiveConsumerApi CreateConsumer(RuntimeLiveBindingNative binding) => new RuntimeLivePreviewConsumer(owner, binding);
            public bool TryGetSourceProgress(out HumanVisionSourceProgress progress) => owner.TryGetSourceProgress(out progress);
        }
        internal void Configure(HumanVisionManager owner, bool preview, bool mirrored)
        { manager = owner; Configure(new ManagerPreviewReader(owner), preview, mirrored); }
        internal void Configure(INativePreviewReader owner, bool preview, bool mirrored)
        { reader = owner; previewEnabled = preview; mirror = mirrored; closed = false; }
        private sealed class ManagerPreviewReader : INativePreviewReader
        {
            private readonly HumanVisionManager owner;
            internal ManagerPreviewReader(HumanVisionManager owner) { this.owner = owner; }
            public bool TryGetProgress(out HumanVisionRuntimeProgress progress) { progress = default; return owner != null && owner.TryGetRuntimeProgress(out progress); }
            public bool TryGetState(out uint state, out ulong frames) { state = 0; frames = 0; return owner != null && owner.TryGetNativeInputState(out state, out frames); }
            public string Diagnostics => owner != null ? owner.RuntimeDiagnostics : "";
            public int CopyPreview(ulong after, IntPtr buffer, uint capacity, ref RuntimePreviewNative info) => owner.CopyNativePreview(after, buffer, capacity, ref info);
        }
        public InputSourceState State {
            get {
                if (realtime && !closed) {
                    if (live != null) return live.State;
                    return liveProvider != null && liveProvider.TryGetSourceProgress(out var progress) ? (InputSourceState)progress.State : InputSourceState.Opening;
                }
                if (realtime && live != null) return live.State;
                if (closed || reader == null) return InputSourceState.Stopped;
                if (!reader.TryGetState(out uint state, out ulong frames)) return InputSourceState.Opening;
                if (state == 9 || state == 10) return InputSourceState.Error;
                if (state == 6) return InputSourceState.Reconnecting;
                return state == 2 && frames > 0 ? InputSourceState.Streaming : InputSourceState.Opening;
            }
        }
        public string LastError => PreviewError.Length > 0 ? PreviewError : State == InputSourceState.Error ? reader.Diagnostics : "";
        public Texture CurrentTexture => realtime ? live?.CurrentTexture : !closed && latestSlot >= 0 ? slots[latestSlot].Texture : null;
        public void Open(HumanVisionSourceSettings settings)
        {
            if (manager == null || !manager.UsesIndependentInput || settings == null || settings.Kind != InputKind.Rtsp)
                throw new InvalidOperationException("The native Runtime must own a bound RTSP source before opening its preview consumer.");
        }
        private void Update() => Tick();
        internal void Tick()
        {
            if (realtime) { TryAttachLive(); live?.Tick(); return; }
            if (closed || reader == null || !previewEnabled) return;
            try { TickCpuPreview(); }
            catch (Exception error) {
                liveError = "Original-resolution preview unavailable: " + error.Message;
                previewEnabled = false; latestSlot = -1;
                foreach (var slot in slots) slot.Published = false;
                // Preview allocation/upload failure does not close decoder/inference.
            }
        }
        private void TickCpuPreview()
        {
            if (!reader.TryGetState(out uint state, out _) || state != 2) return;
            // Metadata-first (no pixels copied) notices a new source/generation even
            // when its sequence restarts. It never waits for a canonical result.
            var info = new RuntimePreviewNative { Size = (uint)Marshal.SizeOf<RuntimePreviewNative>(), Version = 1 };
            int status = reader.CopyPreview(0, IntPtr.Zero, 0, ref info);
            if (status == RuntimeControlBindings.NoData || status == RuntimeControlBindings.Busy) return;
            if (status == RuntimeControlBindings.Unsupported)
                throw new NotSupportedException("Native original-size preview is not supported by this Runtime/Input plugin.");
            if (status != RuntimeControlBindings.BufferTooSmall)
                throw new InvalidOperationException("Native original-resolution preview query failed (" + status + "). " + reader.Diagnostics);
            if (!ValidPreview(info))
                throw new InvalidOperationException("Native original-resolution preview metadata or geometry is unsupported; no smaller texture was substituted.");
            bool sameIdentity = info.SourceId == boundSource && info.Generation == boundGeneration && info.ConfigEpoch == boundEpoch;
            if (sameIdentity && info.FrameId <= after) return;
            if (raw == null || info.RgbaBytes > raw.Length) {
                if (pinned.IsAllocated) pinned.Free();
                raw = new byte[info.RgbaBytes]; upright = new byte[checked(info.Width * info.Height * 4)];
                pinned = GCHandle.Alloc(raw, GCHandleType.Pinned);
            }
            status = reader.CopyPreview(0, pinned.AddrOfPinnedObject(), (uint)raw.Length, ref info);
            if (status != RuntimeControlBindings.Ok || !ValidPreview(info) || info.RgbaBytes > raw.Length) return;
            sameIdentity = info.SourceId == boundSource && info.Generation == boundGeneration && info.ConfigEpoch == boundEpoch;
            if (sameIdentity && info.FrameId <= after) return;
            if (!sameIdentity) {
                after = 0; latestSlot = -1;
                foreach (var slot in slots) slot.Published = false;
                boundSource = info.SourceId; boundGeneration = info.Generation; boundEpoch = info.ConfigEpoch;
            }
            long nativeNow = RuntimeBindings.HV_RuntimeClockUs(), inputNow = InputMonotonicClock.NowUs;
            long age = nativeNow - info.TimestampUs;
            if (age < 0 || age > inputNow || info.FrameId > long.MaxValue) return;
            int writable = -1;
            for (int i = 0; i < slots.Length; ++i) {
                int candidate = (nextSlot + i) % slots.Length;
                if (candidate != protectedSlot) { writable = candidate; break; }
            }
            if (writable < 0) return; // presentation pressure drops preview only
            int width = checked((int)info.Width), height = checked((int)info.Height), stride = checked(width * 4);
            if (upright.Length != checked(stride * height)) upright = new byte[checked(stride * height)];
            for (int y = 0; y < height; ++y) Buffer.BlockCopy(raw, checked((int)info.StrideBytes * y), upright, (height - 1 - y) * stride, stride);
            var target = slots[writable];
            if (target.Texture == null || target.Texture.width != width || target.Texture.height != height) {
                if (target.Texture != null) Destroy(target.Texture);
                target.Texture = new Texture2D(width, height, TextureFormat.RGBA32, false, false) { name = "HumanVision native preview", wrapMode = TextureWrapMode.Clamp, filterMode = FilterMode.Bilinear };
            }
            target.Texture.LoadRawTextureData(upright); target.Texture.Apply(false, false);
            long publication = Math.Max(InputMonotonicClock.NowUs, lastPublication);
            target.Frame = new HumanVisionTextureFrame(info.SourceId, info.Generation, checked((long)info.FrameId), target.Texture,
                publication, -1, 0, mirror, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb, FrameTimestampKind.LocalDecode,
                info.FrameId, inputNow - age, FrameClockDomain.InputMonotonic, 0);
            target.Published = true; latestSlot = writable; nextSlot = (writable + 1) % slots.Length;
            after = info.FrameId; lastPublication = publication;
        }
        private static bool ValidPreview(RuntimePreviewNative info) =>
            info.SourceId != 0 && info.Generation != 0 && info.ConfigEpoch != 0 && info.FrameId != 0 && info.FrameId <= long.MaxValue &&
            info.Width > 0 && info.Height > 0 && info.Width <= 4096 && info.Height <= 4096 &&
            info.StrideBytes >= info.Width * 4 && info.RgbaBytes <= 64u * 1024u * 1024u &&
            info.RgbaBytes >= (ulong)info.StrideBytes * info.Height;

        public bool TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame)
        {
            frame = default;
            if (realtime) return live != null && live.TryGetLatestFrame(afterFrameId, out frame);
            if (closed || latestSlot < 0 || !slots[latestSlot].Published || slots[latestSlot].Frame.FrameId <= afterFrameId) return false;
            frame = slots[latestSlot].Frame; return true;
        }
        internal bool TryGetMatchedFrame(long frameId, ulong sourceId, ulong generation, out HumanVisionTextureFrame frame)
        {
            frame = default;
            if (realtime) return live != null && live.TryGetMatchedFrame(frameId, sourceId, generation, out frame);
            if (closed) return false;
            foreach (var slot in slots) if (slot.Published && slot.Frame.FrameId == frameId && slot.Frame.SourceId == sourceId && slot.Frame.Generation == generation) { frame = slot.Frame; return true; }
            return false;
        }
        // Called only when RuntimeView commits a new publication. Keep the old
        // color immutable through Update/LateUpdate gaps or failed compositions.
        internal bool ProtectMatchedFrame(in HumanVisionTextureFrame frame)
        {
            if (realtime) return live != null && live.ProtectMatchedFrame(in frame);
            if (closed) return false;
            for (int i = 0; i < slots.Length; ++i) {
                var slot = slots[i];
                if (slot.Published && slot.Texture == frame.Texture && slot.Frame.FrameId == frame.FrameId &&
                    slot.Frame.SourceId == frame.SourceId && slot.Frame.Generation == frame.Generation) {
                    protectedSlot = i; return true;
                }
            }
            return false;
        }
        public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease)
        { lease = default; return realtime && live != null && live.TryAcquireSourceCopyLease(in frame, out lease); }
        public void Close()
        {
            if (closed) return; closed = true; latestSlot = protectedSlot = -1;
            liveAttachPending = false;
            if (realtime) { live?.Close(); return; }
            if (pinned.IsAllocated) pinned.Free(); raw = upright = null;
            foreach (var slot in slots) { if (slot.Texture != null) Destroy(slot.Texture); slot.Texture = null; slot.Published = false; }
        }
        private void OnDestroy() => Close();
    }
}
