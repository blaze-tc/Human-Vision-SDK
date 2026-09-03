using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class OverlayGeometryTests
    {
        [Test]
        public void AspectFitUsesLetterboxedVideoArea()
        {
            Rect fitted = OverlayGeometry.CalculateAspectFitRect(new Rect(0f, 0f, 1920f, 1080f), 640, 480);

            Assert.That(fitted.x, Is.EqualTo(240f).Within(0.001f));
            Assert.That(fitted.y, Is.EqualTo(0f).Within(0.001f));
            Assert.That(fitted.width, Is.EqualTo(1440f).Within(0.001f));
            Assert.That(fitted.height, Is.EqualTo(1080f).Within(0.001f));
        }

        [Test]
        public void SourceCoordinatesUseTopLeftOriginAndMapIntoVideoArea()
        {
            var fitted = new Rect(240f, 0f, 1440f, 1080f);

            Vector2 topLeft = OverlayGeometry.SourceToOverlay(new Vector2(0f, 0f), fitted, 640, 480);
            Vector2 bottomRight = OverlayGeometry.SourceToOverlay(new Vector2(640f, 480f), fitted, 640, 480);

            Assert.That(topLeft, Is.EqualTo(new Vector2(240f, 1080f)));
            Assert.That(bottomRight, Is.EqualTo(new Vector2(1680f, 0f)));
        }

        [Test]
        public void Coco17SkeletonContainsOnlyValidSchemaIndices()
        {
            Assert.That(Coco17Skeleton.Bones.Length, Is.EqualTo(18));
            foreach (Coco17Bone bone in Coco17Skeleton.Bones)
            {
                Assert.That(bone.Start, Is.InRange(0, HumanVisionJoint.Count - 1));
                Assert.That(bone.End, Is.InRange(0, HumanVisionJoint.Count - 1));
                Assert.That(bone.Start, Is.Not.EqualTo(bone.End));
            }
        }
    }
}
