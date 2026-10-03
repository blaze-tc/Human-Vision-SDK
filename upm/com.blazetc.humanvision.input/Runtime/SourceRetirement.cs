using System;
using System.Threading;
using UnityEngine;

namespace HumanVision.Input
{
    /// <summary>Completion of a GPU source snapshot copy only; never model inference completion.</summary>
    public interface ISourceCopyFence { bool IsComplete { get; } }

    /// <summary>
    /// Exactly one submission per lease, even if this value is copied. After queueing a copy,
    /// RetireAfter must receive its GPU completion fence. Dispose cancels only an unqueued lease.
    /// All calls take place on the owning Unity main thread.
    /// </summary>
    public struct SourceCopyLease : IDisposable
    {
        private readonly SourceRetirement owner;
        private readonly int slot;
        private readonly ulong identity;

        internal SourceCopyLease(SourceRetirement owner, int slot, ulong identity)
        {
            this.owner = owner;
            this.slot = slot;
            this.identity = identity;
        }

        /// <summary>True only after this exact lease identity was consumed or cancelled.</summary>
        public bool IsRetired => owner == null || owner.IsCopyRetired(slot, identity);

        public void RetireAfter(ISourceCopyFence fence)
        {
            if (owner == null) throw new InvalidOperationException("No source copy lease was acquired.");
            owner.SubmitFence(slot, identity, fence);
        }

        public void Dispose()
        {
            if (owner == null) return;
            owner.CancelUnqueued(slot, identity);
        }
    }

    /// <summary>
    /// Per-source texture ownership and copy retirement. Construct on the Unity main thread.
    /// Register once per resource/contract change; acquire and poll reuse fixed slots with no
    /// managed allocation. Poll must continue while Closing until PendingResourceCount is zero.
    /// A missing copy fence keeps its resource alive; it never permits premature destruction.
    /// </summary>
    public sealed class SourceRetirement
    {
        private struct Resource
        {
            public ulong Token, Generation;
            public Texture Texture;
            public Action Destroy;
            public bool Retiring;
            public int Copies;
        }

        private struct Copy
        {
            public ulong Identity;
            public int ResourceIndex;
            public ISourceCopyFence Fence;
        }

        private readonly Resource[] resources;
        private readonly Copy[] copies;
        private readonly int threadId = Thread.CurrentThread.ManagedThreadId;
        private ulong nextResourceToken, nextCopyIdentity;
        private int pendingResourceCount;

        public SourceRetirement(int resourceCapacity = 16, int copyCapacity = 64)
        {
            if (resourceCapacity < 1) throw new ArgumentOutOfRangeException(nameof(resourceCapacity));
            if (copyCapacity < 1) throw new ArgumentOutOfRangeException(nameof(copyCapacity));
            resources = new Resource[resourceCapacity];
            copies = new Copy[copyCapacity];
        }

        public int PendingResourceCount { get { CheckThread(); return pendingResourceCount; } }

        public ulong Register(ulong generation, Texture texture, Action destroy)
        {
            CheckThread();
            if (generation == 0) throw new ArgumentOutOfRangeException(nameof(generation));
            if (texture == null) throw new ArgumentNullException(nameof(texture));
            if (destroy == null) throw new ArgumentNullException(nameof(destroy));
            for (var i = 0; i < resources.Length; ++i)
                if (resources[i].Token != 0 && resources[i].Texture == texture)
                    throw new InvalidOperationException("This texture already has a source owner. Use another texture until the old generation retires.");
            for (var i = 0; i < resources.Length; ++i)
            {
                if (resources[i].Token != 0) continue;
                var token = checked(++nextResourceToken);
                resources[i] = new Resource { Token = token, Generation = generation, Texture = texture, Destroy = destroy };
                ++pendingResourceCount;
                return token;
            }
            throw new InvalidOperationException("Source resource capacity exhausted. Poll retiring copies before allocating another texture.");
        }

        public void RetireGeneration(ulong generation)
        {
            CheckThread();
            for (var i = 0; i < resources.Length; ++i)
                if (resources[i].Token != 0 && resources[i].Generation == generation)
                    resources[i].Retiring = true;
        }

        internal bool HasPendingCopies(Texture texture)
        {
            CheckThread();
            for (int i = 0; i < resources.Length; ++i)
                if (resources[i].Token != 0 && resources[i].Texture == texture) return resources[i].Copies != 0;
            return false;
        }

        internal bool IsLive(in HumanVisionTextureFrame frame)
        {
            CheckThread();
            return FindResource(in frame) >= 0;
        }

        internal bool TryAcquire(in HumanVisionTextureFrame frame, out SourceCopyLease lease)
        {
            CheckThread();
            lease = default;
            var resourceIndex = FindResource(in frame);
            if (resourceIndex < 0) return false;
            for (var i = 0; i < copies.Length; ++i)
            {
                if (copies[i].Identity != 0) continue;
                var identity = checked(++nextCopyIdentity);
                copies[i] = new Copy { Identity = identity, ResourceIndex = resourceIndex };
                ++resources[resourceIndex].Copies;
                lease = new SourceCopyLease(this, i, identity);
                return true;
            }
            return false; // Bounded backpressure: consumer can skip this copy without blocking preview.
        }

        private int FindResource(in HumanVisionTextureFrame frame)
        {
            if (frame.ResourceToken == 0 || frame.Texture == null) return -1;
            for (var i = 0; i < resources.Length; ++i)
            {
                ref var resource = ref resources[i];
                if (resource.Token == frame.ResourceToken && !resource.Retiring &&
                    resource.Generation == frame.Generation && resource.Texture == frame.Texture)
                    return i;
            }
            return -1;
        }

        internal void SubmitFence(int slot, ulong identity, ISourceCopyFence fence)
        {
            CheckThread();
            if (fence == null) throw new ArgumentNullException(nameof(fence));
            ValidateCopy(slot, identity);
            if (copies[slot].Fence != null) throw new InvalidOperationException("This source copy already has a retirement fence.");
            copies[slot].Fence = fence;
        }

        internal void CancelUnqueued(int slot, ulong identity)
        {
            CheckThread();
            ValidateCopy(slot, identity);
            if (copies[slot].Fence != null) throw new InvalidOperationException("A queued GPU copy cannot be cancelled. Poll its fence.");
            CompleteCopy(slot);
        }

        internal bool IsCopyRetired(int slot, ulong identity)
        {
            CheckThread();
            return slot < 0 || slot >= copies.Length || identity == 0 || copies[slot].Identity != identity;
        }

        private void ValidateCopy(int slot, ulong identity)
        {
            if (slot < 0 || slot >= copies.Length || identity == 0 || copies[slot].Identity != identity)
                throw new InvalidOperationException("The source copy lease has already retired.");
        }

        private void CompleteCopy(int slot)
        {
            --resources[copies[slot].ResourceIndex].Copies;
            copies[slot] = default;
        }

        public void Poll()
        {
            CheckThread();
            for (var i = 0; i < copies.Length; ++i)
                if (copies[i].Identity != 0 && copies[i].Fence != null && copies[i].Fence.IsComplete)
                    CompleteCopy(i);
            for (var i = 0; i < resources.Length; ++i)
            {
                if (resources[i].Token == 0 || !resources[i].Retiring || resources[i].Copies != 0) continue;
                var destroy = resources[i].Destroy;
                resources[i] = default;
                --pendingResourceCount;
                destroy();
            }
        }

        internal void CheckThread()
        {
            if (Thread.CurrentThread.ManagedThreadId != threadId)
                throw new InvalidOperationException("Source textures and copy leases must be managed on the owning Unity main thread.");
        }
    }

    /// <summary>
    /// Shared publication gate for input implementations, owned by one source for its lifetime.
    /// SourceId survives reconnects; generation changes invalidate late callbacks immediately.
    /// FrameId must increase and input-clock publication time must not decrease across generations,
    /// including Close/reopen. Equal microseconds are allowed. Invalid metadata preserves the latest
    /// valid frame. PTS does not participate in ordering. No inference dependency or inference fence.
    /// </summary>
    public sealed class SourceGeneration
    {
        private static long nextSourceId;
        private readonly SourceRetirement retirement;
        private readonly ulong sourceId;
        private ulong generation;
        private bool active, hasLatest;
        private long lastFrameId = -1;
        private long lastPublishedTimestampUs = -1;
        private HumanVisionTextureFrame latest;

        public SourceGeneration(SourceRetirement retirement)
        {
            this.retirement = retirement ?? throw new ArgumentNullException(nameof(retirement));
            retirement.CheckThread();
            sourceId = checked((ulong)Interlocked.Increment(ref nextSourceId));
        }

        public ulong SourceId { get { retirement.CheckThread(); return sourceId; } }
        public ulong Generation { get { retirement.CheckThread(); return generation; } }
        public Texture CurrentTexture { get { return TryGetLatestFrame(-1, out var frame) ? frame.Texture : null; } }

        public ulong BeginGeneration()
        {
            retirement.CheckThread();
            retirement.RetireGeneration(generation);
            generation = checked(generation + 1);
            active = true;
            hasLatest = false;
            latest = default;
            return generation;
        }

        public void Close()
        {
            retirement.CheckThread();
            active = false;
            hasLatest = false;
            latest = default;
            retirement.RetireGeneration(generation);
            retirement.Poll();
        }

        public bool TryPublish(in HumanVisionTextureFrame frame)
        {
            retirement.CheckThread();
            if (!active || frame.SourceId != sourceId || frame.Generation != generation ||
                frame.FrameId <= lastFrameId || !frame.HasValidTimestamps() ||
                frame.PublishedTimestampUs < lastPublishedTimestampUs || !retirement.IsLive(in frame) ||
                frame.Width != frame.Texture.width || frame.Height != frame.Texture.height ||
                frame.Width <= 0 || frame.Height <= 0)
                return false;
            latest = frame;
            hasLatest = true;
            lastFrameId = frame.FrameId;
            lastPublishedTimestampUs = frame.PublishedTimestampUs;
            return true;
        }

        public bool TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame)
        {
            retirement.CheckThread();
            frame = default;
            if (!active || !hasLatest || latest.FrameId <= afterFrameId || !retirement.IsLive(in latest)) return false;
            frame = latest;
            return true;
        }

        public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease)
        {
            retirement.CheckThread();
            lease = default;
            return active && hasLatest && frame.SourceId == sourceId && frame.Generation == generation &&
                frame.FrameId == latest.FrameId && frame.ResourceToken == latest.ResourceToken &&
                frame.Texture == latest.Texture && retirement.TryAcquire(in frame, out lease);
        }
    }
}
