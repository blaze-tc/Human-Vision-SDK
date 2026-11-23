using UnityEngine.Scripting;

namespace HumanVision
{
    /// <summary>Input ownership policy. Compatibility keeps Unity-owned video and camera inputs.</summary>
    public enum HumanVisionExecutionPolicy { CompatibleUnityInput, IndependentPreferred, IndependentRequired }

    /// <summary>A native counter snapshot. Sampling it does not advance inference or publish results.</summary>
    [Preserve]
    public struct HumanVisionRuntimeProgress
    {
        public ulong SourceId, Generation, ConfigEpoch;
        /// <summary>The canonical result frame, separately from the newest admitted input.</summary>
        public long FrameId;
        public ulong InputFrameId, NativeInputFrames, NativeFreshFrames, InputErrors, RuntimeErrors, ResultOverruns;
        /// <summary>Local decode-to-query age. -1 means no valid result; this is not sensor latency.</summary>
        public long ResultAgeMicroseconds;
        public string ActualSource, ActualImageService, ActualBackend, ExecutionMode, CapabilityLevel, FallbackReason;
        public bool UsesGpuImageService, Streaming;
        public uint State;
        public long RegionRevision;
    }

    // Source configuration is an SDK implementation detail, not a vendor/model type in gameplay APIs.
    internal sealed class HumanVisionNativeSourceConfiguration
    {
        internal string Library, Url, HardwareCodec;
        internal uint Width, Height, TimeoutMs = 15000, ReconnectDelayMs = 1000, RotationDegrees;
        internal bool Tcp = true, Mirror, Preview = true;
        internal ulong SourceId;
        internal HumanVisionNativeSourceConfiguration Clone() => (HumanVisionNativeSourceConfiguration)MemberwiseClone();
    }
}
