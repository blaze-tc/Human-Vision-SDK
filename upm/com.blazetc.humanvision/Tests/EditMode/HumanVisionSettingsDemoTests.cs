using System;
using System.IO;
using System.Reflection;
using System.Linq;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSettingsDemoTests
    {
        private Type Find(string name) => typeof(HumanVisionSdk).Assembly.GetType("HumanVision.Demo." + name);
        private GameObject root;
        [TearDown] public void Cleanup() { if (root != null) UnityEngine.Object.DestroyImmediate(root); }
        [Test] public void LayoutIsEditableUguiWithReferenceProportionsAndAllControls()
        {
            var type = Find("HumanVisionSettingsView"); Assert.NotNull(type, "Settings view is missing.");
            root = new GameObject("settings test");
            var view = (Component)type.GetMethod("Create").Invoke(null, new object[] { root.transform, null });
            Assert.That(view.GetComponent<CanvasScaler>().referenceResolution, Is.EqualTo(new Vector2(1600, 900)));
            Assert.That(view.transform.Find("Settings panel").GetComponent<RectTransform>().anchorMin.x, Is.EqualTo(.72f));
            foreach (string name in new[] { "Apply", "ApplySave", "Stop", "Return", "UseRegions", "EditRegions", "ResetRegions", "Reload", "Save", "Mirror", "Advanced" })
                Assert.True(Array.Exists(view.GetComponentsInChildren<Button>(true), b => b.name == name), name);
            Assert.That(view.GetComponentsInChildren<InputField>(true).Length, Is.GreaterThanOrEqualTo(12));
            Assert.That(root.GetComponentsInChildren<EventSystem>(true).Length, Is.EqualTo(1));
            Assert.That(view.GetComponentsInChildren<HumanVisionOverlay>(true)[0].GetComponent<CanvasRenderer>(), Is.Not.Null);
            Assert.That(view.GetComponentsInChildren<Dropdown>(true).Length, Is.GreaterThanOrEqualTo(4));
        }
        [Test] public void SettingsPreserveThreeModesAndCloneRecognitionRegions()
        {
            var type = Find("HumanVisionSettingsData"); Assert.NotNull(type);
            dynamic data = Activator.CreateInstance(type);
            data.Video.VideoPath = "first.mp4"; data.Rtsp.RtspUrl = "rtsp://localhost/live";
            dynamic clone = data.Clone(); clone.Video.VideoPath = "second.mp4";
            Assert.That((string)data.Video.VideoPath, Is.EqualTo("first.mp4"));
            Assert.That((string)clone.Rtsp.RtspUrl, Is.EqualTo("rtsp://localhost/live"));
            Assert.False(ReferenceEquals(data.Recognition.Regions, clone.Recognition.Regions));
        }
        [Test] public void StoreRejectsInvalidDraftAndKeepsCorruptOriginal()
        {
            var type = Find("HumanVisionSdkSettingsStore"); Assert.NotNull(type);
            string dir = Path.Combine(Path.GetTempPath(), "HumanVisionSettingsTest-" + Guid.NewGuid()); Directory.CreateDirectory(dir);
            string path = Path.Combine(dir, "settings.json");
            try {
                dynamic data = Activator.CreateInstance(Find("HumanVisionSettingsData"));
                type.GetMethod("Save").Invoke(null, new object[] { path, data });
                string saved = File.ReadAllText(path); data.Recognition.MaxBodies = 9;
                Assert.Throws<TargetInvocationException>(() => type.GetMethod("Save").Invoke(null, new object[] { path, data }));
                Assert.That(File.ReadAllText(path), Is.EqualTo(saved));
                File.WriteAllText(path, "broken original");
                Assert.Throws<TargetInvocationException>(() => type.GetMethod("Load").Invoke(null, new object[] { path }));
                Assert.That(File.ReadAllText(path), Is.EqualTo("broken original"));
            } finally { Directory.Delete(dir, true); }
        }
        [Test] public void RegionGeometryUsesTopLeftCoordinatesAndClampsDraft()
        {
            var type = Find("HumanVisionSettingsRegionHandle"); Assert.NotNull(type);
            root = new GameObject("region parent", typeof(RectTransform));
            var go = new GameObject("region", typeof(RectTransform), typeof(Image)); go.transform.SetParent(root.transform, false);
            var handle = go.AddComponent(type); type.GetMethod("SetRegion").Invoke(handle, new object[] { new Rect(.2f, .1f, .3f, .4f), true });
            Assert.That(((RectTransform)go.transform).anchorMin, Is.EqualTo(new Vector2(.2f, .5f)));
            Assert.That(((RectTransform)go.transform).anchorMax, Is.EqualTo(new Vector2(.5f, .9f)));
            var moved = (Rect)type.GetMethod("MoveOrResize").Invoke(null, new object[] { new Rect(.2f,.1f,.3f,.4f), new Vector2(10,10), false });
            Assert.That(moved.xMax, Is.EqualTo(1).Within(.00001)); Assert.That(moved.yMax, Is.EqualTo(1).Within(.00001));
        }
        [Test] public void ModePanelsAndBusyStateFollowDraftWithoutApplyingRuntime()
        {
            root = new GameObject("settings interaction"); var sdk = root.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            var view = HumanVisionSettingsView.Create(root.transform); var controller = root.AddComponent<HumanVisionSettingsController>();
            controller.Configure(sdk, view); view.Bind(controller); view.ShowDraft(new HumanVisionSettingsData());
            controller.Execute("Video");
            Assert.True(view.transform.Find("Settings panel").GetComponentsInChildren<Transform>(true).Any(t => t.name == "Video source options" && t.gameObject.activeSelf));
            controller.Execute("RTSP");
            Assert.That(view.Draft.SourceKind, Is.EqualTo(HumanVision.Input.InputKind.Rtsp));
            Assert.False(sdk.IsInitialized); Assert.That(controller.Active, Is.Null);
            view.SetBusy(true);
            foreach (var button in view.GetComponentsInChildren<Button>(true)) Assert.That(button.interactable, Is.EqualTo(button.name == "Stop"));
        }
        [Test] public void InvalidUiFieldDoesNotReplaceDraftOrInitializeSdk()
        {
            root = new GameObject("invalid settings"); var sdk = root.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            var view = HumanVisionSettingsView.Create(root.transform); var controller = root.AddComponent<HumanVisionSettingsController>(); controller.Configure(sdk, view);
            view.Bind(controller); view.ShowDraft(new HumanVisionSettingsData());
            Array.Find(view.GetComponentsInChildren<InputField>(true), f => f.name == "Width").text = "NaN";
            controller.Execute("Mirror"); Assert.False(view.Draft.Camera.Mirror); Assert.False(sdk.IsInitialized); Assert.Null(controller.Active);
        }
        [Test] public void BuilderPreservesUntitledSceneAndSavesPrefabReferences()
        {
            string folder = "Assets/SdkSettingsTest-" + Guid.NewGuid().ToString("N");
            var builder = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.Editor.HumanVisionSettingsDemoBuilder")).FirstOrDefault(t => t != null);
            var generate = builder.GetMethod("GenerateAssetsAt"); Assert.NotNull(generate, "Untitled-scene preservation API is missing.");
            var original = UnityEngine.SceneManagement.SceneManager.GetActiveScene();
            string originalPath = original.path;
            root = new GameObject("Must survive generation");
            try {
                generate.Invoke(null, new object[] { folder });
                Assert.True(root != null); Assert.That(UnityEngine.SceneManagement.SceneManager.GetActiveScene(), Is.EqualTo(original));
                var prefab = UnityEditor.AssetDatabase.LoadAssetAtPath<GameObject>(folder + "/HumanVisionSettingsDemo.prefab");
                Assert.NotNull(prefab); var controller = prefab.GetComponent<HumanVisionSettingsController>();
                Assert.NotNull(controller.Sdk); Assert.NotNull(controller.View);
                Assert.NotNull(UnityEditor.AssetDatabase.LoadAssetAtPath<UnityEditor.SceneAsset>(folder + "/HumanVisionSettingsDemo.unity"));
            } finally {
                // 只删除本测试 UUID 目录；保留之前的场景对象。
                UnityEditor.AssetDatabase.DeleteAsset(folder);
                if (string.IsNullOrEmpty(originalPath)) { UnityEngine.Object.DestroyImmediate(root); root = null; UnityEditor.SceneManagement.EditorSceneManager.NewScene(UnityEditor.SceneManagement.NewSceneSetup.EmptyScene, UnityEditor.SceneManagement.NewSceneMode.Single); }
            }
        }
        [Test] public void MissingSaveUsesSdkInspectorDefaults()
        {
            root = new GameObject("Inspector defaults"); var sdk = root.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            var options = sdk.Configuration; options.SourceKind = HumanVision.Input.InputKind.Video; options.Input.VideoPath = "scene-default.mp4";
            options.Recognition.MaxBodies = 2; options.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(2);
            typeof(HumanVisionSdk).GetField("options", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(sdk, options);
            var view = HumanVisionSettingsView.Create(root.transform); var controller = root.AddComponent<HumanVisionSettingsController>(); controller.Configure(sdk, view);
            typeof(HumanVisionSettingsController).GetMethod("Reload", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(controller, new object[] { System.IO.Path.Combine(System.IO.Path.GetTempPath(), System.Guid.NewGuid() + ".json") });
            Assert.That(controller.Draft.SourceKind, Is.EqualTo(HumanVision.Input.InputKind.Video));
            Assert.That(controller.Draft.Video.VideoPath, Is.EqualTo("scene-default.mp4")); Assert.That(controller.Draft.Recognition.MaxBodies, Is.EqualTo(2));
        }
        [Test] public void PublishedTwoArgumentTextureSubmissionOverloadIsPreserved()
        {
            Assert.NotNull(typeof(VideoPlayerFrameSource).GetMethod("SubmitExternalTexture", new[] { typeof(Texture), typeof(long) }), "Compiled clients need the exact published signature.");
        }
        [Test] public void FixedPcQualityExplainsPlatformWithoutReadingAndroidModels()
        {
            var type = typeof(HumanVisionSdk).Assembly.GetType("HumanVision.HumanVisionSdkQualityCapabilities");
            Assert.NotNull(type, "Quality availability must explain the actual platform contract.");
            dynamic value = type.GetMethod("Load").Invoke(null, new object[] { "not-an-installed-root", "windows-pc-cpu" });
            Assert.That((string)value.Message, Does.Contain("Windows").And.Contain("Android"));
            Assert.That(((Array)value.Choices).Length, Is.Zero);
            Assert.False((bool)value.Selectable);
        }
        [Test] public void InstalledAndroidQualitiesAreAvailableBeforeNativeInitialization()
        {
            var type = typeof(HumanVisionSdk).Assembly.GetType("HumanVision.HumanVisionSdkQualityCapabilities"); Assert.NotNull(type);
            string runtime = Path.Combine(Application.streamingAssetsPath, "HumanVision/Runtime");
            Assert.True(File.Exists(Path.Combine(runtime, "model-input-qualities.json")), "Use the actual release quality fixture.");
            dynamic value = type.GetMethod("Load").Invoke(null, new object[] { runtime, "android-ncnn-vulkan" });
            var choices = (ModelInputQualityChoice[])value.Choices;
            CollectionAssert.AreEqual(new[] { ModelInputQuality.Low, ModelInputQuality.Medium, ModelInputQuality.High }, choices.Select(c => c.Quality));
            CollectionAssert.AreEqual(new[] { 512, 640, 960 }, choices.Select(c => c.Width));
            Assert.True((bool)value.Selectable);
            Assert.NotNull(typeof(HumanVisionSdk).GetMethod("PrepareInputQualities"));
        }
        private object MakeLogger(string directory)
        {
            var type = Find("HumanVisionSettingsLogger"); var constructor = type.GetConstructor(new[] { typeof(string) });
            Assert.NotNull(constructor, "Diagnostics need an isolated destination for verification.");
            return constructor.Invoke(new object[] { directory });
        }
        private string Logs(string directory) => string.Join("\n", Directory.GetFiles(directory, "*.log", SearchOption.AllDirectories).Select(File.ReadAllText));
        [Test] public void LoggerKeepsStartupFailureContextAndStackBeforeSuccessfulApply()
        {
            string directory = Path.Combine(Path.GetTempPath(), "HumanVisionDiagnosticTest-" + Guid.NewGuid()); object logger = null;
            try {
                logger = MakeLogger(directory); var type = logger.GetType();
                type.GetMethod("Configure").Invoke(logger, new object[] { new HumanVisionSettingsData() });
                var record = type.GetMethod("Record"); Assert.NotNull(record);
                record.Invoke(logger, new object[] { "apply.failed", "rtsp://operator:secret@camera/live?password=othersecret", "at Initialize() line 42" });
                string text = Logs(directory);
                Assert.That(text, Does.Contain("session.start").And.Contain("unity=").And.Contain("device=").And.Contain("apply.failed").And.Contain("line 42"));
                Assert.That(text, Does.Not.Contain("operator:secret").And.Not.Contain("othersecret"));
                string zip = (string)type.GetMethod("Export").Invoke(logger, null); Assert.True(File.Exists(zip)); File.Delete(zip);
            } finally { (logger as IDisposable)?.Dispose(); if (Directory.Exists(directory)) Directory.Delete(directory, true); }
        }
        [Test] public void ControllerLogsRejectedConfigurationWithExceptionStack()
        {
            string directory = Path.Combine(Path.GetTempPath(), "HumanVisionDiagnosticTest-" + Guid.NewGuid()); object logger = null;
            try {
                logger = MakeLogger(directory); logger.GetType().GetMethod("Configure").Invoke(logger, new object[] { new HumanVisionSettingsData() });
                root = new GameObject("failed draft diagnostics"); var sdk = root.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
                var view = HumanVisionSettingsView.Create(root.transform); var controller = root.AddComponent<HumanVisionSettingsController>(); controller.Configure(sdk, view);
                view.Bind(controller); view.ShowDraft(new HumanVisionSettingsData());
                typeof(HumanVisionSettingsController).GetField("logger", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(controller, logger);
                controller.Edit(() => throw new InvalidOperationException("invalid test configuration"));
                Assert.Null(controller.Active); Assert.False(sdk.IsInitialized);
                Assert.That(Logs(directory), Does.Contain("configuration.rejected").And.Contain("invalid test configuration").And.Contain("HumanVisionSettingsDemoTests"));
            } finally { (logger as IDisposable)?.Dispose(); if (Directory.Exists(directory)) Directory.Delete(directory, true); }
        }
        [Test] public void DiagnosticRedactionIncludesSecureRtspAndQueryCredentials()
        {
            string redacted = HumanVisionSettingsController.Redact("rtsps://u:p@host/live?token=secret&password=hidden rtsp://a:b@camera/stream");
            Assert.That(redacted, Does.Not.Contain("u:p").And.Not.Contain("a:b").And.Not.Contain("secret").And.Not.Contain("hidden"));
            Assert.That(redacted, Does.Contain("host/live").And.Contain("camera/stream"));
        }
        [Test] public void StartupLogsKeepPreviousSessionsUntilSavedRetentionPolicyLoads()
        {
            string directory = Path.Combine(Path.GetTempPath(), "HumanVisionDiagnosticTest-" + Guid.NewGuid()); object logger = null;
            try {
                for (int i = 1; i <= 6; i++) Directory.CreateDirectory(Path.Combine(directory, "20200101-000000-000-" + i.ToString("x8")));
                logger = MakeLogger(directory); var start = logger.GetType().GetMethod("BeginStartup"); Assert.NotNull(start);
                start.Invoke(logger, null); Assert.That(Directory.GetDirectories(directory).Length, Is.EqualTo(7));
                logger.GetType().GetMethod("ApplySettings").Invoke(logger, new object[] { new HumanVisionSettingsData { RetainedLogSessions = 20 }, true });
                Assert.That(Directory.GetDirectories(directory).Length, Is.EqualTo(7));
            } finally { (logger as IDisposable)?.Dispose(); if (Directory.Exists(directory)) Directory.Delete(directory, true); }
        }
        [Test] public void LogInitializationIoFailureRemainsVisibleWithoutThrowing()
        {
            string directory = Path.Combine(Path.GetTempPath(), "HumanVisionDiagnosticTest-" + Guid.NewGuid()); object logger = null;
            Directory.CreateDirectory(directory); string occupied = Path.Combine(directory, "occupied"); File.WriteAllText(occupied, "keep this file");
            try {
                logger = MakeLogger(occupied);
                Assert.DoesNotThrow(() => logger.GetType().GetMethod("Configure").Invoke(logger, new object[] { new HumanVisionSettingsData() }));
                Assert.That((string)logger.GetType().GetProperty("LastWriteError").GetValue(logger), Is.Not.Empty);
                Assert.That(File.ReadAllText(occupied), Is.EqualTo("keep this file"));
            } finally { (logger as IDisposable)?.Dispose(); Directory.Delete(directory, true); }
        }
        [Test] public void DiagnosticRedactionCoversCompoundTokenParameterNames()
        {
            string redacted = HumanVisionSettingsController.Redact("rtsp://host/live?access_token=first-secret&refresh_token=second-secret&auth_token=third-secret");
            Assert.That(redacted, Does.Not.Contain("first-secret").And.Not.Contain("second-secret").And.Not.Contain("third-secret"));
        }
        [Test] public void EveryRotatedLogRetainsEnvironmentAndStaysBounded()
        {
            string directory = Path.Combine(Path.GetTempPath(), "HumanVisionDiagnosticTest-" + Guid.NewGuid()); object logger = null;
            try {
                logger = MakeLogger(directory); var type = logger.GetType(); type.GetMethod("Configure").Invoke(logger, new object[] { new HumanVisionSettingsData { LogFileMegabytes = 1 } });
                for (int i = 0; i < 300; i++) type.GetMethod("Record").Invoke(logger, new object[] { "rotation.test", i + new string('x', 20000), null });
                var files = Directory.GetFiles(directory, "*.log", SearchOption.AllDirectories); Assert.That(files.Length, Is.LessThanOrEqualTo(4));
                foreach (string file in files) { Assert.That(new FileInfo(file).Length, Is.LessThanOrEqualTo(1024 * 1024)); string text = File.ReadAllText(file); Assert.True(text.Contains("session.start") && text.Contains("device="), "Rotated log lost its environment header: " + file); }
            } finally { (logger as IDisposable)?.Dispose(); if (Directory.Exists(directory)) Directory.Delete(directory, true); }
        }
    }
}
