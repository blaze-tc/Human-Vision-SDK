using System;
using System.Collections;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSdkLifecycleTests
    {
        private Type type;
        private GameObject owner;
        private Component sdk;
        [SetUp] public void Setup()
        {
            type = typeof(HumanVision.Demo.VideoPlayerFrameSource).Assembly.GetType("HumanVision.HumanVisionSdk");
            Assert.NotNull(type, "The mountable SDK controller is missing.");
            owner = new GameObject("SDK lifecycle test"); sdk = owner.AddComponent(type);
        }
        [TearDown] public void Cleanup() { if (owner != null) UnityEngine.Object.DestroyImmediate(owner); }
        private object Call(string name, params object[] args)
        {
            foreach (var method in type.GetMethods())
                if (method.Name == name && method.GetParameters().Length == args.Length)
                    return method.Invoke(sdk, args);
            Assert.Fail("Missing API: " + name); return null;
        }
        [Test] public void MountingRequiresNoCanvasOrSceneSingleton()
        { Assert.That(owner.GetComponentInChildren<Canvas>(), Is.Null); Assert.That(Call("GetMaxBodies"), Is.EqualTo(4)); }
        [Test] public void PreInitializationQueriesAreSafeAndUnknown()
        {
            Assert.That(Call("GetUsersCount"), Is.Zero);
            Assert.That(Call("GetUserIdByIndex", 0), Is.EqualTo(0L));
            Assert.That(Call("GetUserIndexById", 1L), Is.EqualTo(-1));
            Assert.False((bool)Call("IsUserDetected", 0));
            Assert.False((bool)Call("TryGetRegionOccupancy", 0, false));
        }
        [Test] public void ValidPreStartSettingsAreAppliedWithoutOpeningInput()
        {
            Assert.True((bool)Call("TrySetMaxBodies", 2));
            Assert.That(Call("GetMaxBodies"), Is.EqualTo(2));
            Assert.False((bool)type.GetProperty("IsInitialized").GetValue(sdk));
        }
        [Test] public void InvalidCapacityDoesNotChangeConfiguration()
        {
            Assert.False((bool)Call("TrySetMaxBodies", 9));
            Assert.That(Call("GetMaxBodies"), Is.EqualTo(4));
            Assert.That(type.GetProperty("LastError").GetValue(sdk), Is.Not.Empty);
        }
        [Test] public void RegionInputAndReturnedConfigurationAreDefensiveCopies()
        {
            Assert.True((bool)Call("TrySetMaxBodies", 1));
            var regions = new[] { new Rect(.1f, .2f, .7f, .6f) };
            Assert.True((bool)Call("TrySetRegions", regions));
            regions[0] = new Rect(0, 0, 1, 1);
            object[] args = { 0, new Rect() }; Assert.True((bool)Call("TryGetRegion", args));
            Assert.That(((Rect)args[1]).x, Is.EqualTo(.1f));
            var options = type.GetProperty("Configuration").GetValue(sdk);
            var recognition = (HumanVisionSdkConfiguration)options.GetType().GetField("Recognition").GetValue(options);
            recognition.MaxBodies = 8;
            Assert.That(Call("GetMaxBodies"), Is.EqualTo(1));
        }
        [Test] public void ActiveRegionsRejectCapacityMismatch()
        {
            Assert.True((bool)Call("TrySetRegionsEnabled", true));
            Assert.False((bool)Call("TrySetMaxBodies", 2));
            Assert.That(Call("GetMaxBodies"), Is.EqualTo(4));
        }
        [Test] public void InvalidRegionIndicesAndSmallCopyBufferFail()
        {
            Assert.False((bool)Call("TryGetRegion", -1, new Rect()));
            Assert.False((bool)Call("TryGetRegion", 4, new Rect()));
            Assert.That(Call("CopyRegions", new Rect[1]), Is.Zero);
            Assert.That(Call("CopyRegions", new Rect[4]), Is.EqualTo(4));
        }
        [Test] public void InitializeAndStopAreWaitableAndRepeatedStopCompletes()
        {
            Assert.That(Call("Initialize"), Is.InstanceOf<IEnumerator>());
            var stop = (IEnumerator)Call("StopSdk"); int steps = 0;
            while (stop.MoveNext()) Assert.That(++steps, Is.LessThan(10));
            Assert.False(((IEnumerator)Call("Shutdown")).MoveNext());
            Assert.False((bool)type.GetProperty("IsRunning").GetValue(sdk));
        }
        [Test] public void BorrowedAndSampledQueriesHaveSeparatePublicEntrypoints()
        {
            Assert.NotNull(type.GetMethod("TryGetSampledBodyByIndex"));
            Assert.NotNull(type.GetMethod("TryGetBodyByIndex"));
            Assert.NotNull(type.GetMethod("TryGetJointScreenPosition"));
            Assert.NotNull(type.GetMethod("TryGetJointWorldPosition"));
            Assert.NotNull(type.GetMethod("CopySkeletonByIndex"));
        }
        [Test] public void ControllerOffersMainThreadLifecycleAndIdentityEvents()
        {
            foreach (var name in new[] { "Initialized", "ResultUpdated", "UserEntered", "UserExited", "RegionOccupancyChanged", "Stopped", "ErrorOccurred" })
                Assert.NotNull(type.GetEvent(name), name);
        }
        [Test] public void MenuCreatesOnlyOneControllerInCurrentSceneAndSupportsUndo()
        {
            var menuType = typeof(HumanVision.Editor.HumanVisionAndroidBuildSettings).Assembly.GetType("HumanVision.Editor.HumanVisionSdkMenu");
            Assert.NotNull(menuType);
            var create = menuType.GetMethod("CreateSdk");
            Assert.NotNull(create); create.Invoke(null, new object[] { new UnityEditor.MenuCommand(owner) });
            var created = UnityEditor.Selection.activeGameObject;
            Assert.That(created.transform.parent, Is.EqualTo(owner.transform));
            Assert.That(created.GetComponents(type).Length, Is.EqualTo(1));
            Assert.That(created.GetComponentInChildren<Canvas>(), Is.Null);
            UnityEditor.Undo.PerformUndo();
            Assert.True(created == null);
        }
    }
}
