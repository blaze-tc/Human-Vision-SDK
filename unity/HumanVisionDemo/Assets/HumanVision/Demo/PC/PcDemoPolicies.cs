using System;
using System.Text;

namespace HumanVision.Demo.PC
{
    internal static class PcDiagnostics
    {
        // Native sampling ages use steady_clock's epoch. PC captures use Unity's
        // acquisition clock; the frame source supplies the valid age in this HUD.
        public static string Format(string diagnostics)
        {
            if (string.IsNullOrEmpty(diagnostics)) return "";
            var text = new StringBuilder(diagnostics.Length);
            foreach (string line in diagnostics.Split('\n')) {
                if (line.StartsWith("Result age", StringComparison.Ordinal) ||
                    line.StartsWith("Sample age", StringComparison.Ordinal) ||
                    line.StartsWith("Sample state", StringComparison.Ordinal) ||
                    line.StartsWith("Tracked bodies", StringComparison.Ordinal) ||
                    line.StartsWith("Sampled bodies", StringComparison.Ordinal) ||
                    line.StartsWith("Raw body FPS", StringComparison.Ordinal)) continue;
                if (text.Length > 0) text.Append('\n');
                text.Append(line.Replace("Android mode:", "Runtime profile:"));
            }
            return text.ToString();
        }
    }

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
