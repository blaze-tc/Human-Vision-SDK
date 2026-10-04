using System;
using System.Reflection;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSkeletonPlaneTests
    {
        private static object Call(string method, params object[] arguments)
        {
            var type = typeof(OverlayGeometry).Assembly.GetType("HumanVision.Demo.SkeletonImagePlane");
            Assert.That(type, Is.Not.Null, "The production object renderer has no canonical image-plane mapping.");
            var member = type.GetMethod(method, BindingFlags.Static | BindingFlags.NonPublic);
            Assert.That(member, Is.Not.Null, "Missing production image-plane operation " + method);
            return member.Invoke(null, arguments);
        }

        [Test]
        public void FittedOffsetPreviewMapsTopLeftAndBottomRightWithoutAnotherMirror()
        {
            var bottomLeft = new Vector2(160, 90);
            var topLeft = new Vector2(160, 630);
            var bottomRight = new Vector2(1120, 90);
            Assert.That((Vector2)Call("NormalizedToScreen", Vector2.zero, bottomLeft, topLeft, bottomRight), Is.EqualTo(new Vector2(160, 630)));
            Assert.That((Vector2)Call("NormalizedToScreen", Vector2.one, bottomLeft, topLeft, bottomRight), Is.EqualTo(new Vector2(1120, 90)));
            Assert.That((Vector2)Call("NormalizedToScreen", new Vector2(.25f, .75f), bottomLeft, topLeft, bottomRight), Is.EqualTo(new Vector2(400, 225)));
        }

        [Test]
        public void RotatedActualPreviewUsesBothTransformedAxes()
        {
            Assert.That((Vector2)Call("NormalizedToScreen", new Vector2(.25f, .75f),
                new Vector2(100, 100), new Vector2(40, 180), new Vector2(260, 220)), Is.EqualTo(new Vector2(125, 150)));
        }

        [Test]
        public void ReferenceThicknessTracksCanvasScaleAndCameraUnits()
        {
            // 9 reference pixels in 1280x720, doubled screen resolution, and half resolution.
            Assert.That((float)Call("ReferencePixelsToWorld", 9f, 1f, .01f), Is.EqualTo(.09f).Within(.00001f));
            Assert.That((float)Call("ReferencePixelsToWorld", 9f, 2f, .005f), Is.EqualTo(.09f).Within(.00001f));
            Assert.That((float)Call("ReferencePixelsToWorld", 27f, .5f, .02f), Is.EqualTo(.27f).Within(.00001f));
        }

        [Test]
        public void CanonicalGraphConnectsAllThirtyTwoRealSemanticSlots()
        {
            int[] parents = { -1,0,1,2,3,4,5,6,7,8,8,3,11,12,13,14,15,15,0,18,19,20,0,22,23,24,3,26,27,28,27,30 };
            var body = new HumanVisionBody();
            for (int joint = 0; joint < 32; joint++) {
                body.CanonicalJoints[joint] = new HumanVisionCanonicalJoint(new HumanVisionJoint(
                    Vector2.zero, new Vector2(joint / 32f, .2f), 1, true), 1, 0);
                Assert.That((int)Call("Parent", joint), Is.EqualTo(parents[joint]), "Canonical joint " + joint);
                object[] args = { body, joint, true, 1280, 720, Vector2.zero };
                Assert.That((bool)Call("TryPosition", args), Is.True);
                Assert.That((Vector2)args[5], Is.EqualTo(new Vector2(joint / 32f, .2f)));
            }
            body.CanonicalJoints[31] = default;
            object[] missing = { body, 31, true, 1280, 720, Vector2.zero };
            Assert.That((bool)Call("TryPosition", missing), Is.False, "Unavailable canonical joints must stay hidden.");
        }

        [Test]
        public void CompatibilityTorsoUsesExistingDerivedAnchorsAndDoesNotInventHandsOrFeet()
        {
            var body = new HumanVisionBody();
            body.Joints[5] = new HumanVisionJoint(new Vector2(200, 100), new Vector2(.2f,.2f), 1, true);
            body.Joints[6] = new HumanVisionJoint(new Vector2(800, 100), new Vector2(.8f,.2f), 1, true);
            body.Joints[11] = new HumanVisionJoint(new Vector2(300, 400), new Vector2(.3f,.8f), 1, true);
            body.Joints[12] = new HumanVisionJoint(new Vector2(700, 400), new Vector2(.7f,.8f), 1, true);
            object[] pelvis = { body, 0, false, 1000, 500, Vector2.zero };
            Assert.That((bool)Call("TryPosition", pelvis), Is.True);
            Assert.That((Vector2)pelvis[5], Is.EqualTo(new Vector2(.5f,.8f)));
            foreach (int index in new[] { 8,9,10,15,16,17,21,25 }) {
                object[] missing = { body, index, false, 1000, 500, Vector2.zero };
                Assert.That((bool)Call("TryPosition", missing), Is.False, "Absent semantic slot " + index);
            }
        }

        [Test]
        public void CompatibilityHandLandmarksRemainVisibleOnlyWhenActuallyAvailable()
        {
            var body = new HumanVisionBody();
            int[] canonicalIds = { 8,9,10,15,16,17 };
            for (int hand = 0; hand < 6; hand++) {
                body.HandJoints[hand] = new HumanVisionJoint(Vector2.zero, new Vector2(.1f * hand, .7f), 1, true);
                object[] present = { body, canonicalIds[hand], false, 1000, 500, Vector2.zero };
                Assert.That((bool)Call("TryPosition", present), Is.True, "Real V1 hand landmark " + hand);
                Assert.That((Vector2)present[5], Is.EqualTo(new Vector2(.1f * hand,.7f)));
            }
        }

        [Test]
        public void RegionsDisabledDisplaysEveryBodyEvenWithUnassignedRegionSentinel()
        {
            for (int body = 0; body < 8; body++) Assert.That((int)Call("DisplaySlot", body, -1, false), Is.EqualTo(body));
            Assert.That((int)Call("DisplaySlot", 4, 2, true), Is.EqualTo(2));
            Assert.That((int)Call("DisplaySlot", 4, -1, true), Is.EqualTo(-1), "Regions enabled must preserve unavailable assignment.");
        }

        [Test]
        public void CompatibilityCoordinatesUseActualNormalizedGeometryWhenAnalysisIsDownsampled()
        {
            var body = new HumanVisionBody();
            body.Joints[5] = new HumanVisionJoint(new Vector2(50, 20), new Vector2(.25f,.2f), 1, true);
            body.Joints[11] = new HumanVisionJoint(new Vector2(60, 80), new Vector2(.3f,.8f), 1, true);
            body.Joints[12] = new HumanVisionJoint(new Vector2(140, 80), new Vector2(.7f,.8f), 1, true);
            object[] shoulder = { body, 5, false, 4000, 2000, Vector2.zero };
            Assert.That((bool)Call("TryPosition", shoulder), Is.True);
            Assert.That((Vector2)shoulder[5], Is.EqualTo(new Vector2(.25f,.2f)));
            object[] pelvis = { body, 0, false, 4000, 2000, Vector2.zero };
            Assert.That((bool)Call("TryPosition", pelvis), Is.True);
            Assert.That((Vector2)pelvis[5], Is.EqualTo(new Vector2(.5f,.8f)));
        }
    }
}
