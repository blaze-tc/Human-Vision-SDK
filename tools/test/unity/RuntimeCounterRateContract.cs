using System;
#if LEGACY_OBSERVED_RATE
using Window = CounterRateContract.LegacyWindow;
#else
using Window = HumanVisionAcceptance.RuntimeCounterRateWindow;
#endif

namespace CounterRateContract
{
    internal static class Program
    {
        private static int passed, failed;
        private static void Equal(double actual, double expected) { if (Math.Abs(actual - expected) > .00001) throw new Exception(actual + " != " + expected); }
        private static void Check(string name, Action test) { try { test(); ++passed; Console.WriteLine("PASS " + name); } catch (Exception e) { ++failed; Console.WriteLine("FAIL " + name + ": " + e.Message); } }
        private static Window Seed() { var w = new Window(); w.Observe(1, 1, 1, 10, 0, 0, 0, 0); return w; }
        private static int Main()
        {
            Check("first sample is unknown", () => Equal(Seed().First, -1));
            Check("raw 30 at Unity 15 uses real counter delta", () => { var w = Seed(); for (int i=1;i<=15;i++) w.Observe(1,1,1,10+i*1000000L/15,(ulong)i*2,0,0,0); Equal(w.First,30); });
            Check("decode live claim and results are distinct counters", () => { var w=Seed();w.Observe(1,1,1,1000010,30,29,14,13);Equal(w.First,30);Equal(w.Second,29);Equal(w.Third,14);Equal(w.Fourth,13); });
            Check("source switch clears previous rate", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Observe(2,1,1,1100010,100,0,0,0);Equal(w.First,-1); });
            Check("generation switch clears previous rate", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Observe(1,2,1,1100010,0,0,0,0);Equal(w.First,-1); });
            Check("analysis epoch switch resets processing window", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Observe(1,1,2,1100010,1,0,0,0);Equal(w.First,-1); });
            Check("source window ignores model epoch by constant key", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Observe(1,1,1,2000010,60,0,0,0);Equal(w.First,30); });
            Check("counter decrease rebaselines instead of unsigned wrap", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Observe(1,1,1,2000010,1,0,0,0);Equal(w.First,-1); });
            Check("any channel decrease invalidates common interval", () => { var w=Seed();w.Observe(1,1,1,1000010,30,29,20,10);w.Observe(1,1,1,2000010,60,58,40,1);Equal(w.First,-1); });
            Check("backward clock invalidates previous rate", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Observe(1,1,1,900010,31,0,0,0);Equal(w.First,-1); });
            Check("short window remains unknown", () => { var w=Seed();w.Observe(1,1,1,500010,15,0,0,0);Equal(w.First,-1); });
            Check("duplicate native snapshot does not fabricate frames", () => { var w=Seed();for(int i=0;i<100;i++)w.Observe(1,1,1,10,0,0,0,0);w.Observe(1,1,1,1000010,30,0,0,0);Equal(w.First,30); });
            Check("two second pause measures actual native progress", () => { var w=Seed();w.Observe(1,1,1,2000010,60,60,37,37);Equal(w.First,30);Equal(w.Third,18.5); });
            Check("valid stalled source reports zero", () => { var w=Seed();w.Observe(1,1,1,1000010,0,0,0,0);Equal(w.First,0); });
            Check("large cumulative values subtract before conversion", () => { var w=new Window();w.Observe(1,1,1,10,ulong.MaxValue-30,0,0,0);w.Observe(1,1,1,1000010,ulong.MaxValue,0,0,0);Equal(w.First,30); });
            Check("unavailable source clears stale rate", () => { var w=Seed();w.Observe(1,1,1,1000010,30,0,0,0);w.Reset();Equal(w.First,-1); });
            Console.WriteLine(passed + " passed, " + failed + " failed"); return failed==0?0:1;
        }
    }
#if LEGACY_OBSERVED_RATE
    // Runs the actual existing SDK observer. RED demonstrates why a rate of
    // distinct IDs observed on the Unity thread cannot represent source FPS.
    internal sealed class LegacyWindow
    {
        private readonly HumanVision.HumanVisionObservedRate counter = new HumanVision.HumanVisionObservedRate();
        private bool seeded;
        public double First => counter.Rate;
        public double Second => counter.Rate;
        public double Third => counter.Rate;
        public double Fourth => counter.Rate;
        public void Reset() { seeded=false;counter.Reset(0); }
        public void Observe(ulong source,ulong generation,ulong epoch,long us,ulong one,ulong two,ulong three,ulong four) {
            if (!seeded) { seeded=true;counter.Reset(us/1000000d);return; }
            counter.Observe((long)one);counter.Tick(us/1000000d);
        }
    }
#endif
}
