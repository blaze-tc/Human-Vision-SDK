using System;

namespace HumanVision.Demo.PC
{
    internal static class PcDemoConfiguration
    {
        public static string ProfileId(bool cpu, int capacity)
        {
            if (capacity < 1 || capacity > 8) throw new ArgumentOutOfRangeException(nameof(capacity), "Choose 1 to 8 people.");
            return cpu ? "windows-pc-cpu" : "windows-pc-directml";
        }
    }

    // Counts observations once, even when Unity renders a held result many times.
    internal sealed class PcObservationRate
    {
        private long _sequence;
        private int _count;
        private double _since;
        public double FramesPerSecond { get; private set; }
        public void Reset(double now) { _sequence = 0; _count = 0; _since = now; FramesPerSecond = 0; }
        public void Observe(long sequence, double now)
        {
            if (sequence > _sequence) { _sequence = sequence; _count++; }
            double elapsed = now - _since;
            if (elapsed >= 1) { FramesPerSecond = _count / elapsed; _count = 0; _since = now; }
        }
    }

    internal sealed class PcStartGeneration
    {
        private int _generation;
        public int Begin() { return ++_generation; }
        public void Cancel() { ++_generation; }
        public bool IsCurrent(int generation) { return generation == _generation; }
    }
}
