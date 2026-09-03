using System;
using HumanVision.Interop;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionResultBufferTests
    {
        [Test]
        public unsafe void CopyFromNativeMapsBodiesAndReusesManagedObjects()
        {
            using (var buffer = new HumanVisionResultBuffer(2))
            {
                HumanVisionBody[] originalBodies = buffer.Bodies;
                HumanVisionJoint[] originalJoints = buffer.Bodies[0].Joints;
                HVBodyNative* native = (HVBodyNative*)buffer.NativeBodies.ToPointer();
                native[0] = NativeBody(12, 10f, 20f, 30f, 40f, 0.91f);
                native[0].Joint0 = NativeJoint(11f, 22f, 0.11f, 0.22f, 0.8f, true);
                native[0].Joint16 = NativeJoint(31f, 42f, 0.31f, 0.42f, 0.7f, true);

                buffer.CopyFromNative(1, 7, 99, 123456);

                Assert.That(buffer.Bodies, Is.SameAs(originalBodies));
                Assert.That(buffer.Bodies[0].Joints, Is.SameAs(originalJoints));
                Assert.That(buffer.BodyCount, Is.EqualTo(1));
                Assert.That(buffer.ResultSequence, Is.EqualTo(7));
                Assert.That(buffer.SourceFrameId, Is.EqualTo(99));
                Assert.That(buffer.SourceTimestampUs, Is.EqualTo(123456));
                Assert.That(buffer.Bodies[0].TrackId, Is.EqualTo(12));
                Assert.That(buffer.Bodies[0].BoundingBoxPixels, Is.EqualTo(new Rect(10f, 20f, 30f, 40f)));
                Assert.That(buffer.Bodies[0].DetectionConfidence, Is.EqualTo(0.91f));
                Assert.That(buffer.Bodies[0].Joints[0].Pixel, Is.EqualTo(new Vector2(11f, 22f)));
                Assert.That(buffer.Bodies[0].Joints[0].Valid, Is.True);
                Assert.That(buffer.Bodies[0].Joints[16].Normalized, Is.EqualTo(new Vector2(0.31f, 0.42f)));
            }
        }

        [Test]
        public void CopyFromNativeRejectsCountBeyondCapacity()
        {
            using (var buffer = new HumanVisionResultBuffer(2))
            {
                Assert.Throws<ArgumentOutOfRangeException>(() => buffer.CopyFromNative(3, 1, 1, 1));
            }
        }

        [Test]
        public void ResizePreservesExistingManagedObjectsAndAddsCapacityOutsideHotPath()
        {
            using (var buffer = new HumanVisionResultBuffer(1))
            {
                HumanVisionBody first = buffer.Bodies[0];

                buffer.EnsureCapacity(4);

                Assert.That(buffer.Capacity, Is.EqualTo(4));
                Assert.That(buffer.Bodies[0], Is.SameAs(first));
                Assert.That(buffer.Bodies[3].Joints, Has.Length.EqualTo(HumanVisionJoint.Count));
            }
        }

        private static HVBodyNative NativeBody(
            int trackId,
            float x,
            float y,
            float width,
            float height,
            float confidence)
        {
            return new HVBodyNative
            {
                StructSize = NativeBindings.BodySize,
                TrackId = trackId,
                BoundingBox = new HVRectNative { X = x, Y = y, Width = width, Height = height },
                DetectionConfidence = confidence
            };
        }

        private static HVJointNative NativeJoint(
            float x,
            float y,
            float normalizedX,
            float normalizedY,
            float confidence,
            bool valid)
        {
            return new HVJointNative
            {
                X = x,
                Y = y,
                NormalizedX = normalizedX,
                NormalizedY = normalizedY,
                Confidence = confidence,
                Valid = valid ? (byte)1 : (byte)0
            };
        }
    }
}
