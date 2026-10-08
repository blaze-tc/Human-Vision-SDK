using System.Collections;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSdkRuntimeTests
    {
        private GameObject owner;
        private HumanVisionSdk sdk;
        private HumanVisionSdkOptions Options() => new HumanVisionSdkOptions {
            RuntimeRoot = Application.streamingAssetsPath + "/HumanVision/Runtime",
            OpenInputOnInitialize = false, UseWindowsCpu = true,
            Recognition = new HumanVisionSdkConfiguration { MaxBodies = 1, Regions = HumanVisionSdkConfiguration.CreateEqualRegions(1) }
        };
        [SetUp] public void Setup() {
            owner = new GameObject("SDK native lifecycle");
            sdk = owner.AddComponent<HumanVisionSdk>();
            typeof(HumanVisionSdk).GetField("initializeOnStart", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(sdk, false);
        }
        [UnityTearDown] public IEnumerator Cleanup() { if (sdk != null) yield return sdk.StopSdk(); if (owner != null) Object.Destroy(owner); yield return null; }
        [UnityTest] public IEnumerator PackagedRuntimeInitializesAndRepeatedShutdownCompletes()
        {
            int initialized = 0, stopped = 0;
            sdk.Initialized += () => initialized++; sdk.Stopped += () => stopped++;
            yield return sdk.Initialize(Options());
            Assert.True(sdk.IsInitialized, sdk.LastError);
            Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Ready));
            Assert.False(sdk.HasFreshResult); Assert.False(sdk.TryGetRegionOccupancy(0, out _));
            yield return sdk.StopSdk(); yield return sdk.Shutdown();
            Assert.False(sdk.IsInitialized); Assert.That(initialized, Is.EqualTo(1)); Assert.That(stopped, Is.EqualTo(1));
        }
        [UnityTest] public IEnumerator InvalidApplyKeepsActiveNativeConfiguration()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var invalid = sdk.Configuration.Recognition; invalid.MaxBodies = 9;
            Assert.False(sdk.TryApplyConfiguration(invalid)); Assert.True(sdk.IsInitialized);
            Assert.That(sdk.ActiveConfiguration.Recognition.MaxBodies, Is.EqualTo(1));
        }
        [UnityTest] public IEnumerator CapacityAndRegionsApplyTogetherThenRemainUnknownUntilNewObservation()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var config = sdk.Configuration.Recognition; config.MaxBodies = 2; config.UseRegions = true;
            config.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
            Assert.True(sdk.TryApplyConfiguration(config), sdk.LastError);
            Assert.That(sdk.GetMaxBodies(), Is.EqualTo(2)); Assert.That(sdk.GetRegionCount(), Is.EqualTo(2));
            Assert.False(sdk.TryGetRegionOccupancy(1, out _));
        }
        [UnityTest] public IEnumerator DisablingControllerCompletesNativeRetirement()
        {
            int stopped = 0; sdk.Stopped += () => stopped++;
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            sdk.enabled = false;
            double deadline = Time.realtimeSinceStartupAsDouble + 5;
            while (sdk.Busy && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
            Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Stopped)); Assert.That(stopped, Is.EqualTo(1));
        }
        [UnityTest] public IEnumerator DestroyingControllerDoesNotLeaveRuntimeHost()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            Object.Destroy(owner); owner = null; yield return null; yield return null;
            foreach (var manager in Object.FindObjectsOfType<HumanVisionManager>())
                Assert.False(manager.gameObject.name == "SDK native lifecycle Runtime Host");
        }
        [UnityTest] public IEnumerator CancellingResourcePreparationDoesNotStartLateRuntime()
        {
            sdk.StartCoroutine(sdk.Initialize(new HumanVisionSdkOptions { OpenInputOnInitialize = false }));
            sdk.enabled = false; yield return null; yield return null;
            Assert.False(sdk.IsInitialized); Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Stopped));
        }
        [UnityTest] public IEnumerator CpuQueriesUseNativeObservationClock()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var clock = typeof(HumanVisionSdk).GetProperty("NowUs", BindingFlags.Instance | BindingFlags.Static | BindingFlags.NonPublic);
            long actual = (long)clock.GetValue(sdk);
            Assert.That(System.Math.Abs(actual - (long)typeof(HumanVisionManager).Assembly.GetType("HumanVision.Interop.RuntimeBindings").GetMethod("HV_RuntimeClockUs", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic).Invoke(null, null)), Is.LessThan(1000000), "CPU observation ages must use the native monotonic clock.");
        }
        [UnityTest] public IEnumerator FailedInputDoesNotCommitCandidateConfiguration()
        {
            yield return sdk.Initialize(Options()); var before = sdk.ActiveConfiguration;
            var bad = Options(); bad.OpenInputOnInitialize = true; bad.SourceKind = HumanVision.Input.InputKind.Video;
            bad.Input.VideoPath = System.IO.Path.Combine(Application.temporaryCachePath, "missing-" + System.Guid.NewGuid() + ".mp4");
            bad.Recognition.MaxBodies = 2; bad.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
            LogAssert.ignoreFailingMessages = true;
            try { yield return sdk.Initialize(bad); }
            finally { LogAssert.ignoreFailingMessages = false; }
            Assert.False(sdk.IsRunning); Assert.That(sdk.LastError, Is.Not.Empty);
            Assert.That(sdk.ActiveConfiguration.Recognition.MaxBodies, Is.EqualTo(before.Recognition.MaxBodies));
        }
    }
}
