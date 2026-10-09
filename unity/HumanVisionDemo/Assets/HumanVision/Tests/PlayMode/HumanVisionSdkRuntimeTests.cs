using System.Collections;
using System.IO;
using System.Reflection;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSdkRuntimeTests
    {
        private GameObject owner;
        private HumanVisionSdk sdk;
        private CapturedSource capturedSource;
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
        [UnityTearDown] public IEnumerator Cleanup() {
            if (sdk != null) yield return sdk.StopSdk();
            capturedSource?.Close(); capturedSource = null;
            if (owner != null) UnityEngine.Object.Destroy(owner);
            yield return null;
        }
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
        [UnityTest] public IEnumerator RejectedNeuralRequestPreservesLiveNativeSessionAndCommittedSettings()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var manager = Manager(); var before = sdk.ActiveConfiguration;
            string profile = sdk.RuntimeProfile; var bodies = manager.Bodies;
            var requested = Options(); requested.AccelerationMode = HumanVisionAccelerationMode.Neural;
            requested.InputQuality = ModelInputQuality.Low; requested.Recognition.MaxBodies = 2;
            requested.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
            yield return sdk.Initialize(requested);
            Assert.True(sdk.IsInitialized); Assert.AreSame(manager, Manager());
            Assert.AreSame(bodies, manager.Bodies); Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Ready));
            Assert.That(sdk.RuntimeProfile, Is.EqualTo(profile));
            Assert.That(sdk.ActiveAccelerationMode, Is.EqualTo(HumanVisionAccelerationMode.Cpu));
            Assert.That(sdk.ActiveConfiguration.AccelerationMode, Is.EqualTo(before.AccelerationMode));
            Assert.That(sdk.ActiveConfiguration.Recognition.MaxBodies, Is.EqualTo(1));
            Assert.That(sdk.LastError, Is.Not.Empty);
        }
        [UnityTest] public IEnumerator SynchronousManagerNeuralRequestDoesNotShutDownExistingNativeSession()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var manager = Manager(); var bodies = manager.Bodies; string profile = manager.ActiveRuntimeProfile;
            LogAssert.Expect(LogType.Error, "Neural model initialization must run asynchronously. Use HumanVisionSdk.Initialize with the Neural acceleration preference.");
            Assert.False(manager.TryInitialize(new HumanVisionConfig { RuntimeRoot = Options().RuntimeRoot, Profile = "android-rknn-npu-quality-low", MaxBodies = 2 }));
            Assert.True(manager.IsInitialized); Assert.AreSame(bodies, manager.Bodies);
            Assert.That(manager.ActiveRuntimeProfile, Is.EqualTo(profile)); Assert.That(manager.MaxBodies, Is.EqualTo(1));
        }
        private HumanVisionManager Manager() => (HumanVisionManager)typeof(HumanVisionSdk).GetField("manager", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(sdk);
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
        [UnityTest] public IEnumerator CpuAdapterReadbackPreservesCaptureAgeInNativeResultAndFreshness()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var manager = Manager();
            // Force a real source observation to precede its publication by 200 ms.
            const long sourceAgeUs = 200000;
            while (InputMonotonicClock.NowUs < sourceAgeUs) yield return null;
            var bridge = manager.gameObject.GetComponent<VideoPlayerFrameSource>();
            bridge.ConfigureLiveInput(true);
            var rowOrder = typeof(VideoPlayerFrameSource).GetField("_rowOrderReady", BindingFlags.Instance | BindingFlags.NonPublic);
            double probeDeadline = Time.realtimeSinceStartupAsDouble + 5;
            while (!(bool)rowOrder.GetValue(bridge) && Time.realtimeSinceStartupAsDouble < probeDeadline) yield return null;
            Assert.True((bool)rowOrder.GetValue(bridge), "The actual GPU row-order probe must complete before publication.");
            string fixture = Path.Combine(System.Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT"), "tests/testdata/d0_1_human_pose.bgr");
            capturedSource = new CapturedSource(File.ReadAllBytes(fixture), sourceAgeUs);
            bridge.Configure(manager, null, null);
            bridge.BindUnifiedSource(capturedSource);
            long before = NativeNowUs();
            Assert.True(capturedSource.TryGetLatestFrame(-1, out var published));
            long sourceAgeBeforeSubmit = InputMonotonicClock.NowUs - published.SourceTimestampUs;
            double unityBefore = Time.realtimeSinceStartupAsDouble * 1000000;
            Assert.That(System.Math.Abs(before - unityBefore), Is.GreaterThan(1000000), "This regression must exercise different Unity/native epochs.");
            manager.GetComponent<HumanVisionInputAdapter>().Tick();
            long after = NativeNowUs();
            double deadline = Time.realtimeSinceStartupAsDouble + 20;
            while (manager.ResultSequence == 0 && Time.realtimeSinceStartupAsDouble < deadline) {
                capturedSource.Retirement.Poll(); yield return null;
            }
            Assert.That(manager.ResultSequence, Is.GreaterThan(0), manager.LastError);
            Assert.That(manager.BodyCount, Is.EqualTo(1), "The actual CPU pipeline must recognize the checked-in person fixture.");
            long timestamp = manager.SourceTimestampUs;
            Assert.That(timestamp, Is.InRange(before - sourceAgeBeforeSubmit - 10000, after - sourceAgeBeforeSubmit + 10000),
                "Source publication and GPU readback must not relabel the observation as newer.");
            Assert.That(manager.Bodies[0].ObservationTimestampUs, Is.EqualTo(timestamp));
            Assert.That(manager.SampledBodyCount, Is.EqualTo(1), "Native sampling must use the observation clock.");
            var queries = new HumanVisionSkeletonQueries(new HumanVisionSdkConfiguration {
                MaxBodies = 1, Regions = HumanVisionSdkConfiguration.CreateEqualRegions(1), MaximumResultAgeMilliseconds = 1000
            });
            Assert.True(queries.Observe(manager.Bodies, manager.BodyCount, manager.ResultSequence, manager.SourceFrameId, timestamp, true));
            Assert.True(queries.HasFreshResult(timestamp + 999999));
            Assert.False(queries.HasFreshResult(timestamp + 1000001));
            // The legacy CPU bridge numbers its first frame zero; V2 telemetry
            // begins at a positive frame ID. Publish another real source frame.
            long sequence = manager.ResultSequence;
            capturedSource.Publish(2, sourceAgeUs);
            deadline = Time.realtimeSinceStartupAsDouble + 20;
            while (manager.ResultSequence == sequence && Time.realtimeSinceStartupAsDouble < deadline) {
                capturedSource.Retirement.Poll(); yield return null;
            }
            Assert.That(manager.ResultSequence, Is.GreaterThan(sequence), manager.LastError);
            Assert.That(manager.SourceFrameId, Is.GreaterThan(0));
            var stats = typeof(HumanVisionManager).GetProperty("RuntimeStatsV2", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(manager);
            Assert.That((ulong)stats.GetType().GetField("FreshObservationFrames").GetValue(stats), Is.GreaterThan(0));
            Assert.That((long)stats.GetType().GetField("CaptureTimestampUs").GetValue(stats), Is.EqualTo(manager.SourceTimestampUs));
        }
        [UnityTest] public IEnumerator SynchronousNeuralCapacityRequestRetainsLiveNativeHostAndInput()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            var manager = Manager();
            var session = typeof(HumanVisionManager).GetField("_session", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(manager);
            var runtimeConfig = (HumanVisionConfig)session.GetType().GetField("_config", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(session);
            string originalProfile = runtimeConfig.Profile;
            var handle = session.GetType().GetField("_handle", BindingFlags.Instance | BindingFlags.NonPublic);
            var originalHandle = handle.GetValue(session);
            // Classification-only policy test over a real CPU native lease. No NPU driver is initialized.
            runtimeConfig.Profile = "android-rknn-npu-quality-low";
            capturedSource = new CapturedSource(File.ReadAllBytes(Path.Combine(System.Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT"), "tests/testdata/d0_1_human_pose.bgr")), 0);
            var bridge = manager.gameObject.GetComponent<VideoPlayerFrameSource>(); bridge.BindUnifiedSource(capturedSource);
            typeof(HumanVisionSdk).GetField("source", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(sdk, capturedSource);
            typeof(HumanVisionSdk).GetProperty("State").SetValue(sdk, HumanVisionSdkState.Running);
            var before = sdk.ActiveConfiguration;
            var rawQueries = typeof(HumanVisionSdk).GetField("raw", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(sdk);
            try {
                var requested = before.Recognition.Clone(); requested.MaxBodies = 2;
                requested.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
                LogAssert.ignoreFailingMessages = true;
                try { Assert.False(sdk.TryApplyConfiguration(requested)); }
                finally { LogAssert.ignoreFailingMessages = false; }
                Assert.True(sdk.IsInitialized); Assert.True(sdk.IsRunning);
                Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Running));
                Assert.AreSame(manager, Manager()); Assert.AreSame(session, typeof(HumanVisionManager).GetField("_session", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(manager));
                Assert.AreEqual(originalHandle, handle.GetValue(session));
                Assert.That(sdk.ActiveConfiguration.Recognition.MaxBodies, Is.EqualTo(1));
                Assert.That(sdk.Configuration.Recognition.MaxBodies, Is.EqualTo(1));
                Assert.That(sdk.InputState, Is.EqualTo(InputSourceState.Streaming));
                Assert.AreSame(capturedSource, typeof(HumanVisionSdk).GetField("source", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(sdk));
                Assert.AreSame(rawQueries, typeof(HumanVisionSdk).GetField("raw", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(sdk));
                StringAssert.Contains("Initialize", sdk.LastError);
                // The convenience API and direct runtime API must use the same guarded policy.
                LogAssert.ignoreFailingMessages = true;
                try { Assert.False(sdk.TrySetMaxBodies(2)); }
                finally { LogAssert.ignoreFailingMessages = false; }
                Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Running)); Assert.True(sdk.IsInitialized);
                var error = Assert.Throws<TargetInvocationException>(() => session.GetType().GetMethod("ReconfigureMaxBodies").Invoke(session, new object[] { 2 }));
                Assert.That(error.InnerException, Is.TypeOf<System.InvalidOperationException>());
                StringAssert.Contains("Initialize", error.InnerException.Message);
                Assert.AreEqual(originalHandle, handle.GetValue(session)); Assert.That(manager.MaxBodies, Is.EqualTo(1));
                LogAssert.Expect(LogType.Error, new System.Text.RegularExpressions.Regex("Neural.*Initialize"));
                Assert.False(manager.TrySetMaxBodies(2));
                Assert.AreEqual(originalHandle, handle.GetValue(session)); Assert.True(manager.IsInitialized);
                var safe = before.Recognition.Clone(); safe.UseRegions = true;
                Assert.True(sdk.TryApplyConfiguration(safe), sdk.LastError);
                Assert.True(sdk.TrySetMaxBodies(1), sdk.LastError);
                Assert.AreSame(manager, Manager()); Assert.AreEqual(originalHandle, handle.GetValue(session));
                Assert.True(sdk.ActiveConfiguration.Recognition.UseRegions);
                Assert.That(sdk.State, Is.EqualTo(HumanVisionSdkState.Running));
                Assert.That(sdk.InputState, Is.EqualTo(InputSourceState.Streaming));
            } finally { runtimeConfig.Profile = originalProfile; }
        }
        private static long NativeNowUs() => (long)typeof(HumanVisionManager).Assembly.GetType("HumanVision.Interop.RuntimeBindings")
            .GetMethod("HV_RuntimeClockUs", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, null);
        [UnityTest] public IEnumerator PreparedNativeCandidateCommitsItsExistingRegionRevisionAndRetiresBeforeStopped()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized, sdk.LastError);
            long preparedRevision = (long)typeof(HumanVisionSdk).GetField("revision", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(sdk) + 1;
            var type = typeof(HumanVisionManager).Assembly.GetType("HumanVision.HumanVisionRuntimeSession");
            var config = new HumanVisionConfig { RuntimeRoot = Options().RuntimeRoot, Profile = Manager().ActiveRuntimeProfile, MaxBodies = 1 };
            int caller = System.Threading.Thread.CurrentThread.ManagedThreadId, worker = caller;
            var create = System.Threading.Tasks.Task.Run(() => {
                worker = System.Threading.Thread.CurrentThread.ManagedThreadId;
                var value = System.Activator.CreateInstance(type, BindingFlags.Instance | BindingFlags.NonPublic, null, new object[] { config }, null);
                type.GetMethod("SetRegions").Invoke(value, new object[] { HumanVisionSdkConfiguration.CreateEqualRegions(1), preparedRevision });
                return value;
            });
            while (!create.IsCompleted) yield return null;
            Assert.False(create.IsFaulted, create.Exception?.ToString()); Assert.AreNotEqual(caller, worker);
            var prepared = create.Result;
            yield return sdk.StopSdk();
            var hostObject = new GameObject("Prepared native policy host");
            var lifetime = hostObject.AddComponent(typeof(HumanVisionSdk).Assembly.GetType("HumanVision.HumanVisionSdkLifetime"));
            var manager = hostObject.AddComponent<HumanVisionManager>(); var bridge = hostObject.AddComponent<VideoPlayerFrameSource>(); bridge.Configure(manager, null, null);
            // The actual CPU handle is classified as Neural only for adoption/retirement policy, never for NPU inference.
            var preparedConfig = (HumanVisionConfig)type.GetField("_config", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(prepared);
            preparedConfig.Profile = "android-rknn-npu-quality-low"; config.Profile = preparedConfig.Profile;
            typeof(HumanVisionManager).GetMethod("AdoptPreparedSession", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(manager, new object[] { config, prepared });
            foreach (var field in new[] { new { Name = "manager", Value = (object)manager }, new { Name = "bridge", Value = (object)bridge }, new { Name = "host", Value = (object)lifetime } })
                typeof(HumanVisionSdk).GetField(field.Name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(sdk, field.Value);
            var recognition = Options().Recognition; recognition.UseRegions = true;
            var apply = typeof(HumanVisionSdk).GetMethod("ApplyRecognition", BindingFlags.Instance | BindingFlags.NonPublic);
            bool applied;
            LogAssert.ignoreFailingMessages = true;
            try { applied = (bool)apply.Invoke(sdk, apply.GetParameters().Length == 1 ? new object[] { recognition } : new object[] { recognition, preparedRevision }); }
            finally { LogAssert.ignoreFailingMessages = false; }
            Assert.True(applied, "Prepared native regions are already installed; promotion must not submit the same revision twice.");
            Assert.True(manager.IsInitialized); Assert.AreSame(manager, Manager());
            Assert.True(manager.TryCopyRegionAssignments(new int[1], out long nativeRevision));
            Assert.That(nativeRevision, Is.EqualTo(preparedRevision));
            Assert.That((long)typeof(HumanVisionSdk).GetField("revision", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(sdk), Is.EqualTo(preparedRevision));
            Assert.True(sdk.TrySetRegions(HumanVisionSdkConfiguration.CreateEqualRegions(1)), sdk.LastError);
            Assert.True(manager.TryCopyRegionAssignments(new int[1], out nativeRevision)); Assert.That(nativeRevision, Is.EqualTo(preparedRevision + 1));
            var handle = type.GetField("_handle", BindingFlags.Instance | BindingFlags.NonPublic);
            bool stoppedAfterRelease = false; sdk.Stopped += () => stoppedAfterRelease = (System.IntPtr)handle.GetValue(prepared) == System.IntPtr.Zero;
            yield return sdk.StopSdk(); Assert.True(stoppedAfterRelease); Assert.False(manager.IsInitialized);
            Assert.That((System.IntPtr)handle.GetValue(prepared), Is.EqualTo(System.IntPtr.Zero));
        }
        [UnityTest] public IEnumerator InternalReplacementRetainsBusyStateAndSuppressesReentrantStoppedInitialization()
        {
            yield return sdk.Initialize(Options()); Assert.True(sdk.IsInitialized,sdk.LastError);
            int callbacks=0; sdk.Stopped+=()=>{ callbacks++; sdk.TryApplyConfiguration(Options()); };
            typeof(HumanVisionSdk).GetMethod("BeginStop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(sdk,new object[]{false});
            for(int i=0;i<3;i++) yield return null;
            Assert.That(callbacks,Is.EqualTo(0)); Assert.True(sdk.Busy); Assert.That(sdk.State,Is.EqualTo(HumanVisionSdkState.Preparing));
            Assert.False(sdk.TryApplyConfiguration(Options())); Assert.False(sdk.IsInitialized);
        }
        [UnityTest] public IEnumerator WindowsSavedNeuralDraftProducesExecutableCpuOptionsThroughTheComputeRow()
        {
            string file=Path.Combine(Application.temporaryCachePath,"pc-compute-"+System.Guid.NewGuid()+".json");
            var view=HumanVisionSettingsView.Create(owner.transform);
            var controller=owner.AddComponent<HumanVisionSettingsController>(); controller.enabled=false; controller.Configure(sdk,view); view.Bind(controller);
            try {
                var saved=new HumanVisionSettingsData { AccelerationMode=HumanVisionAccelerationMode.Neural, InputQuality=ModelInputQuality.Low,
                    GraphicsInputQuality=ModelInputQuality.High, UseWindowsCpu=true, Recognition=Options().Recognition };
                HumanVisionSdkSettingsStore.Save(file,saved); view.ShowDraft(HumanVisionSdkSettingsStore.Load(file));
                var executable=view.ReadDraft(); Assert.That(executable.AccelerationMode,Is.EqualTo(HumanVisionAccelerationMode.Cpu)); Assert.True(executable.UseWindowsCpu);
                var requested=executable.ToOptions(Options().RuntimeRoot); requested.OpenInputOnInitialize=false;
                yield return sdk.Initialize(requested);
                Assert.True(sdk.IsInitialized,sdk.LastError); Assert.That(sdk.LastError,Is.Empty);
                Assert.That(sdk.RuntimeProfile,Is.EqualTo("windows-pc-cpu")); Assert.That(sdk.ActiveAccelerationMode,Is.EqualTo(HumanVisionAccelerationMode.Cpu));
                Assert.That(sdk.ActiveConfiguration.AccelerationMode,Is.EqualTo(HumanVisionAccelerationMode.Cpu));
                HumanVisionSdkSettingsStore.Save(file,executable); Assert.That(HumanVisionSdkSettingsStore.Load(file).AccelerationMode,Is.EqualTo(HumanVisionAccelerationMode.Cpu));
            } finally { File.Delete(file); }
        }
        [UnityTest] public IEnumerator ActualAndroidCpuCapacityChangesKeepModelGeometryAndDiagnosticsTogether()
        {
            string root=System.Environment.GetEnvironmentVariable("HV_TEST_NEURAL_RUNTIME_ROOT");
            if(string.IsNullOrEmpty(root)) root=Path.Combine(System.Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT")??"","out/rknn-dual-20261009/stage/Runtime");
            if(!Directory.Exists(root)) root=Options().RuntimeRoot;
            var options=Options(); options.RuntimeRoot=root; options.AccelerationMode=HumanVisionAccelerationMode.Cpu;
            options.Recognition.MaxBodies=2; options.Recognition.Regions=HumanVisionSdkConfiguration.CreateEqualRegions(2);
            yield return sdk.Initialize(options); Assert.True(sdk.IsInitialized,sdk.LastError);
            // Exercise the genuine ORT CPU profile on Windows. This is not an Android device performance claim.
            var manager=Manager(); Assert.True(manager.TryInitialize(new HumanVisionConfig { RuntimeRoot=root, Profile="android-cpu-nohands", MaxBodies=2 }),manager.LastError);
            typeof(HumanVisionSdk).GetField("activeContract",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(sdk,AnalysisContract.Load(root,"android-cpu-nohands",2));
            Assert.True(sdk.TryApplyConfiguration(options.Recognition),sdk.LastError);
            Assert.That(sdk.ActiveAccelerationMode,Is.EqualTo(HumanVisionAccelerationMode.Cpu));
            Assert.That(sdk.ActiveModelPack,Is.EqualTo("precision-t-26")); Assert.That(sdk.AnalysisInputSize,Is.EqualTo(new Vector2Int(192,256)));
            StringAssert.Contains("Pipeline=pipeline.topdown",sdk.RuntimeDiagnostics);
            Assert.True(sdk.TrySetMaxBodies(3),sdk.LastError); Assert.That(manager.MaxBodies,Is.EqualTo(3));
            Assert.That(sdk.ActiveModelPack,Is.EqualTo("rtmo-t-416")); Assert.That(sdk.AnalysisInputSize,Is.EqualTo(new Vector2Int(416,416)));
            StringAssert.Contains("Pipeline=pipeline.rtmo",sdk.RuntimeDiagnostics);
            Assert.True(sdk.TrySetMaxBodies(2),sdk.LastError); Assert.That(manager.MaxBodies,Is.EqualTo(2));
            Assert.That(sdk.ActiveModelPack,Is.EqualTo("precision-t-26")); Assert.That(sdk.AnalysisInputSize,Is.EqualTo(new Vector2Int(192,256)));
            StringAssert.Contains("Pipeline=pipeline.topdown",sdk.RuntimeDiagnostics);
            var session=typeof(HumanVisionManager).GetField("_session",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(manager);
            var handle=session.GetType().GetField("_handle",BindingFlags.Instance|BindingFlags.NonPublic);
            bool releasedBeforeStopped=false; sdk.Stopped+=()=>releasedBeforeStopped=(System.IntPtr)handle.GetValue(session)==System.IntPtr.Zero;
            yield return sdk.StopSdk(); Assert.True(releasedBeforeStopped); Assert.False(manager.IsInitialized);
        }
        private sealed class CapturedSource : IHumanVisionFrameSource
        {
            internal readonly SourceRetirement Retirement = new SourceRetirement();
            private readonly SourceGeneration generation;
            private readonly Texture2D texture;
            private readonly ulong token;
            public InputSourceState State { get; private set; } = InputSourceState.Streaming;
            public string LastError => string.Empty;
            public Texture CurrentTexture => generation.CurrentTexture;
            internal CapturedSource(byte[] bgr, long ageUs) {
                const int width = 218, height = 346;
                Assert.That(bgr.Length, Is.EqualTo(width * height * 3));
                var rgba = new Color32[width * height];
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                    int source = (y * width + x) * 3;
                    rgba[(height - 1 - y) * width + x] = new Color32(bgr[source + 2], bgr[source + 1], bgr[source], 255);
                }
                texture = new Texture2D(width, height, TextureFormat.RGBA32, false, true);
                texture.SetPixels32(rgba); texture.Apply();
                generation = new SourceGeneration(Retirement); generation.BeginGeneration();
                token = Retirement.Register(generation.Generation, texture, () => UnityEngine.Object.Destroy(texture));
                Publish(1, ageUs);
            }
            internal void Publish(long frameId, long ageUs) {
                long now = InputMonotonicClock.NowUs;
                var frame = new HumanVisionTextureFrame(generation.SourceId, generation.Generation, frameId, texture,
                    now, -1, 0, false, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Linear,
                    FrameTimestampKind.UnityObserved, token, now - ageUs, FrameClockDomain.InputMonotonic, 0);
                Assert.True(generation.TryPublish(in frame));
            }
            public void Open(HumanVisionSourceSettings settings) { }
            public bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) => generation.TryGetLatestFrame(after, out frame);
            public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) => generation.TryAcquireSourceCopyLease(in frame, out lease);
            public void Close() { generation.Close(); Retirement.Poll(); State = InputSourceState.Stopped; }
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
        [UnityTest] public IEnumerator SettingsLogsFailedNativeInputBeforeFirstSuccessfulApply()
        {
            string directory = System.IO.Path.Combine(Application.temporaryCachePath, "StartupDiagnostics-" + System.Guid.NewGuid());
            var view = HumanVision.Demo.HumanVisionSettingsView.Create(owner.transform);
            var controller = owner.AddComponent<HumanVision.Demo.HumanVisionSettingsController>();
            // 本测试手动驱动 Apply；不加载任何机器上的已保存场景设置。
            controller.enabled = false; controller.Configure(sdk, view);
            var type = typeof(HumanVisionSdk).Assembly.GetType("HumanVision.Demo.HumanVisionSettingsLogger");
            var logger = type.GetConstructor(new[] { typeof(string) }).Invoke(new object[] { directory });
            type.GetMethod("BeginStartup").Invoke(logger, null);
            typeof(HumanVision.Demo.HumanVisionSettingsController).GetField("logger", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(controller, logger);
            typeof(HumanVision.Demo.HumanVisionSettingsController).GetField("runtimeRoot", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(controller, Options().RuntimeRoot);
            var data = new HumanVision.Demo.HumanVisionSettingsData { SourceKind = HumanVision.Input.InputKind.Video, UseWindowsCpu = true };
            data.Video.VideoPath = System.IO.Path.Combine(directory, "missing-real-input.mp4");
            data.Recognition.MaxBodies = 1; data.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(1);
            view.ShowDraft(data);
            LogAssert.ignoreFailingMessages = true;
            try {
                yield return controller.Apply(false);
                Assert.False(sdk.IsRunning); Assert.Null(controller.Active); Assert.Null(controller.Saved);
                string logs = string.Join("\n", System.Array.ConvertAll(System.IO.Directory.GetFiles(directory, "*.log", System.IO.SearchOption.AllDirectories), System.IO.File.ReadAllText));
                Assert.True(logs.Contains("apply.begin") && logs.Contains("apply.failed") && logs.Contains("missing-real-input.mp4") && logs.Contains("session.start"), "Actual native/input failure context was not written.");
            } finally {
                LogAssert.ignoreFailingMessages = false; ((System.IDisposable)logger).Dispose();
                if (System.IO.Directory.Exists(directory)) System.IO.Directory.Delete(directory, true);
            }
        }
    }
}
