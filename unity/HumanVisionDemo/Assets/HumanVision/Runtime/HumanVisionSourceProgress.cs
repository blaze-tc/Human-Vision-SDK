using UnityEngine.Scripting;
namespace HumanVision
{
    /// <summary>Source-owned cumulative counters, independent of inference claims and results.</summary>
    [Preserve]
    public struct HumanVisionSourceProgress
    {
        public ulong SourceId, Generation, ConfigEpoch;
        public ulong DecodedFrames, AcquiredFrames, LivePublishedFrames, AnalysisPublishedFrames, AnalysisClaimedFrames, LiveDroppedFrames;
        /// <summary>Runtime monotonic snapshot time in microseconds; not sensor time.</summary>
        public long TimestampUs;
        public uint State, SourceWidth, SourceHeight, LiveWidth, LiveHeight;
        public bool Streaming, LiveEnabled, LiveAvailable;
    }
}
