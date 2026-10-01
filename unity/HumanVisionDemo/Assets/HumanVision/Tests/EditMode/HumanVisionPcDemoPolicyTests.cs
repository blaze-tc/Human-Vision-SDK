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

        private static float GuiScale(float width, float height, float dpi = 0, float userScale = 1)
        {
            return (float)Policy("PcGuiLayout").GetMethod("Scale").Invoke(null,
                new object[] { width, height, dpi, userScale });
        }

        [TestCase(1280, 720)]
        [TestCase(1920, 1080)]
        [TestCase(2560, 1440)]
        [TestCase(3840, 2160)]
        [TestCase(720, 1280)]
        [TestCase(320, 240)]
        public void GuiExpandedAndCollapsedPanelsFitInsetSafeArea(int width, int height)
        {
            var safe = new UnityEngine.Rect(12, 18, width - 30, height - 42);
            foreach (bool expanded in new[] { true, false }) {
                var panel = (UnityEngine.Rect)Policy("PcGuiLayout").GetMethod("PanelPixels").Invoke(null,
                    new object[] { (float)width, (float)height, safe, 192f, 1.5f, expanded });
                Assert.That(panel.width, Is.GreaterThan(0));
                Assert.That(panel.height, Is.GreaterThan(0));
                Assert.That(panel.xMin, Is.GreaterThanOrEqualTo(safe.xMin));
                Assert.That(panel.yMin, Is.GreaterThanOrEqualTo(height - safe.yMax));
                Assert.That(panel.xMax, Is.LessThanOrEqualTo(safe.xMax));
                Assert.That(panel.yMax, Is.LessThanOrEqualTo(height - safe.yMin));
            }
        }

        [TestCase(400, 600, 120)]
        [TestCase(320, 240, 192)]
        [TestCase(1920, 1080, 0)]
        [TestCase(3840, 2160, 0)]
        public void GuiScrollContentReservesScrollbarAndPaddingInsteadOfExpandingForLongPaths(int width, int height, int dpi)
        {
            float scale = GuiScale(width, height, dpi);
            var panel = (UnityEngine.Rect)Policy("PcGuiLayout").GetMethod("PanelPixels").Invoke(null,
                new object[] { (float)width, (float)height, new UnityEngine.Rect(0, 0, width, height), (float)dpi, 1f, true });
            MethodInfo method = Policy("PcGuiLayout").GetMethod("ScrollContentWidth");
            Assert.That(method, Is.Not.Null, "Scroll content must have an explicit width budget.");
            float contentWidth = (float)method.Invoke(null, new object[] { panel.width, scale });
            Assert.That(contentWidth, Is.GreaterThan(0));
            Assert.That(contentWidth + 16 + 28, Is.LessThanOrEqualTo(panel.width / scale + .01f),
                "Panel padding and visible scrollbar must fit beside the content.");
            Assert.That(contentWidth, Is.LessThan(panel.width / scale));
        }

        [Test]
        public void GuiResolutionAndValidDpiIncreaseReadableScale()
        {
            Assert.That(GuiScale(1280, 720), Is.EqualTo(1));
            Assert.That(GuiScale(1920, 1080), Is.EqualTo(1.5f));
            Assert.That(GuiScale(2560, 1440), Is.EqualTo(2));
            Assert.That(GuiScale(3840, 2160), Is.EqualTo(3));
            Assert.That(GuiScale(1920, 1080, 192), Is.GreaterThan(GuiScale(1920, 1080, 96)));
            Assert.That(GuiScale(1920, 1080, 96, 1.25f), Is.GreaterThan(GuiScale(1920, 1080)));
        }

        [Test]
        public void GuiUnknownOrImplausibleDpiAndInvalidOverridesStayFinite()
        {
            foreach (float dpi in new[] { 0f, -1f, 20f, 9999f, float.NaN, float.PositiveInfinity })
                Assert.That(GuiScale(1920, 1080, dpi), Is.EqualTo(1.5f));
            foreach (float user in new[] { 0f, -1f, float.NaN, float.PositiveInfinity })
                Assert.That(GuiScale(1920, 1080, 0, user), Is.EqualTo(1.5f));
            Assert.That(GuiScale(1920, 1080, 0, 100), Is.EqualTo(3));
            Assert.That(GuiScale(320, 240), Is.GreaterThan(0));
        }

        [Test]
        public void GuiSmallViewportKeepsAllContentReachableByScroll()
        {
            var panel = (UnityEngine.Rect)Policy("PcGuiLayout").GetMethod("PanelPixels").Invoke(null,
                new object[] { 320f, 240f, new UnityEngine.Rect(0, 0, 320, 240), 0f, 1f, true });
            float viewport = (float)Policy("PcGuiLayout").GetMethod("ScrollViewportHeight").Invoke(null,
                new object[] { panel.height, GuiScale(320, 240) });
            Assert.That(viewport, Is.GreaterThan(0));
            Assert.That(viewport, Is.LessThan(panel.height));
            Assert.That(viewport, Is.EqualTo(panel.height / GuiScale(320, 240) - 64).Within(.01f));
            Assert.That(GuiScale(320, 240, 192, 2), Is.LessThanOrEqualTo(1));
            Assert.That(GuiScale(720, 1280, 384, 2), Is.LessThanOrEqualTo(720f / 320));
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
