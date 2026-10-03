using System;
using HumanVision.Input;
namespace HumanVision.Demo
{
    // Maps a paired local monotonic epoch while retaining the full elapsed age.
    // Observation origin stays UnityObserved/LocalDecode; numeric output is in
    // the destination clock domain. Decode PTS is never used as capture time.
    internal static class InputTimestampMapping
    {
        internal static long Map(long observationUs, long sourceNowUs, long destinationNowUs)
        {
            if (observationUs < 0 || sourceNowUs < 0 || destinationNowUs < 0 || observationUs > sourceNowUs)
                throw new ArgumentException("Invalid or future local monotonic observation timestamp.");
            long ageUs = sourceNowUs - observationUs;
            if (ageUs > destinationNowUs)
                throw new ArgumentException("Observation age precedes the destination clock epoch.");
            return destinationNowUs - ageUs;
        }
        internal static long ToUnity(in HumanVisionTextureFrame frame, long inputNowUs, long unityNowUs)
        {
            if (frame.PublishedClockDomain != FrameClockDomain.InputMonotonic || frame.SourceTimestampUs < 0 ||
                (frame.TimestampKind != FrameTimestampKind.UnityObserved && frame.TimestampKind != FrameTimestampKind.LocalDecode) ||
                (frame.SourceClockDomain == FrameClockDomain.InputMonotonic
                    ? frame.SourceClockId != 0 || frame.SourceTimestampUs > frame.PublishedTimestampUs
                    : frame.SourceClockDomain != FrameClockDomain.SourceLocalMonotonic || frame.SourceClockId == 0 || frame.TimestampKind != FrameTimestampKind.LocalDecode))
                throw new ArgumentException("Input observation clock domain or origin is invalid.");
            // InputMonotonic observation includes normalization/publication queue age.
            // Unmapped local decode clocks retain publication age only; their PTS/origin stays separate.
            long observedUs = frame.SourceClockDomain == FrameClockDomain.InputMonotonic
                ? frame.SourceTimestampUs : frame.PublishedTimestampUs;
            Map(frame.PublishedTimestampUs, inputNowUs, unityNowUs); // reject future publication too
            return Map(observedUs, inputNowUs, unityNowUs);
        }
    }
}
