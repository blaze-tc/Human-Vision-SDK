using System;

namespace HumanVision.Input
{
    public enum RtspAnalysisSnapshotStatus
    {
        Unknown,
        FencePassed,
        FenceQueryFailed,
        FenceUnavailable,
        NativeFencePassed,
        NativeCopyFailed,
        ReadbackCompletedBeforeFenceObserved,
        ReadbackCompletedFenceQueryFailed,
        ReadbackCompletedFenceUnavailable,
    }

    /// <summary>
    /// Observations for one managed analysis publication. Times use InputMonotonicClock; -1 means
    /// unobserved. ReleasedUs marks a successful native release call. SnapshotObservedUs is only
    /// populated for direct fence completion. A successful readback alone supplies an upper bound.
    /// Slot flags describe current ownership at the time CopyAnalysisSlotTimings is called.
    /// </summary>
    [Serializable]
    public struct RtspAnalysisSlotTiming
    {
        public const int Capacity = 64;
        public ulong SourceId, Generation, NativeGeneration, NativeSequence;
        public long FrameId, AnalysisPublishedUs, SnapshotObservedUs, ReleasedUs, ReadbackObservedUs, LastUpdatedUs;
        public int Slot, ReleaseAttempts, LastReleaseResult;
        public RtspAnalysisSnapshotStatus SnapshotStatus;
        public bool ReadbackSucceeded, SlotOutstanding, SlotIsLatest, SlotHasPendingCopy;
    }

    // Main-thread ledger. Keep currently owned native slots across the history bound, including
    // a permanently pinned slot. Superseded generations can be read but can no longer be updated.
    internal sealed class RtspAnalysisSlotLedger
    {
        private readonly RtspAnalysisSlotTiming[] rows = new RtspAnalysisSlotTiming[RtspAnalysisSlotTiming.Capacity];
        private readonly ulong sourceId;
        private ulong activeGeneration;
        private int count;

        internal RtspAnalysisSlotLedger(ulong sourceId) => this.sourceId = sourceId;
        internal void BeginGeneration(ulong generation) => activeGeneration = generation;
        internal void Close() => activeGeneration = 0;

        internal void Publish(ulong generation, long frameId, ulong nativeGeneration, ulong nativeSequence, int slot, long publishedUs)
        {
            if (count == rows.Length) {
                int replace = 0;
                while (replace < count && rows[replace].Generation == activeGeneration && rows[replace].ReleasedUs < 0)
                    ++replace;
                // This is unreachable with three native slots, but recording should never block input.
                if (replace == count) return;
                Array.Copy(rows, replace + 1, rows, replace, count - replace - 1);
                --count;
            }
            rows[count++] = new RtspAnalysisSlotTiming {
                SourceId = sourceId, Generation = generation, FrameId = frameId,
                NativeGeneration = nativeGeneration, NativeSequence = nativeSequence, Slot = slot,
                AnalysisPublishedUs = publishedUs, SnapshotObservedUs = -1, ReleasedUs = -1, ReadbackObservedUs = -1,
                LastUpdatedUs = publishedUs
            };
        }

        internal void RecordSnapshot(ulong eventSourceId, ulong generation, long frameId, long observedUs,
            RtspAnalysisSnapshotStatus status)
        {
            if (eventSourceId != sourceId || status == RtspAnalysisSnapshotStatus.Unknown) return;
            int index = Find(generation, frameId);
            if (index < 0) return;
            ref var row = ref rows[index];
            if (observedUs < row.AnalysisPublishedUs || row.SnapshotObservedUs >= 0) return;
            bool fencePassed = status == RtspAnalysisSnapshotStatus.FencePassed || status == RtspAnalysisSnapshotStatus.NativeFencePassed;
            if (fencePassed) row.SnapshotObservedUs = observedUs;
            else if (row.SnapshotStatus != RtspAnalysisSnapshotStatus.Unknown &&
                status != RtspAnalysisSnapshotStatus.ReadbackCompletedBeforeFenceObserved &&
                status != RtspAnalysisSnapshotStatus.ReadbackCompletedFenceQueryFailed &&
                status != RtspAnalysisSnapshotStatus.ReadbackCompletedFenceUnavailable) return;
            row.SnapshotStatus = status;
            row.LastUpdatedUs = Math.Max(row.LastUpdatedUs, observedUs);
        }

        internal void RecordRelease(ulong generation, long frameId, ulong nativeGeneration, ulong nativeSequence, int slot,
            long observedUs, int result)
        {
            int index = Find(generation, frameId);
            if (index < 0) return;
            ref var row = ref rows[index];
            if (row.NativeGeneration != nativeGeneration || row.NativeSequence != nativeSequence || row.Slot != slot ||
                observedUs < row.AnalysisPublishedUs || row.ReleasedUs >= 0) return;
            ++row.ReleaseAttempts;
            row.LastReleaseResult = result;
            if (result == 0) row.ReleasedUs = observedUs;
            row.LastUpdatedUs = Math.Max(row.LastUpdatedUs, observedUs);
        }

        internal void RecordReadback(ulong eventSourceId, ulong generation, long frameId, long observedUs, bool succeeded)
        {
            if (eventSourceId != sourceId) return;
            int index = Find(generation, frameId);
            if (index < 0) return;
            ref var row = ref rows[index];
            if (observedUs < row.AnalysisPublishedUs || row.ReadbackObservedUs >= 0) return;
            row.ReadbackObservedUs = observedUs;
            row.ReadbackSucceeded = succeeded;
            row.LastUpdatedUs = Math.Max(row.LastUpdatedUs, observedUs);
        }

        internal int CopyTo(RtspAnalysisSlotTiming[] destination)
        {
            int copied = Math.Min(destination.Length, count);
            Array.Copy(rows, count - copied, destination, 0, copied);
            return copied;
        }

        private int Find(ulong generation, long frameId)
        {
            if (generation != activeGeneration || generation == 0) return -1;
            for (int i = count - 1; i >= 0 && rows[i].FrameId >= frameId; --i)
                if (rows[i].FrameId == frameId && rows[i].Generation == generation) return i;
            return -1;
        }
    }
}
