using System;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Experimental.Rendering;
namespace HumanVision.Input
{
    internal struct NativeLiveGeometry
    {
        internal ulong Generation;
        internal uint SourceWidth, SourceHeight, Width, Height, RotationDegrees, MirrorX;
    }
    internal struct NativeLiveTextureFrame
    {
        internal ulong Sequence, Generation, TargetGeneration, ClockId;
        internal uint Slot, Width, Height, SourceWidth, SourceHeight, RotationDegrees, MirrorX, RowOrigin, PtsValid, ColorSpace;
        internal long DecodedTimestampUs, PresentationTimestampUs;
    }
    internal interface INativeLiveConsumerApi
    {
        InputSourceState State { get; }
        string LastError { get; }
        bool TryGetGeometry(out NativeLiveGeometry geometry);
        int BindTargets(IntPtr[] textures, uint width, uint height, ulong targetGeneration);
        bool TryPoll(ulong after, out NativeLiveTextureFrame frame);
        int ReleaseSlot(uint slot, ulong targetGeneration, ulong sequence);
        int RetireTargets();
        bool TargetsRetired { get; }
        int Close();
        bool TryRelease();
        void PumpRenderEvent();
    }
    internal interface ILiveSamplingFence { void Arm(); bool IsComplete { get; } }
    // This consumer owns Unity targets only. The backend borrows the existing
    // decoder and cannot Open or claim an analysis publication through this API.
    internal sealed class NativeLiveFrameConsumer
    {
        private sealed class SamplingFence : ILiveSamplingFence {
            private GraphicsFence fence;
            public void Arm() => fence = Graphics.CreateGraphicsFence(GraphicsFenceType.AsyncQueueSynchronisation, SynchronisationStageFlags.AllGPUOperations);
            public bool IsComplete => fence.passed;
        }
        private sealed class Slot {
            internal RenderTexture Texture;
            internal HumanVisionTextureFrame Frame;
            internal ulong Sequence, Token, TargetGeneration;
            internal bool Destroy, FenceArmed;
            internal ILiveSamplingFence Fence;
        }
        private readonly INativeLiveConsumerApi api;
        private readonly ulong sourceId;
        private readonly bool enabled;
        private readonly SourceRetirement retirement = new SourceRetirement();
        private readonly Slot[] slots = new Slot[3];
        private readonly IntPtr[] pointers = new IntPtr[3];
        private readonly InputRetirementRegistration registration;
        private NativeLiveGeometry geometry;
        private ulong targetGeneration, after;
        private int latest = -1, protectedSlot = -1;
        private bool retiringTargets, closing, stopping, released, failed;
        private string error = "";
        internal NativeLiveFrameConsumer(INativeLiveConsumerApi backend, ulong sdkSourceId, bool previewEnabled, Func<ILiveSamplingFence> fenceFactory = null) {
            api = backend ?? throw new ArgumentNullException(nameof(backend));
            if (sdkSourceId == 0) throw new ArgumentOutOfRangeException(nameof(sdkSourceId));
            sourceId = sdkSourceId; enabled = previewEnabled;
            for (int i = 0; i < 3; i++) slots[i] = new Slot { Fence = fenceFactory != null ? fenceFactory() : new SamplingFence() };
            registration = new InputRetirementRegistration(retirement, Progress, () => Pending);
            if (previewEnabled && fenceFactory == null && !SystemInfo.supportsGraphicsFence) { error = "Realtime preview requires Unity GPU sampling fences."; failed = true; BeginClose(); }
        }
        internal bool Pending => !released || retirement.PendingResourceCount != 0;
        internal InputSourceState State => stopping ? (Pending ? InputSourceState.Closing : InputSourceState.Stopped) : api.State;
        internal string LastError => error.Length > 0 ? error : api.LastError;
        internal Texture CurrentTexture => !stopping && !failed && api.State == InputSourceState.Streaming && latest >= 0 ? slots[latest].Texture : null;
        internal uint SourceWidth => geometry.SourceWidth;
        internal uint SourceHeight => geometry.SourceHeight;
        internal void Tick() {
            retirement.Poll();
            try {
                if (closing) { Progress(); return; }
                if (!enabled || failed) return;
                if (!api.TryGetGeometry(out var next)) { api.PumpRenderEvent(); return; }
                if (geometry.Generation != 0 && (next.Generation != geometry.Generation || next.Width != geometry.Width || next.Height != geometry.Height)) BeginTargetRetirement();
                ReleaseUnusedSlots();
                if (retiringTargets) {
                    api.PumpRenderEvent();
                    if (!api.TargetsRetired || retirement.PendingResourceCount != 0) return;
                    DestroyRetiredTextures(); retiringTargets = false; geometry = default; after = 0;
                    if (!api.TryGetGeometry(out next)) return;
                }
                if (geometry.Generation == 0) Allocate(next);
                if (api.TryPoll(after, out var frame)) Publish(frame);
                ReleaseUnusedSlots();
                api.PumpRenderEvent();
            } catch (Exception exception) {
                // A broken optional preview never closes a healthy analysis source.
                error = exception.Message; failed = true; BeginClose();
            }
        }
        private void Allocate(NativeLiveGeometry next) {
            if (next.Generation == 0 || next.Width == 0 || next.Height == 0 || next.Width > 4096 || next.Height > 4096)
                throw new InvalidOperationException("Realtime preview geometry is invalid.");
            geometry = next; ++targetGeneration;
            for (int i = 0; i < 3; i++) {
                int index = i;
                var descriptor = new RenderTextureDescriptor((int)next.Width, (int)next.Height) {
                    graphicsFormat = QualitySettings.activeColorSpace == ColorSpace.Linear ? GraphicsFormat.R8G8B8A8_SRGB : GraphicsFormat.R8G8B8A8_UNorm,
                    depthBufferBits = 0, msaaSamples = 1,
                    useMipMap = false, autoGenerateMips = false
                };
                var texture = new RenderTexture(descriptor) { name = "HumanVision independent live slot " + i,
                    filterMode = FilterMode.Bilinear, wrapMode = TextureWrapMode.Clamp };
                slots[i].Texture = texture;
                slots[i].Token = retirement.Register(next.Generation, texture, () => slots[index].Destroy = true);
                if (!texture.Create()) throw new InvalidOperationException("Realtime RGBA8 target allocation failed.");
                var previous = RenderTexture.active;
                try { RenderTexture.active = texture; GL.Clear(false, true, Color.clear); }
                finally { RenderTexture.active = previous; }
                pointers[i] = texture.GetNativeTexturePtr();
                if (pointers[i] == IntPtr.Zero) throw new InvalidOperationException("Realtime preview target has no native image.");
            }
            Check(api.BindTargets(pointers, next.Width, next.Height, targetGeneration), "bind realtime targets");
        }
        private void Publish(NativeLiveTextureFrame frame) {
            if (frame.Slot >= 3 || frame.TargetGeneration != targetGeneration)
                throw new InvalidOperationException("Realtime preview target lifetime is invalid.");
            var slot = slots[frame.Slot];
            if (slot.Sequence != 0) throw new InvalidOperationException("Realtime preview overwrote a held Unity texture.");
            // Poll already granted a completed target lease. Keep its ownership
            // even if the remaining metadata is rejected, so Close can retire it.
            slot.Sequence = frame.Sequence; slot.TargetGeneration = frame.TargetGeneration; slot.FenceArmed = false;
            if (frame.Generation != geometry.Generation ||
                frame.Sequence <= after || frame.Sequence > long.MaxValue || frame.Width != geometry.Width || frame.Height != geometry.Height ||
                frame.ClockId == 0 || frame.DecodedTimestampUs < 0)
                throw new InvalidOperationException("Realtime preview publication identity is invalid.");
            slot.Frame = new HumanVisionTextureFrame(sourceId, frame.Generation, (long)frame.Sequence, slot.Texture,
                InputMonotonicClock.NowUs, frame.PtsValid != 0 ? frame.PresentationTimestampUs : -1,
                (int)frame.RotationDegrees, frame.MirrorX != 0,
                frame.RowOrigin == 1 ? FrameRowOrigin.NativeTopLeft : FrameRowOrigin.UnityBottomLeft,
                frame.ColorSpace == 1 ? FrameColorSpace.Srgb : FrameColorSpace.Unknown, FrameTimestampKind.LocalDecode, slot.Token,
                frame.DecodedTimestampUs, FrameClockDomain.SourceLocalMonotonic, frame.ClockId);
            latest = (int)frame.Slot; after = frame.Sequence;
        }
        private void ReleaseUnusedSlots() {
            for (int i = 0; i < 3; i++) {
                var slot = slots[i];
                if (slot.Sequence == 0 || !retiringTargets && !closing && (i == latest || i == protectedSlot)) continue;
                if (!slot.FenceArmed) { slot.Fence.Arm(); slot.FenceArmed = true; }
                if (!slot.Fence.IsComplete || retirement.HasPendingCopies(slot.Texture)) continue;
                Check(api.ReleaseSlot((uint)i, slot.TargetGeneration, slot.Sequence), "release realtime target");
                slot.Sequence = 0; slot.FenceArmed = false;
            }
        }
        internal bool TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame) {
            frame = default;
            if (stopping || failed || retiringTargets || api.State != InputSourceState.Streaming || latest < 0 || slots[latest].Frame.FrameId <= afterFrameId) return false;
            frame = slots[latest].Frame; return true;
        }
        internal bool TryGetMatchedFrame(long frameId, ulong id, ulong generation, out HumanVisionTextureFrame frame) {
            frame = default;
            if (stopping || failed || retiringTargets || api.State != InputSourceState.Streaming || id != sourceId) return false;
            for (int i = 0; i < 3; i++) if (slots[i].Sequence != 0 && slots[i].Frame.FrameId == frameId && slots[i].Frame.Generation == generation) { frame = slots[i].Frame; return true; }
            return false;
        }
        internal bool ProtectMatchedFrame(in HumanVisionTextureFrame frame) {
            if (stopping || failed || retiringTargets) return false;
            for (int i = 0; i < 3; i++) if (slots[i].Sequence != 0 && frame.SourceId == sourceId &&
                slots[i].Frame.FrameId == frame.FrameId && slots[i].Frame.Generation == frame.Generation &&
                slots[i].Frame.ResourceToken == frame.ResourceToken && slots[i].Frame.Texture == frame.Texture) {
                if (protectedSlot != i && protectedSlot >= 0) slots[protectedSlot].FenceArmed = false;
                protectedSlot = i; return true;
            }
            return false;
        }
        internal bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) {
            lease = default;
            if (stopping || failed || retiringTargets || frame.SourceId != sourceId) return false;
            for (int i = 0; i < 3; i++) if (slots[i].Sequence != 0 && slots[i].Frame.ResourceToken == frame.ResourceToken && slots[i].Frame.FrameId == frame.FrameId)
                return retirement.TryAcquire(in frame, out lease);
            return false;
        }
        private void BeginTargetRetirement() {
            if (retiringTargets) return;
            retiringTargets = true; latest = protectedSlot = -1;
            if (geometry.Generation != 0) retirement.RetireGeneration(geometry.Generation);
            Check(api.RetireTargets(), "retire realtime targets");
        }
        internal void Close() { stopping = true; BeginClose(); }
        private void BeginClose() {
            if (closing || released) return;
            closing = true; BeginTargetRetirement(); Check(api.Close(), "close realtime consumer");
            InputRetirementPump.Watch(registration);
        }
        private void Progress() {
            if (!closing || released) return;
            ReleaseUnusedSlots();
            if (api.TargetsRetired && retirement.PendingResourceCount == 0) {
                DestroyRetiredTextures();
                if (api.TryRelease()) released = true;
                // A final callback may already be queued. Do not create a new
                // callback reference before each release attempt and starve it.
                return;
            }
            api.PumpRenderEvent();
        }
        private void DestroyRetiredTextures() {
            for (int i = 0; i < 3; i++) if (slots[i].Destroy) {
                FrameTextureNormalizer.Destroy(slots[i].Texture); slots[i].Texture = null;
                slots[i].Token = 0; slots[i].Sequence = 0; slots[i].Destroy = false; pointers[i] = IntPtr.Zero;
            }
        }
        private static void Check(int status, string operation) {
            if (status != 0) throw new InvalidOperationException(operation + " failed (" + status + "); native resources remain owned until completion.");
        }
    }
}
