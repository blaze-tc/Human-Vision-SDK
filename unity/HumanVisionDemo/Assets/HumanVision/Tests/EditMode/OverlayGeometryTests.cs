using HumanVision.Demo;
using NUnit.Framework;
using Unity.Collections;
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
        public void BottomUpGpuReadbackIsCopiedIntoTopLeftRowOrder()
        {
            var source = new NativeArray<byte>(
                new byte[] { 10, 11, 20, 21, 30, 31 },
                Allocator.Temp);
            var destination = new NativeArray<byte>(
                source.Length,
                Allocator.Temp,
                NativeArrayOptions.UninitializedMemory);

            try
            {
                ReadbackRowNormalizer.CopyBottomUpToTopDown(source, destination, 3, 2);

                CollectionAssert.AreEqual(
                    new byte[] { 30, 31, 20, 21, 10, 11 },
                    destination.ToArray());
            }
            finally
            {
                destination.Dispose();
                source.Dispose();
            }
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
        public void DisplaySkeletonUsesKinectStyleSpineShoulderAndLimbHierarchy()
        {
            const int pelvis = 17;
            const int spineNavel = 18;
            const int spineChest = 19;
            const int neck = 20;
            const int head = 21;
            const int clavicleLeft = 22;
            const int clavicleRight = 23;

            Assert.That(Coco17Skeleton.Bones.Length, Is.EqualTo(23));
            Assert.That(ContainsBone(pelvis, spineNavel), Is.True);
            Assert.That(ContainsBone(spineNavel, spineChest), Is.True);
            Assert.That(ContainsBone(spineChest, neck), Is.True);
            Assert.That(ContainsBone(neck, head), Is.True);
            Assert.That(ContainsBone(head, 0), Is.True, "the head should connect the neck to the COCO nose");

            Assert.That(ContainsBone(spineChest, clavicleLeft), Is.True);
            Assert.That(ContainsBone(clavicleLeft, 5), Is.True);
            Assert.That(ContainsBone(5, 7), Is.True);
            Assert.That(ContainsBone(7, 9), Is.True);
            Assert.That(ContainsBone(spineChest, clavicleRight), Is.True);
            Assert.That(ContainsBone(clavicleRight, 6), Is.True);
            Assert.That(ContainsBone(6, 8), Is.True);
            Assert.That(ContainsBone(8, 10), Is.True);

            Assert.That(ContainsBone(pelvis, 11), Is.True);
            Assert.That(ContainsBone(11, 13), Is.True);
            Assert.That(ContainsBone(13, 15), Is.True);
            Assert.That(ContainsBone(pelvis, 12), Is.True);
            Assert.That(ContainsBone(12, 14), Is.True);
            Assert.That(ContainsBone(14, 16), Is.True);

            Assert.That(ContainsBone(0, 5), Is.False, "nose-to-shoulder diagonals create a large triangle");
            Assert.That(ContainsBone(0, 6), Is.False, "nose-to-shoulder diagonals create a large triangle");
            Assert.That(ContainsBone(neck, 5), Is.False, "Kinect shoulders should branch through clavicles");
            Assert.That(ContainsBone(neck, 6), Is.False, "Kinect shoulders should branch through clavicles");
            Assert.That(ContainsBone(5, 11), Is.False, "shoulder-to-hip diagonals obscure the spine");
            Assert.That(ContainsBone(6, 12), Is.False, "shoulder-to-hip diagonals obscure the spine");
        }

        [Test]
        public void DerivedAnchorsFormKinectStyleSpineAndShoulderBelt()
        {
            var joints = new HumanVisionJoint[HumanVisionJoint.Count];
            joints[0] = Joint(50f, 0f);
            joints[5] = Joint(20f, 20f);
            joints[6] = Joint(80f, 20f);
            joints[11] = Joint(30f, 100f);
            joints[12] = Joint(70f, 100f);

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 17, out Vector2 pelvis), Is.True);
            Assert.That(pelvis, Is.EqualTo(new Vector2(50f, 100f)));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 18, out Vector2 spineNavel), Is.True);
            Assert.That(spineNavel.x, Is.EqualTo(50f).Within(0.001f));
            Assert.That(spineNavel.y, Is.EqualTo(72f).Within(0.001f));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 19, out Vector2 spineChest), Is.True);
            Assert.That(spineChest.x, Is.EqualTo(50f).Within(0.001f));
            Assert.That(spineChest.y, Is.EqualTo(40f).Within(0.001f));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 20, out Vector2 neck), Is.True);
            Assert.That(neck, Is.EqualTo(new Vector2(50f, 20f)));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 21, out Vector2 head), Is.True);
            Assert.That(head, Is.EqualTo(new Vector2(50f, 10f)));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 22, out Vector2 clavicleLeft), Is.True);
            Assert.That(clavicleLeft, Is.EqualTo(new Vector2(35f, 30f)));

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 23, out Vector2 clavicleRight), Is.True);
            Assert.That(clavicleRight, Is.EqualTo(new Vector2(65f, 30f)));
        }

        [Test]
        public void MissingShoulderSuppressesOnlyAnchorsThatDependOnShoulderPair()
        {
            var joints = new HumanVisionJoint[HumanVisionJoint.Count];
            joints[0] = Joint(50f, 0f);
            joints[5] = Joint(20f, 20f);
            joints[11] = Joint(30f, 100f);
            joints[12] = Joint(70f, 100f);

            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 17, out Vector2 pelvis), Is.True);
            Assert.That(pelvis, Is.EqualTo(new Vector2(50f, 100f)));
            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 18, out _), Is.False);
            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 19, out _), Is.False);
            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 20, out _), Is.False);
            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 21, out _), Is.False);
            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 22, out _), Is.False);
            Assert.That(Coco17Skeleton.TryResolveAnchor(joints, 23, out _), Is.False);
        }

        [Test]
        public void Coco17DisplaySkeletonContainsOnlyValidAnchorIndices()
        {
            Assert.That(Coco17Skeleton.Bones.Length, Is.EqualTo(23));
            foreach (Coco17Bone bone in Coco17Skeleton.Bones)
            {
                Assert.That(bone.Start, Is.InRange(0, 23));
                Assert.That(bone.End, Is.InRange(0, 23));
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
