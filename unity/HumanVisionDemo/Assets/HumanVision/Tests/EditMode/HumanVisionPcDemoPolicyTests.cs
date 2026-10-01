using System;
using System.Reflection;
using HumanVision.Demo;
using NUnit.Framework;

namespace HumanVision.Tests
{
    public sealed class HumanVisionPcDemoPolicyTests
    {
        private static Type Policy(string name)
        {
            Type type = typeof(VideoPlayerFrameSource).Assembly.GetType("HumanVision.Demo.PC." + name);
            Assert.That(type, Is.Not.Null, "PC demo behavior is missing: " + name);
            return type;
        }

        [Test]
        public void PcDiagnosticsNeverExposeAgeOrStateFromDifferentNativeClockEpoch()
        {
            Type type = Policy("PcDiagnostics");
            string text = "Android mode: windows-pc-directml; input: CPU\nActual backend=backend.ort.directml\nPipeline=pipeline.rtmo\nBody pre/infer/post ms=1 / 12 / 2\nResult age ms=999999\nSample age ms=999999\nSample state=Stale\nTracked bodies=0\nSampled bodies=0\nRaw body FPS=99";
            string formatted = (string)type.GetMethod("Format").Invoke(null, new object[] { text });
            Assert.That(formatted, Does.Contain("backend.ort.directml"));
            Assert.That(formatted, Does.Contain("pipeline.rtmo"));
            Assert.That(formatted, Does.Contain("1 / 12 / 2"));
            Assert.That(formatted, Does.Not.Contain("999999"));
            Assert.That(formatted, Does.Not.Contain("Stale"));
            Assert.That(formatted, Does.Not.Contain("Tracked bodies="));
            Assert.That(formatted, Does.Not.Contain("Sampled bodies="));
            Assert.That(formatted, Does.Not.Contain("Raw body FPS="));
            Assert.That(formatted, Does.Not.Contain("Android mode:"));
        }

        [Test]
        public void CapacityOneAndEightSelectSameExplicitBodyOnlyProfile()
        {
            Type policy = Policy("PcDemoConfiguration");
            MethodInfo method = policy.GetMethod("ProfileId");
            Assert.That(method.Invoke(null, new object[] { false, 1 }), Is.EqualTo("windows-pc-directml"));
            Assert.That(method.Invoke(null, new object[] { false, 8 }), Is.EqualTo("windows-pc-directml"));
            Assert.That(method.Invoke(null, new object[] { true, 7 }), Is.EqualTo("windows-pc-cpu"));
            Assert.Throws<TargetInvocationException>(() => method.Invoke(null, new object[] { false, 9 }));
        }

        [Test]
        public void RepeatedHeldResultsNeverIncreaseFreshObservationRate()
        {
            Type type = Policy("PcObservationRate");
            object rate = Activator.CreateInstance(type);
            MethodInfo reset = type.GetMethod("Reset");
            MethodInfo observe = type.GetMethod("Observe");
            reset.Invoke(rate, new object[] { 10.0 });
            for (int i = 0; i < 60; i++) observe.Invoke(rate, new object[] { 1L, 10.0 + i / 60.0 });
            observe.Invoke(rate, new object[] { 2L, 11.0 });
            Assert.That((double)type.GetProperty("FramesPerSecond").GetValue(rate), Is.EqualTo(2.0).Within(.0001));
            observe.Invoke(rate, new object[] { 2L, 12.0 });
            Assert.That((double)type.GetProperty("FramesPerSecond").GetValue(rate), Is.Zero);
            reset.Invoke(rate, new object[] { 20.0 });
            observe.Invoke(rate, new object[] { 1L, 21.0 });
            Assert.That((double)type.GetProperty("FramesPerSecond").GetValue(rate), Is.EqualTo(1.0));
        }

        [Test]
        public void PcAcquisitionClockStaysMonotonicWhenVideoSeeksOrLoops()
        {
            MethodInfo select = typeof(VideoPlayerFrameSource).GetMethod("SelectVideoTimestampUs", BindingFlags.NonPublic | BindingFlags.Static);
            Assert.That(select, Is.Not.Null, "PC acquisition timestamp selection is missing.");
            Assert.That(select.Invoke(null, new object[] { 37000000L, 1000000L, true }), Is.EqualTo(1000000L));
            Assert.That(select.Invoke(null, new object[] { 0L, 2000000L, true }), Is.EqualTo(2000000L));
            Assert.That(select.Invoke(null, new object[] { 37000000L, 1000000L, false }), Is.EqualTo(37000000L));
        }

        [Test]
        public void StopInvalidatesPreparationAndOnlyLatestStartCanComplete()
        {
            Type type = Policy("PcStartGeneration");
            object gate = Activator.CreateInstance(type);
            MethodInfo begin = type.GetMethod("Begin");
            MethodInfo current = type.GetMethod("IsCurrent");
            MethodInfo cancel = type.GetMethod("Cancel");
            object first = begin.Invoke(gate, null);
            cancel.Invoke(gate, null);
            Assert.That(current.Invoke(gate, new[] { first }), Is.EqualTo(false));
            object second = begin.Invoke(gate, null);
            Assert.That(current.Invoke(gate, new[] { first }), Is.EqualTo(false));
            Assert.That(current.Invoke(gate, new[] { second }), Is.EqualTo(true));
        }
    }
}
