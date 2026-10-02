using System;
using UnityEngine;

namespace HumanVision.Input
{
    public enum FrameRowOrigin { UnityBottomLeft, NativeTopLeft }
    public enum FrameColorSpace { Srgb, Linear, Unknown }
    /// <summary>The event represented by SourceTimestampUs, never the publication clock.</summary>
    public enum FrameTimestampKind { UnityObserved, LocalDecode }
    public enum FrameClockDomain { Unspecified, InputMonotonic, SourceLocalMonotonic }

    /// <summary>
    /// Description of a completed texture publication. Applied transforms have already been
    /// applied to the pixels; consumers must not rotate or mirror them again. Canonical source
    /// output is upright, UnityBottomLeft. PublishedTimestampUs always marks the Unity main-thread
    /// publication of the completed texture, sampled from InputMonotonicClock for either kind.
    /// SourceTimestampUs marks Unity texture observation (UnityObserved) or local decode completion
    /// (LocalDecode), separately from publication. These are observations, never sensor capture time.
    /// PresentationTimestampUs is stream PTS (-1 if unavailable), with its own stream time base;
    /// it can reset/regress and is never comparable with either observation/publication clock.
    /// Published time must be nonnegative and nondecreasing across this source's generations;
    /// equal microseconds are permitted. FrameId must still strictly increase.
    /// </summary>
    public struct HumanVisionTextureFrame
    {
        public ulong SourceId, Generation;
        public long FrameId, PublishedTimestampUs, PresentationTimestampUs;
        public long SourceTimestampUs;
        public FrameClockDomain SourceClockDomain;
        /// <summary>
        /// Zero for InputMonotonic. SourceLocalMonotonic requires a nonzero producer-assigned
        /// clock identity and elapsed microseconds from that clock's declared initialization origin.
        /// Keep this identity only while that origin survives; assign a new identity if it resets.
        /// Native producers must declare origin/current-time conversion in their own clock contract.
        /// Such unmapped clocks cannot be subtracted from InputMonotonicClock.NowUs. Only an
        /// explicit validated mapping permits reporting a native observation as InputMonotonic.
        /// </summary>
        public ulong SourceClockId;
        public FrameClockDomain PublishedClockDomain => FrameClockDomain.InputMonotonic;
        public Texture Texture;
        public int Width, Height, AppliedRotationDegrees;
        public bool AppliedMirrorX;
        public FrameRowOrigin RowOrigin;
        public FrameColorSpace ColorSpace;
        public FrameTimestampKind TimestampKind;
        public ulong ResourceToken;

        public HumanVisionTextureFrame(ulong sourceId, ulong generation, long frameId,
            Texture texture, long publishedTimestampUs, long presentationTimestampUs,
            int appliedRotationDegrees, bool appliedMirrorX, FrameRowOrigin rowOrigin,
            FrameColorSpace colorSpace, FrameTimestampKind timestampKind, ulong resourceToken)
            : this(sourceId, generation, frameId, texture, publishedTimestampUs, presentationTimestampUs,
                appliedRotationDegrees, appliedMirrorX, rowOrigin, colorSpace, timestampKind,
                resourceToken, publishedTimestampUs, FrameClockDomain.InputMonotonic, 0)
        {
            if (timestampKind != FrameTimestampKind.UnityObserved)
                throw new ArgumentException("LocalDecode requires an explicit source timestamp and clock domain.", nameof(timestampKind));
        }

        public HumanVisionTextureFrame(ulong sourceId, ulong generation, long frameId,
            Texture texture, long publishedTimestampUs, long presentationTimestampUs,
            int appliedRotationDegrees, bool appliedMirrorX, FrameRowOrigin rowOrigin,
            FrameColorSpace colorSpace, FrameTimestampKind timestampKind, ulong resourceToken,
            long sourceTimestampUs, FrameClockDomain sourceClockDomain, ulong sourceClockId)
        {
            if (texture == null) throw new ArgumentNullException(nameof(texture));
            SourceTimestampUs = sourceTimestampUs;
            SourceClockDomain = sourceClockDomain;
            SourceClockId = sourceClockId;
            SourceId = sourceId;
            Generation = generation;
            FrameId = frameId;
            Texture = texture;
            Width = texture.width;
            Height = texture.height;
            PublishedTimestampUs = publishedTimestampUs;
            PresentationTimestampUs = presentationTimestampUs;
            AppliedRotationDegrees = appliedRotationDegrees;
            AppliedMirrorX = appliedMirrorX;
            RowOrigin = rowOrigin;
            ColorSpace = colorSpace;
            TimestampKind = timestampKind;
            ResourceToken = resourceToken;
        }
        internal bool HasValidTimestamps()
        {
            if (PublishedTimestampUs < 0 || SourceTimestampUs < 0 ||
                (TimestampKind != FrameTimestampKind.UnityObserved && TimestampKind != FrameTimestampKind.LocalDecode))
                return false;
            if (SourceClockDomain == FrameClockDomain.InputMonotonic)
                return SourceClockId == 0 && SourceTimestampUs <= PublishedTimestampUs;
            return TimestampKind == FrameTimestampKind.LocalDecode &&
                SourceClockDomain == FrameClockDomain.SourceLocalMonotonic && SourceClockId != 0;
        }
    }
}
