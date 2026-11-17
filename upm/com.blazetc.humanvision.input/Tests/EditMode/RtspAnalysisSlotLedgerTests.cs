using NUnit.Framework;

namespace HumanVision.Input.Tests
{
    public sealed class RtspAnalysisSlotLedgerTests
    {
        [Test]
        public void SuccessfulNativeReturnAndReadbackAreRecordedIndependently()
        {
            var ledger = new RtspAnalysisSlotLedger(41);
            ledger.BeginGeneration(7);
            ledger.Publish(7, 101, 2, 73, 1, 1000);
            ledger.RecordSnapshot(41, 7, 101, 1040, RtspAnalysisSnapshotStatus.FencePassed);
            ledger.RecordRelease(7, 101, 2, 74, 1, 1050, 0);
            ledger.RecordRelease(7, 101, 2, 73, 1, 1060, -2);
            var rows = new RtspAnalysisSlotTiming[1];
            Assert.That(ledger.CopyTo(rows), Is.EqualTo(1));
            Assert.That(rows[0].ReleasedUs, Is.EqualTo(-1));
            Assert.That(rows[0].ReleaseAttempts, Is.EqualTo(1));
            Assert.That(rows[0].LastReleaseResult, Is.EqualTo(-2));
            ledger.RecordRelease(7, 101, 2, 73, 1, 1061, 0);
            ledger.RecordReadback(41, 7, 101, 1140, true);
            ledger.CopyTo(rows);
            Assert.That(rows[0].SourceId, Is.EqualTo(41));
            Assert.That(rows[0].Generation, Is.EqualTo(7));
            Assert.That(rows[0].FrameId, Is.EqualTo(101));
            Assert.That(rows[0].NativeGeneration, Is.EqualTo(2));
            Assert.That(rows[0].NativeSequence, Is.EqualTo(73));
            Assert.That(rows[0].Slot, Is.EqualTo(1));
            Assert.That(rows[0].AnalysisPublishedUs, Is.EqualTo(1000));
            Assert.That(rows[0].SnapshotObservedUs, Is.EqualTo(1040));
            Assert.That(rows[0].SnapshotStatus, Is.EqualTo(RtspAnalysisSnapshotStatus.FencePassed));
            Assert.That(rows[0].ReleasedUs, Is.EqualTo(1061));
            Assert.That(rows[0].ReleaseAttempts, Is.EqualTo(2));
            Assert.That(rows[0].ReadbackObservedUs, Is.EqualTo(1140));
            Assert.That(rows[0].ReadbackSucceeded, Is.True);
            Assert.That(rows[0].LastUpdatedUs, Is.EqualTo(1140));
        }

        [Test]
        public void FailedQueryAndReadbackFallbackNeverImplyFenceWasObserved()
        {
            var ledger = new RtspAnalysisSlotLedger(41);
            ledger.BeginGeneration(7);
            ledger.Publish(7, 101, 2, 73, 1, 1000);
            ledger.RecordSnapshot(41, 7, 101, 1040, RtspAnalysisSnapshotStatus.FenceQueryFailed);
            var rows = new RtspAnalysisSlotTiming[1];
            ledger.CopyTo(rows);
            Assert.That(rows[0].SnapshotObservedUs, Is.EqualTo(-1));
            Assert.That(rows[0].SnapshotStatus, Is.EqualTo(RtspAnalysisSnapshotStatus.FenceQueryFailed));
            ledger.RecordReadback(41, 7, 101, 1110, true);
            ledger.RecordSnapshot(41, 7, 101, 1110, RtspAnalysisSnapshotStatus.ReadbackCompletedFenceQueryFailed);
            ledger.CopyTo(rows);
            Assert.That(rows[0].SnapshotObservedUs, Is.EqualTo(-1));
            Assert.That(rows[0].SnapshotStatus, Is.EqualTo(RtspAnalysisSnapshotStatus.ReadbackCompletedFenceQueryFailed));
            Assert.That(rows[0].ReadbackObservedUs, Is.EqualTo(1110));
        }

        [Test]
        public void LateCallbacksCannotChangeClosedOrSupersededSourceGenerations()
        {
            var ledger = new RtspAnalysisSlotLedger(41);
            ledger.BeginGeneration(7);
            ledger.Publish(7, 101, 2, 73, 1, 1000);
            ledger.Close();
            ledger.RecordReadback(41, 7, 101, 1010, true);
            ledger.BeginGeneration(8);
            ledger.Publish(8, 102, 3, 73, 1, 1020);
            ledger.RecordSnapshot(41, 7, 101, 1030, RtspAnalysisSnapshotStatus.NativeFencePassed);
            ledger.RecordSnapshot(42, 8, 102, 1040, RtspAnalysisSnapshotStatus.NativeFencePassed);
            ledger.RecordReadback(41, 8, 102, 1050, false);
            var rows = new RtspAnalysisSlotTiming[2];
            Assert.That(ledger.CopyTo(rows), Is.EqualTo(2));
            Assert.That(rows[0].ReadbackObservedUs, Is.EqualTo(-1));
            Assert.That(rows[0].SnapshotObservedUs, Is.EqualTo(-1));
            Assert.That(rows[1].SnapshotObservedUs, Is.EqualTo(-1));
            Assert.That(rows[1].ReadbackObservedUs, Is.EqualTo(1050));
            Assert.That(rows[1].ReadbackSucceeded, Is.False);
        }

        [Test]
        public void RecordingStaysBoundedAndRetainsAStillOwnedNativeSlot()
        {
            var ledger = new RtspAnalysisSlotLedger(41);
            ledger.BeginGeneration(7);
            ledger.Publish(7, 1, 2, 1, 1, 1);
            const int last = RtspAnalysisSlotTiming.Capacity + 10;
            for (int i = 2; i <= last; ++i) {
                ledger.Publish(7, i, 2, (ulong)i, 2, i * 10);
                ledger.RecordRelease(7, i, 2, (ulong)i, 2, i * 10 + 1, 0);
            }
            var rows = new RtspAnalysisSlotTiming[RtspAnalysisSlotTiming.Capacity];
            Assert.That(ledger.CopyTo(rows), Is.EqualTo(rows.Length));
            Assert.That(rows[0].FrameId, Is.EqualTo(1));
            Assert.That(rows[rows.Length - 1].FrameId, Is.EqualTo(last));
            for (int i = 1; i < rows.Length; ++i) Assert.That(rows[i].FrameId, Is.GreaterThan(rows[i - 1].FrameId));
            ledger.RecordRelease(7, 1, 2, 1, 1, last * 10 + 5, 0);
            ledger.CopyTo(rows);
            Assert.That(rows[0].ReleasedUs, Is.EqualTo(last * 10 + 5));
        }
    }
}
