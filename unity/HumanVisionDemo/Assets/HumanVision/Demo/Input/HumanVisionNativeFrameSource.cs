using System;
using System.Runtime.InteropServices;
using HumanVision.Input;
using HumanVision.Interop;
using UnityEngine;

namespace HumanVision.Demo
{
    // These are regular Unity-owned CPU-upload textures. No decoder image, AHB,
    // native analysis slot or native input lease survives until a Unity frame.
    [DefaultExecutionOrder(-150)]
    internal sealed class HumanVisionNativeFrameSource : MonoBehaviour, IHumanVisionFrameSource
    {
        private sealed class Slot { internal Texture2D Texture; internal HumanVisionTextureFrame Frame; internal bool Published; }
        private readonly Slot[] slots = { new Slot(), new Slot(), new Slot() };
        private HumanVisionManager manager;
        private bool previewEnabled, mirror, closed;
        private byte[] raw, upright;
        private GCHandle pinned;
        private int nextSlot, latestSlot = -1;
        private ulong after, boundSource, boundGeneration, boundEpoch;
        private long lastPublication;
        internal void Configure(HumanVisionManager owner, bool preview, bool mirrored)
        { manager = owner; previewEnabled = preview; mirror = mirrored; closed = false; }
        public InputSourceState State {
            get {
                if (closed || manager == null) return InputSourceState.Stopped;
                if (!manager.TryGetNativeInputState(out uint state, out ulong frames)) return InputSourceState.Opening;
                if (state == 9 || state == 10) return InputSourceState.Error;
                if (state == 6) return InputSourceState.Reconnecting;
                return state == 2 && frames > 0 ? InputSourceState.Streaming : InputSourceState.Opening;
            }
        }
        public string LastError => State == InputSourceState.Error && manager != null ? manager.RuntimeDiagnostics : "";
        public Texture CurrentTexture => !closed && latestSlot >= 0 ? slots[latestSlot].Texture : null;
        public void Open(HumanVisionSourceSettings settings)
        {
            if (manager == null || !manager.UsesIndependentInput || settings == null || settings.Kind != InputKind.Rtsp)
                throw new InvalidOperationException("The native Runtime must own a bound RTSP source before opening its preview consumer.");
        }
        private void Update() => Tick();
        internal void Tick()
        {
            if (closed || manager == null || !previewEnabled || !manager.TryGetRuntimeProgress(out var progress) || !progress.Streaming) return;
            if (boundSource != progress.SourceId || boundGeneration != progress.Generation || boundEpoch != progress.ConfigEpoch) {
                after = 0; latestSlot = -1;
                foreach (var slot in slots) slot.Published = false;
                boundSource = progress.SourceId; boundGeneration = progress.Generation; boundEpoch = progress.ConfigEpoch;
            }
            var info = new RuntimePreviewNative { Size = (uint)Marshal.SizeOf<RuntimePreviewNative>(), Version = 1 };
            IntPtr address = pinned.IsAllocated ? pinned.AddrOfPinnedObject() : IntPtr.Zero;
            int status = manager.CopyNativePreview(after, address, raw == null ? 0u : (uint)raw.Length, ref info);
            if (status == RuntimeControlBindings.NoData || status == RuntimeControlBindings.Unsupported || status == RuntimeControlBindings.Busy) return;
            if (status == RuntimeControlBindings.BufferTooSmall) {
                if (info.Width == 0 || info.Height == 0 || info.Width > 4096 || info.Height > 4096 ||
                    info.StrideBytes < info.Width * 4 || info.RgbaBytes > 64u * 1024u * 1024u ||
                    info.RgbaBytes < (ulong)info.StrideBytes * info.Height) return;
                if (pinned.IsAllocated) pinned.Free();
                raw = new byte[info.RgbaBytes]; upright = new byte[checked(info.Width * info.Height * 4)];
                pinned = GCHandle.Alloc(raw, GCHandleType.Pinned);
                status = manager.CopyNativePreview(after, pinned.AddrOfPinnedObject(), (uint)raw.Length, ref info);
            }
            if (status != 0 || raw == null || info.RgbaBytes > raw.Length || info.SourceId != boundSource ||
                info.Generation != boundGeneration || info.ConfigEpoch != boundEpoch || info.FrameId <= after ||
                info.Width == 0 || info.Height == 0 || info.StrideBytes < info.Width * 4 ||
                info.RgbaBytes < (ulong)info.StrideBytes * info.Height) return;
            int width = checked((int)info.Width), height = checked((int)info.Height), stride = checked(width * 4);
            if (upright.Length != checked(stride * height)) upright = new byte[checked(stride * height)];
            for (int y = 0; y < height; ++y) Buffer.BlockCopy(raw, checked((int)info.StrideBytes * y), upright, (height - 1 - y) * stride, stride);
            var target = slots[nextSlot];
            if (target.Texture == null || target.Texture.width != width || target.Texture.height != height) {
                if (target.Texture != null) Destroy(target.Texture);
                target.Texture = new Texture2D(width, height, TextureFormat.RGBA32, false, false) { name = "HumanVision native preview", wrapMode = TextureWrapMode.Clamp, filterMode = FilterMode.Bilinear };
            }
            target.Texture.LoadRawTextureData(upright); target.Texture.Apply(false, false);
            long nativeNow = RuntimeBindings.HV_RuntimeClockUs(), inputNow = InputMonotonicClock.NowUs;
            long age = nativeNow - info.TimestampUs;
            if (age < 0 || age > inputNow) return;
            long publication = Math.Max(inputNow, lastPublication);
            target.Frame = new HumanVisionTextureFrame(info.SourceId, info.Generation, checked((long)info.FrameId), target.Texture,
                publication, -1, 0, mirror, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb, FrameTimestampKind.LocalDecode,
                info.FrameId, inputNow - age, FrameClockDomain.InputMonotonic, 0);
            target.Published = true; latestSlot = nextSlot; nextSlot = (nextSlot + 1) % slots.Length;
            after = info.FrameId; lastPublication = publication;
        }
        public bool TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame)
        {
            frame = default;
            if (closed || latestSlot < 0 || !slots[latestSlot].Published || slots[latestSlot].Frame.FrameId <= afterFrameId) return false;
            frame = slots[latestSlot].Frame; return true;
        }
        internal bool TryGetMatchedFrame(long frameId, ulong sourceId, ulong generation, out HumanVisionTextureFrame frame)
        {
            frame = default;
            if (closed) return false;
            foreach (var slot in slots) if (slot.Published && slot.Frame.FrameId == frameId && slot.Frame.SourceId == sourceId && slot.Frame.Generation == generation) { frame = slot.Frame; return true; }
            return false;
        }
        public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease)
        { lease = default; return false; } // this presentation consumer cannot become another ingestion owner
        public void Close()
        {
            if (closed) return; closed = true; latestSlot = -1;
            if (pinned.IsAllocated) pinned.Free(); raw = upright = null;
            foreach (var slot in slots) { if (slot.Texture != null) Destroy(slot.Texture); slot.Texture = null; slot.Published = false; }
        }
        private void OnDestroy() => Close();
    }
}
