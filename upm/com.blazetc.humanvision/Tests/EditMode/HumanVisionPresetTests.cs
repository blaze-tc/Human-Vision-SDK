using System;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using HumanVision.Demo;

namespace HumanVision.Tests
{
    public sealed class HumanVisionPresetTests
    {
        private ScriptableObject preset;
        private GameObject owner;
        private Type PresetType => typeof(HumanVisionSdk).Assembly.GetType("HumanVision.HumanVisionSdkPreset");
        [SetUp] public void SetUp()
        {
            Assert.NotNull(PresetType, "Initialization must support a project-owned ScriptableObject asset.");
            preset = ScriptableObject.CreateInstance(PresetType);
        }
        [TearDown] public void TearDown()
        {
            if (owner != null) UnityEngine.Object.DestroyImmediate(owner);
            if (preset != null) UnityEngine.Object.DestroyImmediate(preset);
        }
        private HumanVisionSdkOptions Read() => (HumanVisionSdkOptions)PresetType.GetMethod("CreateOptions").Invoke(preset, null);
        private void Write(HumanVisionSdkOptions value) => PresetType.GetMethod("SetOptions").Invoke(preset, new object[] { value });
        [Test] public void AssetAndCallerNeverShareMutableOptionsOrRegions()
        {
            var value = Read(); value.Recognition.MaxBodies = 2;
            value.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
            value.Input.RequestedWidth = 1920; value.Input.RequestedHeight = 1080; Write(value);
            value.Input.RequestedWidth = 640; value.Recognition.Regions[0] = default;
            var copy = Read(); Assert.That(copy.Input.RequestedWidth, Is.EqualTo(1920));
            Assert.That(copy.Recognition.Regions[0].width, Is.EqualTo(.5f));
            copy.Recognition.MaxBodies = 7; Assert.That(Read().Recognition.MaxBodies, Is.EqualTo(2));
        }
        [TestCase(HumanVisionAccelerationMode.Cpu, true)]
        [TestCase(HumanVisionAccelerationMode.Graphics, false)]
        [TestCase(HumanVisionAccelerationMode.Neural, false)]
        public void OneBackendChoiceControlsLegacyWindowsPreference(HumanVisionAccelerationMode mode, bool cpu)
        {
            var value = Read(); value.AccelerationMode = mode; value.UseWindowsCpu = !cpu; Write(value);
            Assert.That(Read().UseWindowsCpu, Is.EqualTo(cpu)); Assert.That(Read().AccelerationMode, Is.EqualTo(mode));
        }
        [Test] public void SdkReadsPresetBeforeStartAndRuntimeEditsDoNotMutateAsset()
        {
            var value = Read(); value.Recognition.MaxBodies = 2; Write(value);
            owner = new GameObject("preset SDK"); var sdk = owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            var property = typeof(HumanVisionSdk).GetProperty("ConfigurationAsset"); Assert.NotNull(property);
            property.SetValue(sdk, preset);
            Assert.That(sdk.GetMaxBodies(), Is.EqualTo(2));
            Assert.True(sdk.TrySetMaxBodies(3)); Assert.That(sdk.GetMaxBodies(), Is.EqualTo(3));
            Assert.That(Read().Recognition.MaxBodies, Is.EqualTo(2));
            Assert.That(HumanVisionSettingsData.FromOptions(sdk.Configuration).Recognition.MaxBodies, Is.EqualTo(3));
            Assert.False(sdk.IsInitialized);
        }
        [TestCase(HumanVision.Input.InputKind.WebCamera, "camera-2")]
        [TestCase(HumanVision.Input.InputKind.Rtsp, "rtsp://127.0.0.1/live")]
        [TestCase(HumanVision.Input.InputKind.Video, "D:/movie.mp4")]
        public void CodeInputOverridesOnlyItsCopy(HumanVision.Input.InputKind kind, string address)
        {
            var original = Read(); original.Input.RequestedWidth = 1920; Write(original);
            var value = (HumanVisionSdkOptions)PresetType.GetMethod("CreateOptionsForInput").Invoke(preset, new object[] { kind, address });
            value.Validate(); Assert.That(value.SourceKind, Is.EqualTo(kind));
            string actual = kind == HumanVision.Input.InputKind.WebCamera ? value.Input.CameraDevice :
                kind == HumanVision.Input.InputKind.Rtsp ? value.Input.RtspUrl : value.Input.VideoPath;
            Assert.That(actual, Is.EqualTo(address)); Assert.That(value.Input.RequestedWidth, Is.EqualTo(1920));
            Assert.That(Read().Input.RtspUrl, Is.EqualTo(original.Input.RtspUrl));
            Assert.That(Read().Input.VideoPath, Is.EqualTo(original.Input.VideoPath));
            Assert.That(Read().Input.CameraDevice, Is.EqualTo(original.Input.CameraDevice));
        }
        [Test] public void SettingsPreservesPresetTimeoutRuntimeAndImageDefaultsAcrossInputTabs()
        {
            var value = Read(); value.Input.RequestedWidth = 1920; value.Input.RequestedHeight = 1080;
            value.InputOpenTimeoutSeconds = 24; value.RuntimeRoot = "D:/runtime";
            var data = HumanVisionSettingsData.FromOptions(value);
            foreach (var kind in new[] { HumanVision.Input.InputKind.WebCamera, HumanVision.Input.InputKind.Rtsp, HumanVision.Input.InputKind.Video }) {
                data.SourceKind = kind; var copy = data.ToOptions();
                Assert.That(copy.Input.RequestedWidth, Is.EqualTo(1920));
                Assert.That(copy.InputOpenTimeoutSeconds, Is.EqualTo(24)); Assert.That(copy.RuntimeRoot, Is.EqualTo("D:/runtime"));
            }
            data.Camera.RequestedWidth = 640; Assert.That(data.Video.RequestedWidth, Is.EqualTo(1920));
        }
        [Test] public void CoordinateCopiesKeepSparseSlotIdentityValidityAndSourceFrame()
        {
            owner = new GameObject("coordinate SDK"); var sdk = owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            owner.transform.position = new Vector3(3, 4, 5); sdk.WorldPlaneSize = new Vector2(2, 4);
            long now = (long)(Time.realtimeSinceStartupAsDouble * 1000000);
            var body = (HumanVisionBody)Activator.CreateInstance(typeof(HumanVisionBody), true);
            typeof(HumanVisionBody).GetProperty("RegionIndex").SetValue(body, 3);
            typeof(HumanVisionBody).GetProperty("StableTrackId").SetValue(body, 4294967311L);
            typeof(HumanVisionBody).GetProperty("ObservationTimestampUs").SetValue(body, now);
            var position = (HumanVisionJoint)Activator.CreateInstance(typeof(HumanVisionJoint), BindingFlags.NonPublic | BindingFlags.Instance, null,
                new object[] { new Vector2(25, 75), new Vector2(.25f, .75f), .9f, true, false }, null);
            int wrist = (int)HumanVisionCanonicalJointId.WristLeft;
            body.CanonicalJoints[wrist] = (HumanVisionCanonicalJoint)Activator.CreateInstance(typeof(HumanVisionCanonicalJoint), BindingFlags.NonPublic | BindingFlags.Instance, null,
                new object[] { position, now, 0f }, null);
            var query = new HumanVisionSkeletonQueries(new HumanVisionSdkConfiguration());
            Assert.True(query.Observe(new[] { body }, 1, 7, 12, now, true));
            var fields = BindingFlags.NonPublic | BindingFlags.Instance;
            typeof(HumanVisionSdk).GetField("raw", fields).SetValue(sdk, query);
            typeof(HumanVisionSdk).GetField("acceptResults", fields).SetValue(sdk, true);
            typeof(HumanVisionSdk).GetField("sourceFresh", fields).SetValue(sdk, true);
            var pixels = new Vector2[32]; var world = new Vector3[32]; var valid = new bool[32];
            Assert.True(sdk.CopySkeleton2DByIndex(3, pixels, valid, out var metadata));
            Assert.That(metadata.StableTrackId, Is.EqualTo(4294967311L)); Assert.That(metadata.Index, Is.EqualTo(3));
            Assert.That(metadata.SourceFrameId, Is.EqualTo(12)); Assert.That(metadata.ResultSequence, Is.EqualTo(7));
            Assert.True(valid[wrist]); Assert.That(pixels[wrist], Is.EqualTo(new Vector2(25, 75))); Assert.False(valid[0]);
            Assert.True(sdk.CopySkeletonWorldByIndex(3, world, valid, out metadata));
            Assert.That(world[wrist], Is.EqualTo(new Vector3(2.5f, 3, 5)));
            Assert.False(sdk.CopySkeleton2DByIndex(0, pixels, valid, out metadata)); Assert.That(valid, Is.All.False);
        }
        [Test] public void ExplicitStartOpensInputAfterPrepareOnlyConfiguration()
        {
            var value = Read(); value.SourceKind = HumanVision.Input.InputKind.Video;
            value.Input.VideoPath = ""; value.OpenInputOnInitialize = false; Write(value);
            owner = new GameObject("prepare only SDK"); var sdk = owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            typeof(HumanVisionSdk).GetProperty("ConfigurationAsset").SetValue(sdk, preset);
            var start = sdk.StartSdk(); Assert.True(start.MoveNext());
            var initialize = (System.Collections.IEnumerator)start.Current;
            Assert.False(initialize.MoveNext(), "Explicit Start must validate/open the video, not silently prepare again.");
            StringAssert.Contains("视频", sdk.LastError);
            Assert.False(Read().OpenInputOnInitialize, "Starting must not mutate the asset.");
        }
        [Test] public void StoppedCopiesClearPreviousSkeleton()
        {
            owner = new GameObject("stopped SDK"); var sdk = owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            var positions = new Vector2[32]; var world = new Vector3[32]; var valid = new bool[32];
            for (int i = 0; i < 32; i++) { positions[i] = Vector2.one; world[i] = Vector3.one; valid[i] = true; }
            Assert.False(sdk.CopySkeleton2DByIndex(0, positions, valid, out var metadata));
            Assert.That(metadata.StableTrackId, Is.Zero); Assert.That(positions, Is.All.EqualTo(Vector2.zero)); Assert.That(valid, Is.All.False);
            Assert.False(sdk.CopySkeletonWorldByIndex(0, world, valid, out metadata)); Assert.That(world, Is.All.EqualTo(Vector3.zero));
        }
        [Test] public void InitializationAndInputMetadataHaveExplicitEntrypoints()
        {
            foreach (var method in new[] { "InitializeFromPreset", "StartSdk", "RestartSdk", "TryGetInputFrame", "CopySkeleton2DByIndex", "CopySkeletonWorldByIndex" })
                Assert.NotNull(typeof(HumanVisionSdk).GetMethod(method), method);
            owner = new GameObject("stopped SDK"); var sdk = owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            Assert.That(typeof(HumanVisionSdk).GetProperty("SupportsMetric3D").GetValue(sdk), Is.False);
            object[] args = { -1L, null };
            Assert.False((bool)typeof(HumanVisionSdk).GetMethod("TryGetInputFrame").Invoke(sdk, args));
        }
    }
}
