namespace HumanVision
{
    /// <summary>主线程观测速率：同一帧只计一次；停流窗口归零，不使用渲染帧率补数。</summary>
    internal sealed class HumanVisionObservedRate
    {
        private long lastId = -1;
        private int count;
        private double start;
        public double Rate { get; private set; }
        public void Reset(double now) { lastId = -1; count = 0; start = now; Rate = 0; }
        public void Observe(long id) { if (id <= lastId) return; lastId = id; ++count; }
        public void Tick(double now)
        {
            double elapsed = now - start;
            if (elapsed < 1) return;
            Rate = count / elapsed; count = 0; start = now;
        }
    }
}
