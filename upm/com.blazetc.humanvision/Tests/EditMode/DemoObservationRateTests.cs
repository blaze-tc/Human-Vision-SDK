using System;
using System.Reflection;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class DemoObservationRateTests
    {
        private sealed class Rate
        {
            private readonly object target;
            private readonly Type type;
            public Rate()
            {
                type = typeof(HumanVisionUnifiedDemoCanvas).Assembly.GetType("HumanVision.Demo.DemoObservationRate");
                Assert.That(type, Is.Not.Null, "Manager delivery rate must be measured independently of native InferenceFps.");
                target = Activator.CreateInstance(type, true);
            }
            public void Reset(double now, long sequence = 0) => type.GetMethod("Reset").Invoke(target, new object[] { now, sequence });
            public void Observe(long sequence, double now) => type.GetMethod("Observe").Invoke(target, new object[] { sequence, now });
            public double Read(double now) => (double)type.GetMethod("Read").Invoke(target, new object[] { now });
        }

        [Test] public void DeliveryRateUsesActualElapsedSecondsAndWaitsForOneSecond()
        {
            var rate = new Rate(); rate.Reset(10);
            for (int i = 1; i <= 9; i++) rate.Observe(i, 10 + i * .1);
            Assert.That(rate.Read(10.9), Is.Zero);
            for (int i = 10; i <= 16; i++) rate.Observe(i, 10 + i * .1);
            Assert.That(rate.Read(11.6), Is.EqualTo(10).Within(.000001));
        }

        [Test] public void DuplicateHandNotificationsAndSkippedSequencesNeverInventFrames()
        {
            var rate = new Rate(); rate.Reset(0, 40);
            rate.Observe(40, .1); // Existing snapshot / hand-only notification.
            rate.Observe(41, .2); rate.Observe(41, .3);
            rate.Observe(49, .8); rate.Observe(49, .9);
            Assert.That(rate.Read(1), Is.EqualTo(2));
        }

        [Test] public void IdleClearsTheDisplayedRateAndRestartsTheElapsedWindow()
        {
            var rate = new Rate(); rate.Reset(0);
            rate.Observe(1, .2); rate.Observe(2, .9);
            Assert.That(rate.Read(1), Is.EqualTo(2));
            Assert.That(rate.Read(1.91), Is.Zero);
            rate.Observe(3, 20); rate.Observe(4, 20.8);
            Assert.That(rate.Read(20.9), Is.Zero);
            Assert.That(rate.Read(21), Is.EqualTo(2));
        }

        [Test] public void ResetBaselinesExistingResultsAndClearsTheOldEpoch()
        {
            var rate = new Rate(); rate.Reset(0);
            rate.Observe(1, .5); Assert.That(rate.Read(1), Is.EqualTo(1));
            rate.Reset(5, 90); rate.Observe(90, 5.1);
            Assert.That(rate.Read(6), Is.Zero);
            rate.Observe(91, 6.2); Assert.That(rate.Read(7), Is.EqualTo(1));
        }

        [Test] public void SequenceRegressionStartsANewBaselineInsteadOfKeepingTheOldRate()
        {
            var rate = new Rate(); rate.Reset(0, 100);
            rate.Observe(101, .5); Assert.That(rate.Read(1), Is.EqualTo(1));
            rate.Observe(1, 1.1); Assert.That(rate.Read(1.2), Is.Zero);
            rate.Observe(2, 1.5); Assert.That(rate.Read(2.1), Is.EqualTo(1));
        }

        [Test] public void RepeatedReadsNeverCountRenderOrSamplingUpdates()
        {
            var rate = new Rate(); rate.Reset(0);
            rate.Observe(1, .5);
            for (int i = 1; i < 100; i++) Assert.That(rate.Read(i * .01), Is.Zero);
            Assert.That(rate.Read(1), Is.EqualTo(1));
            Assert.That(rate.Read(2), Is.Zero);
        }

        // The integration tests exercise the real canvas/manager event boundary.
        // This session emits controlled diagnostic notifications, never inference fixtures.
        private sealed class Session : IHumanVisionSession
        {
            public long Sequence; public int Count;
            public int MaxBodies => 8; public HumanVisionBody[] Bodies => null;
            public int BodyCount => Count; public long ResultSequence => Sequence;
            public long SourceFrameId => Sequence; public long SourceTimestampUs => 0;
            public HumanVisionStats Stats => default; // Native FPS deliberately unavailable/zero.
            public bool SubmitFrame(IntPtr data, int w, int h, int s, HumanVisionPixelFormat f, long id, long t, int b) => false;
            public bool PollLatestResult() => true;
            public void SetRegions(Rect[] regions, long revision) { }
            public bool CopyRegions(long sequence, int[] indices, out long revision) { revision = 0; return false; }
            public void RefreshStats() { }
            public void ReconfigureMaxBodies(int maxBodies) { }
            public void Dispose() { }
        }
        private sealed class Source : IHumanVisionFrameSource
        {
            public ulong Generation = 1;
            public InputSourceState State { get; set; } = InputSourceState.Streaming;
            public string LastError => ""; public Texture CurrentTexture => null;
            public void Open(HumanVisionSourceSettings settings) { }
            public void Close() { State = InputSourceState.Stopped; }
            public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) { lease = default; return false; }
            public bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame)
            {
                frame = new HumanVisionTextureFrame { SourceId = 1, Generation = Generation, FrameId = 1 };
                return true;
            }
        }
        private static void Set(object target, string field, object value) => target.GetType().GetField(field, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(target, value);
        private static void Invoke(object target, string method) => target.GetType().GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic).Invoke(target, null);

        [Test] public void HudShowsDeliveredFramesEvenWhenNativeFpsIsZeroAndCountsEmptyAndEightBodyFramesOnce()
        {
            WithCanvas((canvas, manager, session, source, status) => {
                for (int i = 1; i <= 10; i++) { session.Count = i % 2 == 0 ? 8 : 0; session.Sequence = i; Invoke(manager, "Update"); Invoke(manager, "Update"); }
                Invoke(canvas, "Update");
                Assert.That(status.text, Does.Contain("Manager raw result delivery FPS:"));
                Assert.That(status.text, Does.Not.Contain("Raw fresh complete FPS:"));
                Assert.That(status.text, Does.Contain("30 fresh complete observations/s remains unaccepted."));
                string deliveryLine = Array.Find(status.text.Split('\n'), line => line.StartsWith("Manager raw result delivery FPS:"));
                double displayed = double.Parse(deliveryLine.Split(':')[1].Trim().Split(' ')[0], System.Globalization.CultureInfo.CurrentCulture);
                Assert.That(displayed, Is.EqualTo(5).Within(.1), "HUD must show measured delivery rate even while native InferenceFps is zero.");
                var rate = canvas.GetType().GetField("observationRate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(canvas);
                var fps = (double)rate.GetType().GetMethod("Read").Invoke(rate, new object[] { Time.realtimeSinceStartupAsDouble });
                Assert.That(fps, Is.EqualTo(5).Within(.1), "Ten whole-frame deliveries / two actual seconds; never eighty people or native zero.");
            });
        }

        [Test] public void CanvasDetachAndReenableResetTheBaselineWithoutDuplicatingSubscriptions()
        {
            WithCanvas((canvas, manager, session, source, status) => {
                var eventField = typeof(HumanVisionManager).GetField("ResultUpdated", BindingFlags.Instance | BindingFlags.NonPublic);
                Assert.That(((Delegate)eventField.GetValue(manager)).GetInvocationList().Length, Is.EqualTo(1));
                var oldManagerCallback = (Action<long>)eventField.GetValue(manager);
                canvas.enabled = false; Invoke(canvas, "OnDisable"); Assert.That(eventField.GetValue(manager), Is.Null);
                session.Sequence = 90; canvas.enabled = true; Invoke(canvas, "OnEnable");
                Assert.That(((Delegate)eventField.GetValue(manager)).GetInvocationList().Length, Is.EqualTo(1));
                var rate = canvas.GetType().GetField("observationRate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(canvas);
                Assert.That((double)rate.GetType().GetMethod("Read").Invoke(rate, new object[] { Time.realtimeSinceStartupAsDouble + 1 }), Is.Zero);
                manager.Shutdown(); Set(canvas, "nextStatus", 0f); Invoke(canvas, "Update");
                Assert.That(status.text, Does.Contain("Recognition unavailable; preview remains independent."));
                var replacement = new GameObject("replacement manager"); replacement.transform.SetParent(canvas.transform);
                var newManager = replacement.AddComponent<HumanVisionManager>(); var newSession = new Session { Sequence = 90 };
                Set(newManager, "_session", newSession);
                Set(canvas.GetComponent<HumanVisionDemoNavigator>(), "<Manager>k__BackingField", newManager);
                Set(canvas, "nextStatus", 0f); Invoke(canvas, "Update"); // Baseline the existing replacement snapshot.
                newSession.Sequence = 99; oldManagerCallback(99);
                Assert.That((long)rate.GetType().GetProperty("LastSequence").GetValue(rate), Is.EqualTo(90),
                    "A held callback from the old manager must not claim the replacement's numerically equal result sequence.");
                Invoke(newManager, "Update");
                Assert.That((long)rate.GetType().GetProperty("LastSequence").GetValue(rate), Is.EqualTo(99),
                    "The actual replacement manager delivery is accepted.");
            });
        }

        [Test] public void NewSourceGenerationAndStoppedSourceClearTheOldCanvasRate()
        {
            WithCanvas((canvas, manager, session, source, status) => {
                session.Sequence = 1; Invoke(manager, "Update");
                Invoke(canvas, "Update");
                var rate = canvas.GetType().GetField("observationRate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(canvas);
                Assert.That((double)rate.GetType().GetMethod("Read").Invoke(rate, new object[] { Time.realtimeSinceStartupAsDouble }), Is.EqualTo(.5).Within(.1));
                source.Generation++; Set(canvas, "nextStatus", 0f); Invoke(canvas, "Update");
                Assert.That(status.text, Does.Contain("Manager raw result delivery FPS: 0.00"));
                source.State = InputSourceState.Stopped; Set(canvas, "nextStatus", 0f); Invoke(canvas, "Update");
                Assert.That(status.text, Does.Contain("Manager raw result delivery FPS: 0.00"));
            });
        }

        [TestCase(false)]
        [TestCase(true)]
        public void CanvasCountsFirstKnownDeliveryBeforeUpdateAtSourceTransition(bool openingToStreaming)
        {
            WithCanvas((canvas, manager, session, source, status) => {
                if (openingToStreaming) {
                    source.State = InputSourceState.Opening; Invoke(canvas, "Update");
                    source.State = InputSourceState.Streaming;
                } else source.Generation++;
                // Manager runs before canvas.Update in production. These two known
                // notifications must both survive the event-first context reset.
                double begin = Time.realtimeSinceStartupAsDouble;
                session.Sequence = 1; Invoke(manager, "Update");
                System.Threading.Thread.Sleep(550);
                session.Sequence = 2; Invoke(manager, "Update");
                System.Threading.Thread.Sleep(550);
                double end = Time.realtimeSinceStartupAsDouble;
                var rate = canvas.GetType().GetField("observationRate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(canvas);
                double fps = (double)rate.GetType().GetMethod("Read").Invoke(rate, new object[] { end });
                Assert.That(fps, Is.EqualTo(2 / (end - begin)).Within(.01), "Both actual new body events count once; the first is not an existing snapshot baseline.");

                source.Generation++; Invoke(manager, "Update"); // Same sequence: hand-only / duplicate.
                Assert.That((double)rate.GetType().GetMethod("Read").Invoke(rate, new object[] { Time.realtimeSinceStartupAsDouble }), Is.Zero,
                    "A same-sequence notification at a new epoch must clear the old rate without counting a delivery.");
                var callback = (Action<long>)typeof(HumanVisionManager).GetField("ResultUpdated", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(manager);
                callback(99); // Stale notification does not describe manager's actual body sequence 2.
                Assert.That((long)rate.GetType().GetProperty("LastSequence").GetValue(rate), Is.EqualTo(2),
                    "A notification inconsistent with the current manager snapshot cannot invent a new body delivery.");
            });
        }

        private static void WithCanvas(Action<HumanVisionUnifiedDemoCanvas, HumanVisionManager, Session, Source, UnityEngine.UI.Text> test)
        {
            var go = new GameObject("delivery diagnostic test");
            var textObject = new GameObject("diagnostic text", typeof(RectTransform), typeof(UnityEngine.UI.Text), typeof(UnityEngine.UI.LayoutElement));
            try {
                var manager = go.AddComponent<HumanVisionManager>(); var session = new Session(); Set(manager, "_session", session);
                var input = go.AddComponent<InputPreviewControls>(); var source = new Source(); Set(input, "<Source>k__BackingField", source);
                var bridge = go.AddComponent<VideoPlayerFrameSource>();
                var navigator = go.AddComponent<HumanVisionDemoNavigator>(); navigator.enabled = false;
                Set(navigator, "<Manager>k__BackingField", manager); Set(navigator, "<Input>k__BackingField", input); Set(navigator, "<Bridge>k__BackingField", bridge);
                var canvas = go.AddComponent<HumanVisionUnifiedDemoCanvas>(); var status = textObject.GetComponent<UnityEngine.UI.Text>();
                Set(canvas, "navigator", navigator); Set(canvas, "recognitionStatus", status); Set(canvas, "compactStatus", status);
                canvas.GetType().GetMethod("OnEnable", BindingFlags.Instance | BindingFlags.NonPublic)?.Invoke(canvas, null);
                var rateField = canvas.GetType().GetField("observationRate", BindingFlags.Instance | BindingFlags.NonPublic);
                if (rateField != null) { var rate = rateField.GetValue(canvas); rate.GetType().GetMethod("Reset").Invoke(rate, new object[] { Time.realtimeSinceStartupAsDouble - 2, 0L }); }
                test(canvas, manager, session, source, status);
            } finally {
                // Start is deliberately disabled: no models, GPU setup or scene IO.
                // Its region facade was never bound, so retire the test bridge here.
                var navigator = go.GetComponent<HumanVisionDemoNavigator>();
                if (navigator != null) Set(navigator, "<Bridge>k__BackingField", null);
                UnityEngine.Object.DestroyImmediate(go); UnityEngine.Object.DestroyImmediate(textObject);
            }
        }
    }
}
