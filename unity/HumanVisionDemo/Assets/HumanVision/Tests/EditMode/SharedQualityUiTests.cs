using System;
using System.IO;
using System.Linq;
using System.Reflection;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class SharedQualityUiTests
    {
        private string root;
        private GameObject owner;
        [SetUp] public void Setup() { root = Path.Combine(Path.GetTempPath(), "HV-Q3-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root); }
        [TearDown] public void Cleanup() { if (owner != null) UnityEngine.Object.DestroyImmediate(owner); Directory.Delete(root, true); }
        private static FieldInfo QualityField() { var field = typeof(SharedRecognitionSettings).GetField("InputQuality"); Assert.NotNull(field, "Persisted shared quality is missing"); return field; }
        private static ModelInputQuality Quality(SharedRecognitionSettings value) => (ModelInputQuality)QualityField().GetValue(value);
        private static void SetQuality(SharedRecognitionSettings value, ModelInputQuality quality) => QualityField().SetValue(value, quality);
        private static AnalysisContract Resolve(SharedRecognitionSettings value, string root, string profile)
        {
            var method = typeof(SharedRecognitionSettings).GetMethod("ResolveContract"); Assert.NotNull(method, "Quality contract preflight is missing");
            try { return (AnalysisContract)method.Invoke(value, new object[] { root, profile }); } catch (TargetInvocationException error) { throw error.InnerException; }
        }
        private void CopyApproved()
        {
            string source = Path.Combine(Application.dataPath, "..", "ApprovedQualities"); Assert.IsTrue(Directory.Exists(source));
            foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories)) { string target = Path.Combine(root, file.Substring(source.Length + 1)); Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(file, target); }
        }
        [Test] public void LegacyMissingFieldUsesMediumAndPreservesRegions()
        {
            var legacy = new SharedRecognitionSettings { MaxBodies = 2, UseRegions = true, Regions = new[] { new Rect(.1f, .2f, .3f, .4f), new Rect(.5f, .2f, .3f, .4f) } };
            string json = System.Text.RegularExpressions.Regex.Replace(JsonUtility.ToJson(legacy), ",?\"InputQuality\":\\d+", "");
            File.WriteAllText(Path.Combine(root, "shared.json"), json);
            var value = new HumanVisionSettingsStore(root).LoadShared();
            Assert.That(Quality(value), Is.EqualTo(ModelInputQuality.Medium)); Assert.That(value.MaxBodies, Is.EqualTo(2));
            Assert.That(value.Regions[0], Is.EqualTo(new Rect(.1f, .2f, .3f, .4f)));
        }
        [Test] public void InvalidSavedEnumIsRejected()
        {
            File.WriteAllText(Path.Combine(root, "shared.json"), "{\"InputQuality\":73,\"Version\":1,\"MaxBodies\":4,\"AnalysisProfileId\":\"cpu\",\"ModelPackId\":\"pose\",\"PoseWidth\":192,\"PoseHeight\":256,\"DetectionCadence\":1}");
            Assert.Throws<ArgumentException>(() => new HumanVisionSettingsStore(root).LoadShared());
        }
        [Test] public void QualityIsSharedAcrossThreeModesWithoutChangingCaptureOrStyle()
        {
            var store = new HumanVisionSettingsStore(root); var value = new SharedRecognitionSettings(); SetQuality(value, ModelInputQuality.High); store.SaveShared(value);
            foreach (InputKind kind in Enum.GetValues(typeof(InputKind))) {
                var mode = new DemoModeSettings { RequestedWidth = 1920, RequestedHeight = 1080, Mirror = true, LineWidth = 12, PointDiameter = 31, RtspUrl = "rtsp://manual.example/live" };
                store.SaveMode(kind, mode);
                Assert.That(Quality(store.LoadShared()), Is.EqualTo(ModelInputQuality.High));
                var saved = store.LoadMode(kind); Assert.That(saved.RequestedWidth, Is.EqualTo(1920)); Assert.That(saved.Mirror, Is.True); Assert.That(saved.PointDiameter, Is.EqualTo(31)); Assert.That(saved.RtspUrl, Is.EqualTo("rtsp://manual.example/live"));
            }
        }
        [TestCase(ModelInputQuality.High, "android-ncnn-vulkan-quality-high", 960, 576)]
        [TestCase(ModelInputQuality.Medium, "android-ncnn-vulkan", 640, 384)]
        [TestCase(ModelInputQuality.Low, "android-ncnn-vulkan-quality-low", 512, 288)]
        public void SelectedQualityResolvesRealValidatedPack(ModelInputQuality quality, string profile, int width, int height)
        {
            CopyApproved(); var value = new SharedRecognitionSettings { MaxBodies = 8 }; SetQuality(value, quality);
            var contract = Resolve(value, root, "android-ncnn-vulkan"); Assert.That(contract.ProfileId, Is.EqualTo(profile)); Assert.That(contract.PoseWidth, Is.EqualTo(width)); Assert.That(contract.PoseHeight, Is.EqualTo(height));
            Assert.That(Quality(value), Is.EqualTo(quality)); Assert.That(value.AnalysisProfileId, Is.EqualTo("android-ncnn-vulkan"), "Preflight must not claim that the draft is already active");
        }
        [Test] public void PcRetainsItsActualContractAndSavedAndroidChoice()
        {
            CopyApproved(); var value = new SharedRecognitionSettings { UseWindowsCpu = true }; SetQuality(value, ModelInputQuality.High);
            var contract = Resolve(value, root, "windows-pc-cpu"); Assert.That(contract.ProfileId, Is.EqualTo("windows-pc-cpu")); Assert.That(Quality(value), Is.EqualTo(ModelInputQuality.High));
        }
        [Test] public void MissingQualityCatalogRejectsHighWithoutChangingDraft()
        {
            var value = new SharedRecognitionSettings(); SetQuality(value, ModelInputQuality.High);
            Assert.Throws<InvalidOperationException>(() => Resolve(value, root, "android-ncnn-vulkan")); Assert.That(Quality(value), Is.EqualTo(ModelInputQuality.High));
        }
        [Test] public void TamperedOrMissingPackRejectsBeforeContractResolution()
        {
            CopyApproved(); var value = new SharedRecognitionSettings(); SetQuality(value, ModelInputQuality.High);
            string pack = Directory.GetFiles(Path.Combine(root, "modelpacks"), "modelpack.json", SearchOption.AllDirectories).Single(path => path.Contains("960x576"));
            File.Delete(pack); Assert.Throws<InvalidDataException>(() => Resolve(value, root, "android-ncnn-vulkan"));
        }
        private HumanVisionDemoNavigator Navigator(InputKind kind)
        {
            owner = new GameObject("Real Demo UI", typeof(RectTransform)); var nav = owner.AddComponent<HumanVisionDemoNavigator>(); nav.Kind = kind;
            SetProperty(nav, "Shared", new SharedRecognitionSettings()); SetProperty(nav, "Mode", new DemoModeSettings()); SetProperty(nav, "Input", owner.AddComponent<InputPreviewControls>()); SetProperty(nav, "Overlay", owner.AddComponent<HumanVisionOverlay>()); return nav;
        }
        private static void SetProperty(object target, string name, object value) => target.GetType().GetProperty(name).SetValue(target, value);
        private static void SetField(object target, string name, object value) => target.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(target, value);
        private sealed class RecordingSession : IHumanVisionSession
        {
            public int Disposals; public int MaxBodies => 4; public HumanVisionBody[] Bodies => null; public int BodyCount => 0; public long ResultSequence => 12; public long SourceFrameId => 12; public long SourceTimestampUs => 0; public HumanVisionStats Stats => default;
            public void Dispose() { ++Disposals; } public bool PollLatestResult() => false; public void RefreshStats() { } public void SetRegions(Rect[] regions, long revision) { } public void ReconfigureMaxBodies(int count) { }
            public bool CopyRegions(long sequence, int[] indices, out long revision) { revision = 0; return false; }
            public bool SubmitFrame(IntPtr data, int width, int height, int stride, HumanVisionPixelFormat format, long id, long timestamp, int bytes) => false;
        }
        [TestCase(ModelInputQuality.High)] [TestCase(ModelInputQuality.Low)]
        public void MissingCatalogHasExplicitMediumDraftRecoveryWithoutRetiringCurrentSession(ModelInputQuality saved)
        {
            Directory.CreateDirectory(Path.Combine(root, "profiles")); Directory.CreateDirectory(Path.Combine(root, "modelpacks", "legacy"));
            File.WriteAllText(Path.Combine(root, "profiles", "android-ncnn-vulkan.json"), "{\"profile\":\"android-ncnn-vulkan\",\"body\":{\"modelPack\":\"legacy\"}}");
            File.WriteAllText(Path.Combine(root, "modelpacks", "legacy", "manifest.json"), "{\"pack_id\":\"legacy\",\"models\":[{\"role\":\"body\",\"input_contract\":{\"width\":192,\"height\":256}}]}");
            var nav = Navigator(InputKind.Video); SetQuality(nav.Shared, saved); nav.Mode.RequestedWidth = 1920; nav.Mode.Mirror = true;
            var manager = owner.AddComponent<HumanVisionManager>(); var session = new RecordingSession(); SetField(manager, "_session", session); SetProperty(nav, "Manager", manager);
            var panel = owner.AddComponent<HumanVisionSharedSettingsPanel>(); panel.Build(nav, InputPreviewCanvas.Column(owner.transform));
            var draft = new SharedRecognitionSettings(); panel.ReadInto(draft); Assert.Throws<InvalidOperationException>(() => Resolve(draft, root, "android-ncnn-vulkan"));
            Assert.That(Quality(nav.Shared), Is.EqualTo(saved)); Assert.That(session.Disposals, Is.Zero);
            var reset = owner.GetComponentsInChildren<Button>().SingleOrDefault(button => button.name == "Reset quality draft to Medium (default)"); Assert.NotNull(reset, "Missing-catalog error must have a reachable explicit recovery action");
            reset.onClick.Invoke(); panel.ReadInto(draft);
            var contract = Resolve(draft, root, "android-ncnn-vulkan"); Assert.That(contract.PoseWidth, Is.EqualTo(192)); Assert.That(contract.PoseHeight, Is.EqualTo(256)); Assert.That(Quality(draft), Is.EqualTo(ModelInputQuality.Medium));
            Assert.That(Quality(nav.Shared), Is.EqualTo(saved), "Reset changes only the pending draft until Save/Apply"); Assert.That(session.Disposals, Is.Zero); Assert.That(nav.Mode.RequestedWidth, Is.EqualTo(1920)); Assert.That(nav.Mode.Mirror, Is.True);
        }
        private sealed class RecordingSource : IHumanVisionFrameSource
        {
            public int CloseCount;
            public InputSourceState State => InputSourceState.Streaming;
            public string LastError => "";
            public Texture CurrentTexture => null;
            public void Open(HumanVisionSourceSettings settings) { }
            public void Close() { ++CloseCount; }
            public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) { lease = default; return false; }
            public bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) { frame = default; return false; }
        }
        [Test] public void ActualApplyRejectsMissingContractBeforeSessionRetirementAndDraftMutation()
        {
            var nav = Navigator(InputKind.Video); nav.Shared.UseWindowsCpu = true;
            var manager = owner.AddComponent<HumanVisionManager>(); var session = new RecordingSession(); SetField(manager, "_session", session); SetProperty(nav, "Manager", manager);
            var bridge = owner.AddComponent<VideoPlayerFrameSource>(); SetProperty(nav, "Bridge", bridge);
            var source = new RecordingSource(); SetProperty(nav.Input, "Source", source); bridge.BindUnifiedSource(source);
            SetField(nav, "regionFacade", owner.AddComponent<HumanVisionCameraManager>()); SetField(nav, "runtimeRoot", root);
            var panel = owner.AddComponent<HumanVisionSharedSettingsPanel>(); nav.SharedPanel = panel; panel.Build(nav, InputPreviewCanvas.Column(owner.transform));
            owner.GetComponentsInChildren<InputField>().Single(field => field.name == "People (1–8)").text = "8";
            owner.GetComponentsInChildren<Button>().Single(button => button.name == "Apply shared settings").onClick.Invoke();
            Assert.That(session.Disposals, Is.Zero, "Contract must be validated before manager Shutdown"); Assert.That(manager.ResultSequence, Is.EqualTo(12)); Assert.That(nav.Shared.MaxBodies, Is.EqualTo(4), "Failed Apply must retain the saved draft");
            Assert.That(nav.Input.Source, Is.SameAs(source)); Assert.That(source.CloseCount, Is.Zero);
            var adapter = owner.GetComponent<HumanVisionInputAdapter>(); Assert.That(typeof(HumanVisionInputAdapter).GetField("source", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(adapter), Is.SameAs(source), "Preflight rejection must leave the recognition consumer attached");
            Assert.That(nav.Status, Does.Contain("windows-pc-cpu"));
        }
        [Test] public void SavePersistsDraftWithoutChangingActiveRegionsOrContract()
        {
            CopyApproved(); var nav = Navigator(InputKind.Video); SetField(nav, "store", new HumanVisionSettingsStore(Path.Combine(root, "settings")));
            SetField(nav, "regionFacade", owner.AddComponent<HumanVisionCameraManager>());
            var contract = AnalysisContract.Load(root, "android-ncnn-vulkan", 4); SetProperty(nav, "Contract", contract); contract.ApplyTo(nav.Shared);
            SetProperty(nav, "ActiveShared", nav.Shared.Clone());
            var panel = owner.AddComponent<HumanVisionSharedSettingsPanel>(); nav.SharedPanel = panel; panel.Build(nav, InputPreviewCanvas.Column(owner.transform));
            var refresh = panel.GetType().GetMethod("RefreshQualityChoices"); Assert.NotNull(refresh); refresh.Invoke(panel, new object[] { HumanVisionModelInputQualities.Load(root).ChoicesForMode("android-ncnn-vulkan") });
            owner.GetComponentsInChildren<Button>().Single(button => button.name.StartsWith("High 960x576")).onClick.Invoke();
            owner.GetComponentsInChildren<Button>().Single(button => button.name == "Save all settings").onClick.Invoke();
            Assert.That(Quality(new HumanVisionSettingsStore(Path.Combine(root, "settings")).LoadShared()), Is.EqualTo(ModelInputQuality.High));
            Assert.That(nav.Contract.PoseWidth, Is.EqualTo(640)); Assert.That(nav.Status, Does.Contain("draft"));
            Assert.That(Quality(nav.ActiveShared), Is.EqualTo(ModelInputQuality.Medium));
        }
        [Test] public void RealQualityButtonChangesOnlyDraftAndClearSelectedState()
        {
            CopyApproved(); var nav = Navigator(InputKind.Video); var panel = owner.AddComponent<HumanVisionSharedSettingsPanel>(); panel.Build(nav, InputPreviewCanvas.Column(owner.transform));
            var refresh = panel.GetType().GetMethod("RefreshQualityChoices"); Assert.NotNull(refresh, "Runtime validated quality UI is missing"); refresh.Invoke(panel, new object[] { HumanVisionModelInputQualities.Load(root).ChoicesForMode("android-ncnn-vulkan") });
            var button = owner.GetComponentsInChildren<Button>().Single(item => item.name.StartsWith("High 960x576")); button.onClick.Invoke();
            Assert.That(Quality(nav.Shared), Is.EqualTo(ModelInputQuality.Medium), "Selection alone is not Apply");
            var draft = new SharedRecognitionSettings(); panel.ReadInto(draft); Assert.That(Quality(draft), Is.EqualTo(ModelInputQuality.High));
            Assert.That(button.GetComponentInChildren<Text>().text, Does.Contain("Selected")); Assert.That(nav.Mode.RequestedWidth, Is.EqualTo(1280));
        }
        private static Type HostType() { var type = typeof(HumanVisionDemoNavigator).Assembly.GetType("HumanVision.Demo.HumanVisionRtspComputerHost"); Assert.NotNull(type, "Build-computer RTSP host helper is missing"); return type; }
        private static string Url(string host, bool camera)
        {
            try { return (string)HostType().GetMethod("BuildUrl").Invoke(null, new object[] { host, camera }); } catch (TargetInvocationException error) { throw error.InnerException; }
        }
        [Test] public void PlayerHostUsesBakedComputerAndManualOverride()
        {
            var nav = Navigator(InputKind.Rtsp); var component = owner.AddComponent(HostType());
            HostType().GetMethod("Configure").Invoke(component, new object[] { "192.168.8.40" });
            Assert.That(HostType().GetMethod("Resolve").Invoke(null, new object[] { owner.scene, "" }), Is.EqualTo("192.168.8.40"));
            Assert.That(HostType().GetMethod("Resolve").Invoke(null, new object[] { owner.scene, "10.3.4.5" }), Is.EqualTo("10.3.4.5"));
        }
        private static Type HostEditorType() { var type = AppDomain.CurrentDomain.GetAssemblies().Select(assembly => assembly.GetType("HumanVision.Editor.HumanVisionRtspHostBuildProcessor")).FirstOrDefault(value => value != null); Assert.NotNull(type, "RTSP build scene baker is missing"); return type; }
        [TestCase("Wi-Fi", "Physical WLAN", true, "192.168.1.1", true)]
        [TestCase("Meta TUN", "VPN Tunnel", true, "192.168.1.1", false)]
        [TestCase("Ethernet", "Physical Ethernet", false, "192.168.1.1", false)]
        [TestCase("Ethernet", "Virtual Ethernet", true, "192.168.1.1", false)]
        [TestCase("Ethernet", "Physical Ethernet", true, "0.0.0.0", false)]
        public void EditorHostExcludesVirtualDownAndGatewaylessInterfaces(string name, string description, bool up, string gateway, bool expected)
        {
            var method = HostEditorType().GetMethod("IsEligibleInterface"); Assert.NotNull(method);
            Assert.That(method.Invoke(null, new object[] { System.Net.NetworkInformation.NetworkInterfaceType.Ethernet, name, description, up, gateway }), Is.EqualTo(expected));
        }
        [Test] public void BakeAddsSerializedComputerHostOnlyToOfficialTransientScene()
        {
            var scene = UnityEngine.SceneManagement.SceneManager.GetActiveScene(); string originalName = scene.name;
            Component host = null;
            try {
                var bake = HostEditorType().GetMethod("Bake"); Assert.NotNull(bake);
                scene.name = "UnrelatedScene"; int roots = scene.rootCount; bake.Invoke(null, new object[] { scene, "192.168.8.40" }); Assert.That(scene.rootCount, Is.EqualTo(roots));
                scene.name = "HumanVisionRtspDemo"; bake.Invoke(null, new object[] { scene, "192.168.8.40" });
                host = scene.GetRootGameObjects().SelectMany(go => go.GetComponents(HostType())).Single();
                Assert.That(HostType().GetProperty("ComputerHost").GetValue(host), Is.EqualTo("192.168.8.40"));
                bake.Invoke(null, new object[] { scene, "192.168.8.41" }); Assert.That(scene.rootCount, Is.EqualTo(roots + 1)); Assert.That(HostType().GetProperty("ComputerHost").GetValue(host), Is.EqualTo("192.168.8.41"));
            } finally { if (host != null) UnityEngine.Object.DestroyImmediate(host.gameObject); scene.name = originalName; }
        }
        [Test] public void BusyApplyRetryAndSceneSwitchKeepExistingSessionAndSourceSettings()
        {
            var nav = Navigator(InputKind.Video); SetField(nav, "initializing", true); var before = JsonUtility.ToJson(nav.Shared); var mode = JsonUtility.ToJson(nav.Mode);
            nav.ApplyShared(); nav.RetryRecognition(); nav.SwitchTo(InputKind.Rtsp);
            Assert.That(JsonUtility.ToJson(nav.Shared), Is.EqualTo(before)); Assert.That(JsonUtility.ToJson(nav.Mode), Is.EqualTo(mode)); Assert.That(nav.Kind, Is.EqualTo(InputKind.Video)); Assert.That(nav.Status, Does.Contain("initializing"));
        }
        [TestCase(1280, 720)] [TestCase(720, 1280)]
        public void SharedQualityButtonsAndRetryStayReachableInActualScrollLayout(int width, int height)
        {
            CopyApproved(); var nav = Navigator(InputKind.Rtsp);
            SetProperty(nav, "ActiveShared", nav.Shared); SetProperty(nav, "Manager", owner.AddComponent<HumanVisionManager>()); SetProperty(nav, "Bridge", owner.AddComponent<VideoPlayerFrameSource>());
            owner.AddComponent<HumanVisionCameraManager>();
            HumanVisionUnifiedDemoCanvas.Build(nav, out _, out _);
            var canvas = owner.GetComponentInChildren<Canvas>(); canvas.renderMode = RenderMode.WorldSpace; var canvasRect = (RectTransform)canvas.transform; canvasRect.sizeDelta = new Vector2(width, height);
            owner.GetComponentInChildren<InputSafeArea>().enabled = false;
            nav.SharedPanel.RefreshQualityChoices(HumanVisionModelInputQualities.Load(root).ChoicesForMode("android-ncnn-vulkan"));
            var scroll = owner.GetComponentInChildren<ScrollRect>();
            Canvas.ForceUpdateCanvases(); LayoutRebuilder.ForceRebuildLayoutImmediate(canvasRect); Canvas.ForceUpdateCanvases();
            foreach (var button in scroll.content.GetComponentsInChildren<Button>()) {
                Assert.That(((RectTransform)button.transform).rect.width, Is.GreaterThan(100), button.name); Assert.That(((RectTransform)button.transform).rect.height, Is.GreaterThanOrEqualTo(44), button.name);
            }
            var high = scroll.content.GetComponentsInChildren<Button>().Single(button => button.name.StartsWith("High 960x576"));
            var retry = scroll.content.GetComponentsInChildren<Button>().Single(button => button.name == "Retry recognition initialization");
            var shared = (RectTransform)nav.SharedPanel.transform;
            var bounds = RectTransformUtility.CalculateRelativeRectTransformBounds(shared, retry.transform);
            Assert.That(bounds.min.y, Is.GreaterThanOrEqualTo(shared.rect.yMin - 1), "All added controls must contribute to the shared wrapper preferred height");
            scroll.verticalNormalizedPosition = 0; Canvas.ForceUpdateCanvases();
            var bottom = RectTransformUtility.CalculateRelativeRectTransformBounds(scroll.viewport, retry.transform);
            Assert.That(bottom.min.y, Is.GreaterThanOrEqualTo(scroll.viewport.rect.yMin - 1)); Assert.That(bottom.max.y, Is.LessThanOrEqualTo(scroll.viewport.rect.yMax + 1));
            Assert.That(scroll.content.rect.height, Is.GreaterThan(scroll.viewport.rect.height));
        }
        [TestCase(true, "rtsp://192.168.8.40:554/videodevice")]
        [TestCase(false, "rtsp://192.168.8.40:554/video-1.mp4")]
        public void PresetUsesExactBuildComputerHostAndHappytimePath(bool camera, string expected) => Assert.That(Url("192.168.8.40", camera), Is.EqualTo(expected));
        [TestCase("")] [TestCase("127.0.0.1")] [TestCase("169.254.1.1")] [TestCase("198.18.0.1")] [TestCase("192.168.1.40:554")] [TestCase("192.168.1.40/live")] [TestCase("8.8.8.8")]
        public void InvalidPresetHostFailsExplicitly(string host) => Assert.Throws<ArgumentException>(() => Url(host, true));
        [TestCase(InputKind.Video)] [TestCase(InputKind.WebCamera)]
        public void PresetsAreAbsentFromOtherModes(InputKind kind)
        {
            var nav = Navigator(kind); owner.AddComponent<HumanVisionModeSettingsPanel>().Build(nav, InputPreviewCanvas.Column(owner.transform));
            Assert.That(owner.GetComponentsInChildren<Button>().Any(button => button.name.StartsWith("Computer ")), Is.False);
        }
        [Test] public void RtspButtonsUseHostOverridePreserveManualStartupAndRecognitionSettings()
        {
            var nav = Navigator(InputKind.Rtsp); nav.Mode.RtspUrl = "rtsp://manual.example/live";
            var host = nav.Mode.GetType().GetField("RtspComputerHost"); Assert.NotNull(host); host.SetValue(nav.Mode, "192.168.8.40");
            var panel = owner.AddComponent<HumanVisionModeSettingsPanel>(); nav.ModePanel = panel; panel.Build(nav, InputPreviewCanvas.Column(owner.transform));
            var url = owner.GetComponentsInChildren<InputField>().Single(field => field.name == "RTSP URL (H.264 / TCP)"); Assert.That(url.text, Is.EqualTo("rtsp://manual.example/live"));
            var before = JsonUtility.ToJson(nav.Shared); var camera = owner.GetComponentsInChildren<Button>().Single(button => button.name == "Computer camera"); camera.onClick.Invoke();
            Assert.That(url.text, Is.EqualTo("rtsp://192.168.8.40:554/videodevice")); Assert.That(nav.Mode.RtspUrl, Is.EqualTo(url.text)); Assert.That(JsonUtility.ToJson(nav.Shared), Is.EqualTo(before));
            Assert.That(nav.Input.Request.Location, Is.EqualTo(url.text)); Assert.That(nav.Input.Request.Kind, Is.EqualTo(InputKind.Rtsp));
            owner.GetComponentsInChildren<Button>().Single(button => button.name == "Computer video").onClick.Invoke(); Assert.That(url.text, Is.EqualTo("rtsp://192.168.8.40:554/video-1.mp4"));
        }
    }
}
