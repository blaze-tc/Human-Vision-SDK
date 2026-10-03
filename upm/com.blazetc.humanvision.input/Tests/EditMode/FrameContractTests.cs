using System;
using System.Linq;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Input.Tests
{
    public sealed class FrameContractTests
    {
        private sealed class CopyFence : ISourceCopyFence
        {
            public bool IsComplete { get; set; }
        }

        [Test]
        public void CopyLeaseReuseWaitsForExactRetirementAcknowledgment()
        {
            var property = typeof(SourceCopyLease).GetProperty("IsRetired");
            Assert.That(property, Is.Not.Null, "Consumer cannot safely reuse a fence while its old lease is still retained.");
            var retirement = new SourceRetirement(2, 1);
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            try {
                var token = retirement.Register(generation, texture, () => { });
                var frame = Frame(source, generation, 1, texture, token);
                Assert.That(source.TryPublish(in frame), Is.True);
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out var lease), Is.True);
                var duplicate = lease;
                var fence = new CopyFence();
                lease.RetireAfter(fence);
                Assert.That(property.GetValue(lease), Is.False);
                fence.IsComplete = true;
                Assert.That(property.GetValue(lease), Is.False, "Native completion alone is not lease consumption.");
                retirement.Poll();
                Assert.That(property.GetValue(duplicate), Is.True);
                retirement.Poll();
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out var replacement), Is.True);
                Assert.That(property.GetValue(replacement), Is.False);
                Assert.That(property.GetValue(lease), Is.True, "An old copied lease must not alias the reused slot.");
                replacement.Dispose();
                Assert.That(property.GetValue(replacement), Is.True);
                source.Close(); source.BeginGeneration();
                Assert.That(property.GetValue(lease), Is.True);
            } finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void AndroidInitializationFailureRemainsActionable()
        {
            if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Vulkan) Assert.Ignore("This negative capability case uses the qualified D3D host.");
            var gpu = new AndroidRtspGpuSource();
            Assert.Throws<InvalidOperationException>(() => gpu.Open(new RtspSourceSettings { Location = "rtsp://127.0.0.1:1/test" }, 500, 250));
            gpu.Tick();
            Assert.That(gpu.State, Is.EqualTo(InputSourceState.Error));
            Assert.That(gpu.LastError, Does.Contain("Vulkan"));
            Assert.That(gpu.Pending, Is.False);
        }

        [Test]
        public void OldGenerationCannotPublish()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var oldGeneration = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            var replacementTexture = new Texture2D(8, 4);
            try
            {
                var oldToken = retirement.Register(oldGeneration, texture, () => { });
                var oldFrame = Frame(source, oldGeneration, 1, texture, oldToken);
                Assert.That(source.TryPublish(in oldFrame), Is.True);
                var newGeneration = source.BeginGeneration();
                Assert.That(newGeneration, Is.GreaterThan(oldGeneration));
                Assert.That(source.TryPublish(in oldFrame), Is.False);
                Assert.That(source.TryGetLatestFrame(-1, out _), Is.False);
                var token = retirement.Register(newGeneration, replacementTexture, () => { });
                var newFrame = Frame(source, newGeneration, 2, replacementTexture, token);
                Assert.That(source.TryPublish(in newFrame), Is.True);
                Assert.That(source.TryGetLatestFrame(1, out var latest), Is.True);
                Assert.That(latest.SourceId, Is.EqualTo(oldFrame.SourceId));
                Assert.That(latest.Generation, Is.EqualTo(newGeneration));
                Assert.That(source.TryPublish(in newFrame), Is.False, "Repeated frame IDs must not publish");
                source.Close();
                Assert.That(source.TryPublish(in newFrame), Is.False);
                Assert.That(source.TryGetLatestFrame(-1, out _), Is.False);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(texture);
                UnityEngine.Object.DestroyImmediate(replacementTexture);
            }
        }

        [Test]
        public void TextureCannotHaveTwoIndependentDestroyOwners()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            try
            {
                retirement.Register(generation, texture, () => { });
                var nextGeneration = source.BeginGeneration();
                Assert.Throws<InvalidOperationException>(() => retirement.Register(nextGeneration, texture, () => { }));
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void ClosingWaitsForCopyNotInference()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            var destroyed = false;
            try
            {
                var token = retirement.Register(generation, texture, () => destroyed = true);
                var frame = Frame(source, generation, 1, texture, token);
                Assert.That(source.TryPublish(in frame), Is.True);
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out var lease), Is.True);
                var copy = new CopyFence();
                var inference = new CopyFence(); // A separate consumer-owned operation, never given to the source.
                lease.RetireAfter(copy);
                source.Close();
                retirement.Poll();
                Assert.That(destroyed, Is.False);
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out _), Is.False);
                copy.IsComplete = true;
                retirement.Poll();
                Assert.That(destroyed, Is.True);
                Assert.That(inference.IsComplete, Is.False);
                Assert.That(retirement.PendingResourceCount, Is.Zero);
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void FrameMetadataUsesActualGeometry()
        {
            var settings = new HumanVisionSourceSettings { RequestedWidth = 1920, RequestedHeight = 1080 };
            var texture = new Texture2D(1280, 720);
            try
            {
                var frame = new HumanVisionTextureFrame(7, 2, 30, texture, 123456, 4567,
                    180, true, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb,
                    FrameTimestampKind.UnityObserved, 9);
                Assert.That(frame.Width, Is.EqualTo(texture.width));
                Assert.That(frame.Height, Is.EqualTo(texture.height));
                Assert.That(frame.Width, Is.Not.EqualTo(settings.RequestedWidth));
                Assert.That(frame.Height, Is.Not.EqualTo(settings.RequestedHeight));
                Assert.That(frame.AppliedRotationDegrees, Is.EqualTo(180));
                Assert.That(frame.AppliedMirrorX, Is.True);
                Assert.That(frame.RowOrigin, Is.EqualTo(FrameRowOrigin.UnityBottomLeft));
                Assert.That(frame.TimestampKind, Is.EqualTo(FrameTimestampKind.UnityObserved));
                Assert.That(frame.PublishedTimestampUs, Is.EqualTo(123456));
                Assert.That(frame.PresentationTimestampUs, Is.EqualTo(4567));
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void InputAssemblyHasNoInferenceReferences()
        {
            var references = typeof(IHumanVisionFrameSource).Assembly.GetReferencedAssemblies();
            foreach (var reference in references)
                Assert.That(reference.Name.ToLowerInvariant(), Does.Not.Contain("onnx").And.Not.Contain("ncnn").And.Not.Contain("humanvision.runtime"));
            Assert.That(typeof(IHumanVisionFrameSource).GetMethod("Open").GetParameters().Single().ParameterType,
                Is.EqualTo(typeof(HumanVisionSourceSettings)));
            Assert.That(typeof(IHumanVisionFrameSource).GetMethod("Close").GetParameters(), Is.Empty);
            Assert.That(typeof(IHumanVisionFrameSource).GetProperty("CurrentTexture").PropertyType, Is.EqualTo(typeof(Texture)));
        }

        [Test]
        public void EveryOutstandingCopyMustCompleteBeforeResourceRetires()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            var destroyed = 0;
            try
            {
                var token = retirement.Register(generation, texture, () => destroyed++);
                var frame = Frame(source, generation, 1, texture, token);
                Assert.That(source.TryPublish(in frame), Is.True);
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out var first), Is.True);
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out var second), Is.True);
                Assert.That(retirement.HasPendingCopies(texture), Is.True, "A queued or unqueued consumer lease prevents native output reuse");
                var fence = new CopyFence { IsComplete = true };
                first.RetireAfter(fence);
                Assert.Throws<InvalidOperationException>(() => first.RetireAfter(fence));
                source.Close();
                retirement.Poll();
                Assert.That(destroyed, Is.Zero, "Acquired but not yet queued copies also hold the source");
                second.RetireAfter(fence);
                retirement.Poll();
                retirement.Poll();
                Assert.That(destroyed, Is.EqualTo(1));
                Assert.That(retirement.HasPendingCopies(texture), Is.False);
                Assert.Throws<InvalidOperationException>(() => second.RetireAfter(fence));
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void ForeignOrRetiredResourceCannotAcquireLease()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            var otherTexture = new Texture2D(8, 4);
            try
            {
                var token = retirement.Register(generation, texture, () => { });
                var wrongTexture = Frame(source, generation, 1, otherTexture, token);
                Assert.That(source.TryPublish(in wrongTexture), Is.False);
                var frame = Frame(source, generation, 1, texture, token);
                frame.SourceId++;
                Assert.That(source.TryPublish(in frame), Is.False);
                frame.SourceId = source.SourceId;
                Assert.That(source.TryPublish(in frame), Is.True);
                retirement.RetireGeneration(generation);
                Assert.That(source.TryAcquireSourceCopyLease(in frame, out _), Is.False);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(texture);
                UnityEngine.Object.DestroyImmediate(otherTexture);
            }
        }

        [Test]
        public void PublicationTimelineRejectsInvalidValuesAndPreservesLatest()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            try
            {
                var token = retirement.Register(generation, texture, () => { });
                var first = Frame(source, generation, 1, texture, token);
                Assert.That(source.TryPublish(in first), Is.True);
                var invalid = Frame(source, generation, 2, texture, token);
                invalid.SourceTimestampUs = 0;
                invalid.PublishedTimestampUs = -1;
                Assert.That(source.TryPublish(in invalid), Is.False, "Negative publication time must be rejected");
                invalid.PublishedTimestampUs = 999;
                Assert.That(source.TryPublish(in invalid), Is.False, "Regressing publication time must be rejected");
                Assert.That(source.TryGetLatestFrame(-1, out var latest), Is.True);
                Assert.That(latest.FrameId, Is.EqualTo(first.FrameId));
                invalid.PublishedTimestampUs = 1000;
                invalid.PresentationTimestampUs = -100;
                Assert.That(source.TryPublish(in invalid), Is.True, "Equal microseconds and independent PTS resets are allowed");
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void PublicationTimelineContinuesAcrossGenerations()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var texture = new Texture2D(8, 4);
            var replacement = new Texture2D(8, 4);
            try
            {
                var generation = source.BeginGeneration();
                var token = retirement.Register(generation, texture, () => { });
                var frame = Frame(source, generation, 1, texture, token);
                Assert.That(source.TryPublish(in frame), Is.True);
                source.Close();
                generation = source.BeginGeneration();
                token = retirement.Register(generation, replacement, () => { });
                frame = Frame(source, generation, 2, replacement, token);
                frame.SourceTimestampUs = 0;
                frame.PublishedTimestampUs = -1;
                Assert.That(source.TryPublish(in frame), Is.False);
                frame.PublishedTimestampUs = 999;
                Assert.That(source.TryPublish(in frame), Is.False, "Reopening does not reset the publication clock");
                Assert.That(source.TryGetLatestFrame(-1, out _), Is.False);
                frame.PublishedTimestampUs = 1000;
                frame.PresentationTimestampUs = 0;
                Assert.That(source.TryPublish(in frame), Is.True);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(texture);
                UnityEngine.Object.DestroyImmediate(replacement);
            }
        }

        [Test]
        public void TimestampContractDeclaresIndependentClockAndSourceObservation()
        {
            Assert.That(InputMonotonicClock.Domain, Is.EqualTo(FrameClockDomain.InputMonotonic));
            Assert.That(InputMonotonicClock.OriginStopwatchTicks, Is.GreaterThanOrEqualTo(0));
            Assert.That(InputMonotonicClock.StopwatchFrequency, Is.GreaterThan(0));
            var before = InputMonotonicClock.NowUs;
            var texture = new Texture2D(8, 4);
            try
            {
                foreach (var kind in new[] { FrameTimestampKind.UnityObserved, FrameTimestampKind.LocalDecode })
                {
                    var publication = InputMonotonicClock.NowUs;
                    var frame = new HumanVisionTextureFrame(1, 1, 1, texture, publication, 900000000,
                        0, false, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb, kind, 1,
                        before, FrameClockDomain.InputMonotonic, 0);
                    Assert.That(frame.PublishedClockDomain, Is.EqualTo(InputMonotonicClock.Domain));
                    Assert.That(frame.SourceTimestampUs, Is.EqualTo(before));
                    Assert.That(frame.SourceClockDomain, Is.EqualTo(InputMonotonicClock.Domain));
                    Assert.That(frame.SourceClockId, Is.Zero);
                    Assert.That(frame.TimestampKind, Is.EqualTo(kind));
                    Assert.That(frame.PublishedTimestampUs, Is.InRange(before, InputMonotonicClock.NowUs));
                    Assert.That(frame.PresentationTimestampUs, Is.EqualTo(900000000), "PTS is never compared to local clocks");
                }
                Assert.Throws<ArgumentException>(() => new HumanVisionTextureFrame(1, 1, 1, texture, before, -1,
                    0, false, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb, FrameTimestampKind.LocalDecode, 1));
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        [Test]
        public void SourceObservationValidityRespectsClockDomain()
        {
            var retirement = new SourceRetirement();
            var source = new SourceGeneration(retirement);
            var generation = source.BeginGeneration();
            var texture = new Texture2D(8, 4);
            try
            {
                var token = retirement.Register(generation, texture, () => { });
                var frame = Frame(source, generation, 1, texture, token);
                Assert.That(source.TryPublish(in frame), Is.True);
                frame.FrameId = 2;
                frame.SourceTimestampUs = -1;
                Assert.That(source.TryPublish(in frame), Is.False);
                frame.SourceTimestampUs = 1001;
                Assert.That(source.TryPublish(in frame), Is.False, "Same-clock observation cannot follow publication");
                frame.SourceClockDomain = FrameClockDomain.Unspecified;
                frame.SourceTimestampUs = 10;
                Assert.That(source.TryPublish(in frame), Is.False);
                frame.SourceClockDomain = FrameClockDomain.SourceLocalMonotonic;
                frame.SourceClockId = 7;
                Assert.That(source.TryPublish(in frame), Is.False, "Unity observation uses the input clock");
                frame.TimestampKind = FrameTimestampKind.LocalDecode;
                frame.SourceClockId = 0;
                Assert.That(source.TryPublish(in frame), Is.False, "Foreign clocks need explicit identity");
                frame.SourceClockId = 7;
                frame.SourceTimestampUs = 900000000;
                Assert.That(source.TryPublish(in frame), Is.True, "Unmapped decode clock cannot be ordered against publication");
                Assert.That(source.TryGetLatestFrame(-1, out var latest), Is.True);
                frame.FrameId = 3;
                frame.SourceClockDomain = FrameClockDomain.InputMonotonic;
                frame.SourceClockId = 0;
                Assert.That(source.TryPublish(in frame), Is.False);
                Assert.That(source.TryGetLatestFrame(-1, out var preserved), Is.True);
                Assert.That(preserved.FrameId, Is.EqualTo(latest.FrameId));
            }
            finally { UnityEngine.Object.DestroyImmediate(texture); }
        }

        private static HumanVisionTextureFrame Frame(SourceGeneration source, ulong generation, long id, Texture texture, ulong token)
        {
            return new HumanVisionTextureFrame(source.SourceId, generation, id, texture, id * 1000, -1,
                0, false, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb, FrameTimestampKind.UnityObserved, token);
        }
    }
}
