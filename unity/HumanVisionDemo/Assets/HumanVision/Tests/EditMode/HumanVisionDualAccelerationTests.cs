using System;
using NUnit.Framework;
using HumanVision.Editor;
using HumanVision.Demo;
using UnityEngine;
using System.Linq;
using System.Threading;
using System.IO;

namespace HumanVision.Tests
{
    public sealed class HumanVisionDualAccelerationTests
    {
        private static readonly string[] Profiles = { "android-ncnn-vulkan-quality-low", "android-ncnn-vulkan", "android-ncnn-vulkan-quality-high", "android-rknn-npu-quality-low", "android-cpu-nohands" };
        [Test]
        public void DualBuildDefaultsToGraphicsAndAdmitsItsNeuralProfile()
        {
            Assert.AreEqual("android-ncnn-vulkan", HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", "android-ncnn-vulkan", "android-dual-vulkan-npu", Profiles));
            Assert.AreEqual("android-rknn-npu-quality-low", HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("android-rknn-npu-quality-low", "android-ncnn-vulkan", "android-dual-vulkan-npu", Profiles));
            Assert.AreEqual("android-cpu-nohands", HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("android-cpu-nohands", "android-ncnn-vulkan", "android-dual-vulkan-npu", Profiles));
        }
        [Test]
        public void DualBuildRejectsIncompleteAndUnexpectedAdmission()
        {
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", "android-ncnn-vulkan", "android-dual-vulkan-npu", new[] { "android-rknn-npu-quality-low" }));
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("android-rknn-npu-quality-high", "android-ncnn-vulkan", "android-dual-vulkan-npu", Profiles));
        }
        [TestCase(false)] [TestCase(true)]
        public void DualBuildRejectsMissingMapBeforeLegacyDefaultResolution(bool empty)
        {
            string[] missing = empty ? new string[0] : null;
            foreach (string baked in new[] { "android-ncnn-vulkan", "android-rknn-npu-quality-low" })
                Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", baked, "android-dual-vulkan-npu", missing));
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("android-ncnn-vulkan", "android-ncnn-vulkan", "android-dual-vulkan-npu", missing));
            Assert.AreEqual("android-ncnn-vulkan", HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", "android-ncnn-vulkan", "android-ncnn-vulkan", missing), "Legacy single-mode metadata keeps its existing strict base-profile contract.");
        }
        [Test]
        public void DualRegistryRequiresGraphicsBridgeAndDefaultsToGraphicsProfile()
        {
            var mode = HumanVisionAndroidRuntimeModeRegistry.Resolve("android-dual-vulkan-npu");
            Assert.AreEqual("android-ncnn-vulkan", mode.ProfileId);
            Assert.IsTrue(mode.RequiresVulkan); Assert.IsTrue(mode.RequiresGpuBridge); Assert.IsTrue(mode.RequiresNcnn);
        }
        [Test]
        public void NeuralProfileUsesCpuSubmissionWithoutChangingGraphicsRoute()
        {
            Assert.AreEqual(HumanVisionAndroidFrameRoute.FramePath.Cpu, HumanVisionAndroidFrameRoute.Select("android-rknn-npu-quality-low"));
            Assert.AreEqual(HumanVisionAndroidFrameRoute.FramePath.Gpu, HumanVisionAndroidFrameRoute.Select("android-ncnn-vulkan-quality-high"));
        }
        [Test]
        public void SavedInvalidAccelerationIsRejectedRatherThanIgnored()
        {
            var saved = JsonUtility.FromJson<HumanVisionSettingsData>("{\"AccelerationMode\":99}");
            Assert.Throws<ArgumentException>(() => saved.Validate());
            var options = JsonUtility.FromJson<HumanVisionSdkOptions>("{\"AccelerationMode\":99}");
            Assert.Throws<ArgumentException>(() => options.Validate());
            var shared = JsonUtility.FromJson<SharedRecognitionSettings>("{\"AccelerationMode\":99}");
            Assert.Throws<ArgumentException>(() => shared.Validate());
        }
        [Test]
        public void DualBuildRefusesMissingNeuralRuntime()
        {
            var environment = new AndroidBuildEnvironment { MinimumApiLevel = 26, Arm64Only = true, Il2Cpp = true,
                VulkanAvailable = true, VulkanFirst = true, HasHumanVisionLibrary = true, HasProfile = true,
                HasNcnnLibrary = true, HasBridgeSymbolManifest = true, HasNcnnModelPackAssets = true, HasNcnnModelPackSha256Index = true };
            var issues = HumanVisionAndroidRuntimeBuildValidator.Validate(HumanVisionAndroidRuntimeModeRegistry.Resolve("android-dual-vulkan-npu"), environment);
            Assert.That(issues, Has.Some.Property("Code").EqualTo("HasNeuralRuntimeAudit"));
        }
        [TestCase("RK3588", "rockchip", true)]
        [TestCase("rk3588s", "", true)]
        [TestCase("", "rk3588_t", true)]
        [TestCase("SM8350", "qcom", false)]
        [TestCase("rk3568", "rockchip", false)]
        [TestCase("rk3588-fake", "mali-g610", false)]
        public void NeuralDeviceAdmissionUsesExactSocProperties(string soc, string hardware, bool supported)
        {
            Assert.AreEqual(supported, HumanVisionAndroidAccelerationSelection.SupportsNeuralDevice(soc,hardware));
            if (supported) Assert.AreEqual("android-rknn-npu-quality-low", HumanVisionAndroidAccelerationSelection.Resolve(HumanVisionAccelerationMode.Neural, ModelInputQuality.Low,"android-ncnn-vulkan","android-dual-vulkan-npu",Profiles,soc,hardware));
            else {
                var error = Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidAccelerationSelection.Resolve(HumanVisionAccelerationMode.Neural,ModelInputQuality.Low,"android-ncnn-vulkan","android-dual-vulkan-npu",Profiles,soc,hardware));
                StringAssert.Contains(soc,error.Message); StringAssert.Contains(hardware,error.Message);
            }
        }
        [TestCase(ModelInputQuality.Medium)] [TestCase(ModelInputQuality.High)]
        public void NeuralRejectsUnavailableQualityEvenOnSupportedDevice(ModelInputQuality quality)
        {
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidAccelerationSelection.Resolve(HumanVisionAccelerationMode.Neural,quality,"android-ncnn-vulkan","android-dual-vulkan-npu",Profiles,"rk3588",""));
        }
        [Test]
        public void PreferencesSaveSeparatelyAndReturnToPreviousGraphicsQuality()
        {
            var data = new HumanVisionSettingsData { InputQuality = ModelInputQuality.High };
            data.SelectAcceleration(HumanVisionAccelerationMode.Neural);
            Assert.AreEqual(ModelInputQuality.Low,data.InputQuality);
            var loaded = JsonUtility.FromJson<HumanVisionSettingsData>(JsonUtility.ToJson(data));
            Assert.AreEqual(HumanVisionAccelerationMode.Neural,loaded.ToOptions().AccelerationMode);
            Assert.AreEqual(ModelInputQuality.High,loaded.GraphicsInputQuality);
            loaded.SelectAcceleration(HumanVisionAccelerationMode.Graphics);
            Assert.AreEqual(ModelInputQuality.High,loaded.InputQuality);
            Assert.AreEqual(HumanVisionAccelerationMode.Neural,data.AccelerationMode);
            var legacy = JsonUtility.FromJson<HumanVisionSettingsData>("{\"InputQuality\":1}");
            Assert.AreEqual(HumanVisionAccelerationMode.Graphics,legacy.AccelerationMode);
            Assert.AreEqual(ModelInputQuality.High,legacy.InputQuality);
        }
        [Test]
        public void WiredAccelerationChoiceChangesOnlyDraftAndKeepsGraphicsQuality()
        {
            var owner = new GameObject("Dual UI test");
            try {
                var sdk=owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart=false;
                var view=HumanVisionSettingsView.Create(owner.transform);
                var configurePlatform=typeof(HumanVisionSettingsView).GetMethod("ConfigureExecutionPlatform",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic);
                configurePlatform?.Invoke(view,new object[]{RuntimePlatform.Android});
                var controller=owner.AddComponent<HumanVisionSettingsController>(); controller.Configure(sdk,view); view.Bind(controller);
                controller.Edit(() => new HumanVisionSettingsData { InputQuality=ModelInputQuality.High });
                var choice=view.GetComponentsInChildren<UnityEngine.UI.Dropdown>(true).Single(item => item.name=="AccelerationChoice");
                CollectionAssert.AreEqual(new[] { "NCNN Vulkan","RK3588 NPU","CPU" },choice.options.Select(item=>item.text).ToArray());
                choice.value=1; Assert.AreEqual(HumanVisionAccelerationMode.Neural,controller.Draft.AccelerationMode);
                Assert.AreEqual(ModelInputQuality.Low,controller.Draft.InputQuality); Assert.IsNull(controller.Active); Assert.IsNull(controller.Saved); Assert.IsFalse(sdk.IsInitialized);
                choice.value=2; Assert.AreEqual((HumanVisionAccelerationMode)2,controller.Draft.AccelerationMode);
                Assert.AreEqual(ModelInputQuality.High,controller.Draft.GraphicsInputQuality);
                choice.value=0; Assert.AreEqual(ModelInputQuality.High,controller.Draft.InputQuality);
            } finally { UnityEngine.Object.DestroyImmediate(owner); }
        }
        [Test]
        public void MissingNeuralAssetsOfferNoInventedChoice()
        {
            string root=Path.Combine(Path.GetTempPath(),"MissingNeural-"+Guid.NewGuid()); Directory.CreateDirectory(root);
            try {
                var values=HumanVisionSdkQualityCapabilities.Load(root,"android-rknn-npu-quality-low");
                Assert.IsEmpty(values.Choices); Assert.IsNotEmpty(values.Error);
                Assert.Throws<FileNotFoundException>(()=>HumanVisionNeuralModelContract.Validate(root));
            } finally { Directory.Delete(root); }
        }
        [Test]
        public void MigratingLegacyViewPlacesAccelerationBesideQualityAndPreservesProjectDiagnostics()
        {
            var owner = new GameObject("Legacy settings migration");
            try {
                var view = HumanVisionSettingsView.Create(owner.transform);
                var serialized = new UnityEditor.SerializedObject(view);
                var hint = (UnityEngine.UI.Text)serialized.FindProperty("qualityHint").objectReferenceValue;
                var capture = serialized.FindProperty("captureChoice").objectReferenceValue;
                var old = view.GetComponentsInChildren<UnityEngine.UI.Dropdown>(true).Single(item => item.name == "AccelerationChoice");
                var label = old.transform.parent.GetChild(old.transform.GetSiblingIndex() - 1);
                UnityEngine.Object.DestroyImmediate(label.gameObject); UnityEngine.Object.DestroyImmediate(old.gameObject);
                var custom = new GameObject("Project diagnostics", typeof(RectTransform), typeof(UnityEngine.UI.Text));
                custom.transform.SetParent(hint.transform.parent, false);
                var preview = view.Preview; var overlay = view.Overlay;
                view.EnsureAccelerationControl(); view.EnsureAccelerationControl();
                var choice = view.GetComponentsInChildren<UnityEngine.UI.Dropdown>(true).Single(item => item.name == "AccelerationChoice");
                Assert.That(choice.transform.GetSiblingIndex(), Is.LessThan(hint.transform.GetSiblingIndex()));
                Assert.AreSame(preview, view.Preview); Assert.AreSame(overlay, view.Overlay);
                serialized.Update(); Assert.AreSame(capture, serialized.FindProperty("captureChoice").objectReferenceValue);
                Assert.That(custom.transform.parent, Is.SameAs(hint.transform.parent));
                Assert.That(custom.name, Is.EqualTo("Project diagnostics"));
            } finally { UnityEngine.Object.DestroyImmediate(owner); }
        }
        [Test]
        public void StagedInitializationRunsOffCallerAndLateCancellationReleasesExactlyOnce()
        {
            int caller=Thread.CurrentThread.ManagedThreadId,worker=caller;
            using(var release=new ManualResetEventSlim()) {
                var resource=new OwnedResource();
                var pending=new HumanVisionStagedInitialization<OwnedResource>(()=> { worker=Thread.CurrentThread.ManagedThreadId; release.Wait(); return resource; });
                pending.Abandon(); release.Set();
                Assert.True(SpinWait.SpinUntil(()=>Volatile.Read(ref resource.Disposals)==1,5000));
                Assert.AreNotEqual(caller,worker); Assert.IsNull(pending.Take()); pending.Abandon(); Assert.AreEqual(1,resource.Disposals);
            }
        }
        [Test]
        public void StagedFailureExposesDriverErrorAndSuccessfulPromotionTransfersOwnership()
        {
            var failed=new HumanVisionStagedInitialization<OwnedResource>(()=>throw new InvalidOperationException("driver rejects model"));
            Assert.True(SpinWait.SpinUntil(()=>failed.Complete,5000)); Assert.AreEqual("driver rejects model",failed.Error); Assert.IsNull(failed.Take()); failed.Abandon();
            var resource=new OwnedResource(); var success=new HumanVisionStagedInitialization<OwnedResource>(()=>resource);
            Assert.True(SpinWait.SpinUntil(()=>success.Complete,5000)); Assert.AreSame(resource,success.Take()); Assert.IsNull(success.Take()); success.Abandon(); Assert.AreEqual(0,resource.Disposals);
            resource.Dispose(); Assert.AreEqual(1,resource.Disposals);
        }
        [Test]
        public void CancellingCompletedCandidateBeforePromotionReleasesItExactlyOnce()
        {
            int caller = Thread.CurrentThread.ManagedThreadId;
            for (int iteration = 0; iteration < 5; ++iteration) {
                var resource = new OwnedResource();
                var pending = new HumanVisionStagedInitialization<OwnedResource>(() => resource);
                Assert.True(SpinWait.SpinUntil(() => pending.Complete, 5000));
                pending.Abandon(); pending.Abandon();
                Assert.True(SpinWait.SpinUntil(() => Volatile.Read(ref resource.Disposals) == 1, 5000));
                Assert.AreNotEqual(caller, resource.DisposalThread, "Even completed candidates must release on a worker.");
                Assert.IsNull(pending.Take());
            }
        }
        [Test] public void WindowsExecutionChoiceKeepsCpuGpuIndependentOfAndroidNeuralPreference()
        {
            var owner = new GameObject("PC execution choice");
            string saved = Path.Combine(Path.GetTempPath(), "pc-compute-" + Guid.NewGuid() + ".json");
            try {
                var sdk = owner.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
                var view = HumanVisionSettingsView.Create(owner.transform);
                var controller = owner.AddComponent<HumanVisionSettingsController>(); controller.Configure(sdk, view); view.Bind(controller);
                controller.Edit(() => new HumanVisionSettingsData { UseWindowsCpu = true, InputQuality = ModelInputQuality.High });
                var choice = view.GetComponentsInChildren<UnityEngine.UI.Dropdown>(true).Single(item => item.name == "AccelerationChoice");
                CollectionAssert.AreEqual(new[] { "GPU", "CPU" }, choice.options.Select(item => item.text));
                Assert.That(choice.value, Is.EqualTo(1)); choice.value = 0;
                Assert.False(controller.Draft.UseWindowsCpu); Assert.That(controller.Draft.AccelerationMode, Is.EqualTo(HumanVisionAccelerationMode.Graphics));
                Assert.That(controller.Draft.InputQuality, Is.EqualTo(ModelInputQuality.High));
                HumanVisionSdkSettingsStore.Save(saved, controller.Draft);
                var loaded = HumanVisionSdkSettingsStore.Load(saved);
                Assert.False(loaded.ToOptions().UseWindowsCpu);
                Assert.That(new SharedRecognitionSettings { UseWindowsCpu = loaded.UseWindowsCpu }.RuntimeProfileFor(RuntimePlatform.WindowsPlayer), Is.EqualTo("windows-pc-directml"));
                choice.value = 1; Assert.True(controller.Draft.UseWindowsCpu);
                Assert.That(new SharedRecognitionSettings { UseWindowsCpu = controller.Draft.UseWindowsCpu }.RuntimeProfileFor(RuntimePlatform.WindowsPlayer), Is.EqualTo("windows-pc-cpu"));
                Assert.IsNull(controller.Active); Assert.IsNull(controller.Saved); Assert.False(sdk.IsInitialized);
            } finally { if (File.Exists(saved)) File.Delete(saved); UnityEngine.Object.DestroyImmediate(owner); }
        }
        private sealed class OwnedResource:IDisposable {
            internal int Disposals, DisposalThread;
            public void Dispose() { DisposalThread = Thread.CurrentThread.ManagedThreadId; Interlocked.Increment(ref Disposals); }
        }
        [Test] public void AndroidCpuIsExplicitlyAdmittedOnAnySocWithoutGraphicsOrNeuralFallback()
        {
            Assert.AreEqual("android-cpu-nohands", HumanVisionAndroidAccelerationSelection.Resolve((HumanVisionAccelerationMode)2,
                ModelInputQuality.High,"android-ncnn-vulkan","android-dual-vulkan-npu",Profiles,"SM8350","qcom"));
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidAccelerationSelection.Resolve((HumanVisionAccelerationMode)2,
                ModelInputQuality.Medium,"android-ncnn-vulkan","android-dual-vulkan-npu",Profiles.Take(4).ToArray(),"rk3588","rockchip"));
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("android-ort-cpu",
                "android-ncnn-vulkan","android-dual-vulkan-npu",Profiles));
        }
        [Test] public void DualBuildRequiresCpuAssetsAndAuditedRuntimeClosure()
        {
            var environment = new AndroidBuildEnvironment { MinimumApiLevel=26, Arm64Only=true, Il2Cpp=true, VulkanAvailable=true,
                VulkanFirst=true, HasHumanVisionLibrary=true, HasNcnnLibrary=true, HasBridgeSymbolManifest=true, HasProfile=true,
                HasNcnnModelPackAssets=true, HasNcnnModelPackSha256Index=true, HasNeuralRuntimeAudit=true, HasNeuralModelPackAssets=true };
            var issues = HumanVisionAndroidRuntimeBuildValidator.Validate(HumanVisionAndroidRuntimeModeRegistry.Resolve("android-dual-vulkan-npu"),environment);
            Assert.That(issues, Has.Some.Property("Code").EqualTo("HasCpuRuntimeAudit"));
            Assert.That(issues, Has.Some.Property("Code").EqualTo("HasCpuModelPackAssets"));
        }
    }
}
