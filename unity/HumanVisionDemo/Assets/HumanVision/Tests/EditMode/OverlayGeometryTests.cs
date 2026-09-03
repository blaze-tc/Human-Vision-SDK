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
        public void DisplaySkeletonUsesNeckAndPelvisInsteadOfTriangularTorso()
        {
            Assert.That(Coco17Skeleton.Bones.Length, Is.EqualTo(18));
            Assert.That(ContainsBone(0, 17), Is.True, "nose should connect to the derived neck");
            Assert.That(ContainsBone(17, 5), Is.True);
            Assert.That(ContainsBone(17, 6), Is.True);
            Assert.That(ContainsBone(17, 18), Is.True, "the torso should have a central spine");
            Assert.That(ContainsBone(18, 11), Is.True);
            Assert.That(ContainsBone(18, 12), Is.True);

            Assert.That(ContainsBone(0, 5), Is.False, "nose-to-shoulder diagonals create a large triangle");
            Assert.That(ContainsBone(0, 6), Is.False, "nose-to-shoulder diagonals create a large triangle");
            Assert.That(ContainsBone(5, 11), Is.False, "shoulder-to-hip diagonals obscure the spine");
            Assert.That(ContainsBone(6, 12), Is.False, "shoulder-to-hip diagonals obscure the spine");
        }

        [Test]
        public void DerivedAnchorsUseShoulderAndHipMidpoints()
        {
            var joints = new HumanVisionJoint[HumanVisionJoint.Count];
            joints[5] = Joint(10f, 20f);
            joints[6] = Joint(30f, 40f);
            joints[11] = Joint(50f, 60f);
            joints[12] = Joint(90f, 100f);

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 17, out Vector2 neck), Is.True);
            Assert.That(neck, Is.EqualTo(new Vector2(20f, 30f)));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 18, out Vector2 pelvis), Is.True);
            Assert.That(pelvis, Is.EqualTo(new Vector2(70f, 80f)));
        }

        [Test]
        public void Coco17DisplaySkeletonContainsOnlyValidAnchorIndices()
        {
            Assert.That(Coco17Skeleton.Bones.Length, Is.EqualTo(18));
            foreach (Coco17Bone bone in Coco17Skeleton.Bones)
            {
                Assert.That(bone.Start, Is.InRange(0, 18));
                Assert.That(bone.End, Is.InRange(0, 18));
                Assert.That(bone.Start, Is.Not.EqualTo(bone.End));
            }
        }

        private static bool ContainsBone(int first, int second)
        {
            foreach (Coco17Bone bone in Coco17Skeleton.Bones)
            {
                if ((bone.Start == first && bone.End == second) ||
                    (bone.Start == second && bone.End == first))
                {
                    return true;
                }
            }

            return false;
        }

        private static HumanVisionJoint Joint(float x, float y)
        {
            return new HumanVisionJoint(
                new Vector2(x, y),
                Vector2.zero,
                1f,
                true);
        }
    }
}
