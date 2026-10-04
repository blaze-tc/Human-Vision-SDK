namespace HumanVision.Demo
{
    // Counts manager body-result deliveries, including empty/partial bodies. This
    // diagnostic does not establish native coverage or per-person/joint freshness.
    internal sealed class DemoObservationRate
    {
        private double windowStart, lastDelivery, rate;
        private int deliveries;
        private bool hasDelivery;
        public long LastSequence { get; private set; }

        public void Reset(double now, long sequence)
        {
            windowStart = now; lastDelivery = now; rate = 0;
            deliveries = 0; hasDelivery = false; LastSequence = sequence;
        }

        public void Observe(long sequence, double now)
        {
            if (sequence < LastSequence) { Reset(now, sequence); return; }
            if (sequence == LastSequence) return; // Also excludes hand-only notifications.
            if (hasDelivery && now - lastDelivery >= 1) {
                windowStart = now; deliveries = 0; rate = 0;
            }
            LastSequence = sequence;
            lastDelivery = now; hasDelivery = true;
            ++deliveries; // Gaps are unknown deliveries, never sequence deltas.
        }

        public double Read(double now)
        {
            if (!hasDelivery) {
                if (now - windowStart >= 1) windowStart = now;
                return 0;
            }
            if (now - lastDelivery >= 1) {
                windowStart = now; deliveries = 0; rate = 0;
                return 0;
            }
            double elapsed = now - windowStart;
            if (elapsed >= 1) {
                rate = deliveries / elapsed;
                windowStart = now; deliveries = 0;
            }
            return rate;
        }
    }
}
