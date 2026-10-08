using System;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSdkQueryTests
    {
        private Type configType, queryType;
        private object config, query;
        [SetUp] public void Setup()
        {
            configType = typeof(HumanVisionManager).Assembly.GetType("HumanVision.HumanVisionSdkConfiguration");
            queryType = typeof(HumanVisionManager).Assembly.GetType("HumanVision.HumanVisionSkeletonQueries");
            Assert.NotNull(configType, "SDK configuration contract is missing.");
            Assert.NotNull(queryType, "Semantic index/region query API is missing.");
            config = Activator.CreateInstance(configType);
            configType.GetField("MaxBodies").SetValue(config, 4);
            query = Activator.CreateInstance(queryType, config);
        }
        private object Call(string name, params object[] args) => queryType.GetMethod(name).Invoke(query, args);
        private bool Body(int index, long now, out HumanVisionBody body)
        {
            object[] args = { index, now, null };
            bool found = (bool)Call("TryGetBody", args); body = args[2] as HumanVisionBody; return found;
        }
        private bool Joint(int index, HumanVisionCanonicalJointId joint, long now)
            => (bool)Call("TryGetJoint", index, joint, now, null);
        private HumanVisionBody Fixture(int slot, long id, long time = 1000000)
        {
            var body = (HumanVisionBody)Activator.CreateInstance(typeof(HumanVisionBody), true);
            typeof(HumanVisionBody).GetProperty("RegionIndex").SetValue(body, slot);
            typeof(HumanVisionBody).GetProperty("StableTrackId").SetValue(body, id);
            typeof(HumanVisionBody).GetProperty("ObservationTimestampUs").SetValue(body, time);
            return body;
        }
        private static void SetJoint(HumanVisionBody body, HumanVisionCanonicalJointId id,
            Vector2 normalized, float confidence = .9f, bool valid = true, long time = 1000000)
        {
            var position = (HumanVisionJoint)Activator.CreateInstance(typeof(HumanVisionJoint),
                BindingFlags.NonPublic | BindingFlags.Instance, null,
                new object[] { normalized * 100, normalized, confidence, valid, false }, null);
            body.CanonicalJoints[(int)id] = (HumanVisionCanonicalJoint)Activator.CreateInstance(
                typeof(HumanVisionCanonicalJoint), BindingFlags.NonPublic | BindingFlags.Instance, null,
                new object[] { position, time, 0f }, null);
        }
        private void Observe(params HumanVisionBody[] bodies) => Call("Observe", bodies, bodies.Length, 7L, 12L, 1000000L, true);
        [Test] public void SparseSlotsDoNotBecomeCompactIndices()
        {
            Observe(Fixture(3, 4294967311));
            Assert.False(Body(0, 1050000, out _));
            Assert.True(Body(3, 1050000, out var body));
            Assert.That(body.StableTrackId, Is.EqualTo(4294967311));
            Assert.That(Call("GetUsersCount", 1050000L), Is.EqualTo(1));
            Assert.That(Call("GetUserIndexById", 4294967311L, 1050000L), Is.EqualTo(3));
        }
        [TestCase(-1)] [TestCase(4)] public void InvalidIndicesFail(int index)
        { Observe(Fixture(0, 1)); Assert.False(Body(index, 1050000, out _)); }
        [Test] public void UnknownEmptyAndExpiredResultsHaveDifferentOccupancy()
        {
            object[] args = { 0, 1050000L, false };
            Assert.False((bool)Call("TryGetOccupancy", args));
            Observe(); Assert.True((bool)Call("TryGetOccupancy", args)); Assert.False((bool)args[2]);
            args[1] = 2500001L; Assert.False((bool)Call("TryGetOccupancy", args));
        }
        [Test] public void OldBodyTimestampCannotBeRevivedByNewPublication()
        { Observe(Fixture(0, 1, 1)); Assert.False(Body(0, 1050000, out _)); }
        [Test] public void ClearInvalidatesCurrentIdentityAndOccupancy()
        { Observe(Fixture(0, 1)); Call("Clear"); Assert.False(Body(0, 1050000, out _)); }
        [Test] public void IndependentHandAgeIsChecked()
        {
            var body = Fixture(0, 1);
            SetJoint(body, HumanVisionCanonicalJointId.WristLeft, new Vector2(.2f, .3f));
            SetJoint(body, HumanVisionCanonicalJointId.HandtipLeft, new Vector2(.2f, .3f), time: 750000);
            Observe(body);
            Assert.True(Joint(0, HumanVisionCanonicalJointId.WristLeft, 1050000));
            Assert.False(Joint(0, HumanVisionCanonicalJointId.HandtipLeft, 1050000));
        }
        [TestCase(.1f, true)] [TestCase(.9f, false)]
        public void LowConfidenceOrInvalidJointFails(float confidence, bool valid)
        {
            var body = Fixture(0, 1); SetJoint(body, HumanVisionCanonicalJointId.Head, Vector2.one * .5f, confidence, valid);
            Observe(body); Assert.False(Joint(0, HumanVisionCanonicalJointId.Head, 1050000));
        }
        [Test] public void NonfinitePositionAndUnknownJointFail()
        {
            var body = Fixture(0, 1); SetJoint(body, HumanVisionCanonicalJointId.Head, new Vector2(float.NaN, .5f));
            Observe(body); Assert.False(Joint(0, HumanVisionCanonicalJointId.Head, 1050000));
            Assert.False(Joint(0, (HumanVisionCanonicalJointId)32, 1050000));
        }
        [Test] public void CopySkeletonRetainsValuesAfterBorrowedBodyChanges()
        {
            var body = Fixture(0, 1); SetJoint(body, HumanVisionCanonicalJointId.Head, new Vector2(.2f, .3f)); Observe(body);
            var buffer = new HumanVisionCanonicalJoint[32];
            Assert.True((bool)Call("CopySkeleton", 0, 1050000L, buffer, null));
            SetJoint(body, HumanVisionCanonicalJointId.Head, Vector2.one);
            Assert.That(buffer[(int)HumanVisionCanonicalJointId.Head].Position.Normalized, Is.EqualTo(new Vector2(.2f, .3f)));
            Assert.False((bool)Call("CopySkeleton", 0, 1050000L, new HumanVisionCanonicalJoint[31], null));
        }
        [Test] public void ConfigurationCloneDoesNotShareRegionArrays()
        {
            var regions = new[] { new Rect(0, 0, 1, 1) };
            configType.GetField("MaxBodies").SetValue(config, 1);
            configType.GetField("Regions").SetValue(config, regions);
            var clone = configType.GetMethod("Clone").Invoke(config, null);
            regions[0] = new Rect(.2f, 0, .8f, 1);
            Assert.That(((Rect[])configType.GetField("Regions").GetValue(clone))[0].x, Is.Zero);
        }
        [TestCase(0)] [TestCase(9)] public void CapacityMustBeOneThroughEight(int count)
        {
            configType.GetField("MaxBodies").SetValue(config, count);
            Assert.Throws<TargetInvocationException>(() => configType.GetMethod("Validate").Invoke(config, null));
        }
        [Test] public void OverlappingAndNonfiniteRegionsAreRejected()
        {
            configType.GetField("MaxBodies").SetValue(config, 2); configType.GetField("UseRegions").SetValue(config, true);
            foreach (var regions in new[] {
                new[] { new Rect(0, 0, .6f, 1), new Rect(.5f, 0, .5f, 1) },
                new[] { new Rect(float.NaN, 0, .5f, 1), new Rect(.5f, 0, .5f, 1) } })
            {
                configType.GetField("Regions").SetValue(config, regions);
                Assert.Throws<TargetInvocationException>(() => configType.GetMethod("Validate").Invoke(config, null));
            }
        }
        [Test] public void ScreenPositionUsesBottomLeftAndActualImageRect()
        {
            var body = Fixture(0, 1); SetJoint(body, HumanVisionCanonicalJointId.Head, new Vector2(.25f, .75f)); Observe(body);
            object[] args = { 0, HumanVisionCanonicalJointId.Head, 1050000L, new Rect(100, 50, 800, 400), Vector2.zero };
            Assert.True((bool)Call("TryGetScreenPosition", args));
            Assert.That((Vector2)args[4], Is.EqualTo(new Vector2(300, 150)));
        }
        [Test] public void WorldPlaneUsesTransformAndConfiguredSize()
        {
            var go = new GameObject("mapping"); go.transform.position = new Vector3(3, 4, 5);
            go.transform.rotation = Quaternion.Euler(0, 0, 90);
            try {
                var body = Fixture(0, 1); SetJoint(body, HumanVisionCanonicalJointId.Head, new Vector2(1, .5f)); Observe(body);
                object[] args = { 0, HumanVisionCanonicalJointId.Head, 1050000L, go.transform, new Vector2(2, 4), Vector3.zero };
                Assert.True((bool)Call("TryGetWorldPosition", args));
                Assert.That(Vector3.Distance((Vector3)args[5], new Vector3(3, 5, 5)), Is.LessThan(.0001f));
            } finally { UnityEngine.Object.DestroyImmediate(go); }
        }
        [Test] public void DegenerateJointAngleFails()
        {
            var body = Fixture(0, 1);
            foreach (var id in new[] { HumanVisionCanonicalJointId.ShoulderLeft, HumanVisionCanonicalJointId.ElbowLeft, HumanVisionCanonicalJointId.WristLeft })
                SetJoint(body, id, Vector2.one * .5f);
            Observe(body);
            Assert.False((bool)Call("TryGetAngle", 0, HumanVisionCanonicalJointId.ShoulderLeft, HumanVisionCanonicalJointId.ElbowLeft, HumanVisionCanonicalJointId.WristLeft, 1050000L, 0f));
        }
    }
}
