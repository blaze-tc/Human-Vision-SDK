using System.Diagnostics;

namespace HumanVision.Input
{
    /// <summary>
    /// Independent input-package clock, shared by all managed input sources in this runtime
    /// domain. Zero is OriginStopwatchTicks, sampled when this type initializes. It is neither
    /// UTC nor Unity game time nor any SDK/native clock. A domain reload/process restart starts
    /// a new origin: never compare persisted timestamps or timestamps from another process.
    /// Reconnection, generation changes and pause/timeScale do not reset this clock.
    /// </summary>
    public static class InputMonotonicClock
    {
        public static readonly long OriginStopwatchTicks = Stopwatch.GetTimestamp();
        public static long StopwatchFrequency => Stopwatch.Frequency;
        public static FrameClockDomain Domain => FrameClockDomain.InputMonotonic;

        /// <summary>Elapsed microseconds, truncated toward zero; equal readings are allowed.</summary>
        public static long NowUs
        {
            get
            {
                var ticks = Stopwatch.GetTimestamp() - OriginStopwatchTicks;
                var frequency = Stopwatch.Frequency;
                // Split whole seconds before multiplication to avoid tick-conversion overflow.
                return checked((ticks / frequency) * 1000000L +
                    (long)((ticks % frequency) * (1000000.0 / frequency)));
            }
        }
    }
}
