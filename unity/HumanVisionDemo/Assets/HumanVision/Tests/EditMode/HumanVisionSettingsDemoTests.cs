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
            var builder = typeof(HumanVision.Editor.HumanVisionSdkMenu).Assembly.GetType("HumanVision.Editor.HumanVisionSettingsDemoBuilder");
            var generate = builder.GetMethod("GenerateAssetsAt"); Assert.NotNull(generate, "Untitled-scene preservation API is missing.");
            var original = UnityEngine.SceneManagement.SceneManager.GetActiveScene();
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
            }
        }
    }
}
