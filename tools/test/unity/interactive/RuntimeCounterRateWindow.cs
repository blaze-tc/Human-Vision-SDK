namespace HumanVisionAcceptance
{
    // Rates are deltas of cumulative counters, never counts of observed frame
    // identities. A slow Unity consumer can still measure a 30 Hz native source.
    internal sealed class RuntimeCounterRateWindow
    {
        private bool seeded;
        private ulong sourceId, generation, epoch, first, second, third, fourth;
        private ulong lastFirst, lastSecond, lastThird, lastFourth;
        private long startUs, lastUs;
        internal double First { get; private set; } = -1;
        internal double Second { get; private set; } = -1;
        internal double Third { get; private set; } = -1;
        internal double Fourth { get; private set; } = -1;

        internal void Reset()
        {
            seeded = false;
            First = Second = Third = Fourth = -1;
        }

        internal void Observe(ulong source, ulong nativeGeneration, ulong keyEpoch,
            long timestampUs, ulong one, ulong two, ulong three, ulong four)
        {
            if (source == 0 || nativeGeneration == 0 || timestampUs < 0) { Reset(); return; }
            if (!seeded || source != sourceId || nativeGeneration != generation || keyEpoch != epoch ||
                timestampUs < lastUs || one < lastFirst || two < lastSecond || three < lastThird || four < lastFourth) {
                Reset(); seeded = true;
                sourceId = source; generation = nativeGeneration; epoch = keyEpoch;
                startUs = timestampUs; first = one; second = two; third = three; fourth = four;
            }
            lastUs = timestampUs; lastFirst = one; lastSecond = two; lastThird = three; lastFourth = four;
            long elapsedUs = timestampUs - startUs;
            if (elapsedUs < 1000000) return;
            double seconds = elapsedUs / 1000000d;
            // Subtract unsigned counters before converting to double, preserving
            // small real increments even near the cumulative counter's limit.
            First = (one - first) / seconds; Second = (two - second) / seconds;
            Third = (three - third) / seconds; Fourth = (four - fourth) / seconds;
            startUs = timestampUs; first = one; second = two; third = three; fourth = four;
        }
    }
}
