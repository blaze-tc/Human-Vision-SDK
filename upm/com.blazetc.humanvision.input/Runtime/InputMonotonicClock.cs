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

        /// <summary>Read elapsed time in the native input frame's own clock.</summary>
        public static bool TryGetNativeFrameAgeUs(ulong clockId, long sourceUs, out long ageUs)
        {
            ageUs = -1;
            if (clockId == 0 || sourceUs < 0) return false;
            var clock = NativeInputBindings.ClockInfo.Create();
            try {
                if (NativeInputBindings.HV_Input_QueryClock(ref clock) != 0 || clock.ClockId != clockId ||
                    clock.NowUs < sourceUs) return false;
            } catch (System.DllNotFoundException) {
                return false;
            } catch (System.EntryPointNotFoundException) {
                return false;
            }
            ageUs = clock.NowUs - sourceUs;
            return true;
        }

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
