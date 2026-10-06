using System;
using System.Collections;
using System.IO;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Editor;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;

public static class SdkDocsProbe
{
    static IEnumerator preparation;
    static AsyncOperation waiting;
    static string prepared, error;
    static double deadline;
    static HumanVisionManager manager;
    public static void Run()
    {
        try
        {
            HumanVisionModelInstaller.Prepare();
            HumanVisionUnifiedDemoBuilder.BuildScenes("Assets/HumanVisionUnifiedDemo");
            Check(EditorBuildSettings.scenes.Length == 3, "Three generated demos");
            var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
            var canvas = new GameObject("VisionCanvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            canvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = canvas.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1280, 720);
            var area = new GameObject("PreviewArea", typeof(RectTransform));
            area.transform.SetParent(canvas.transform, false); Stretch(area.GetComponent<RectTransform>());
            var preview = new GameObject("Preview", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage), typeof(AspectRatioFitter));
            preview.transform.SetParent(area.transform, false);
            preview.GetComponent<AspectRatioFitter>().aspectMode = AspectRatioFitter.AspectMode.FitInParent;
            preview.GetComponent<AspectRatioFitter>().aspectRatio = 1280f / 720;
            var bones = new GameObject("SkeletonOverlay", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionOverlay));
            bones.transform.SetParent(preview.transform, false); Stretch(bones.GetComponent<RectTransform>());
            var root = new GameObject("VisionRoot");
            var quick = root.AddComponent<SdkCameraQuickStart>();
            root.AddComponent<SdkSkeletonReader>();
            quick.preview = preview.GetComponent<RawImage>();
            quick.previewFitter = preview.GetComponent<AspectRatioFitter>();
            quick.overlay = bones.GetComponent<HumanVisionOverlay>();
            var eventSystem = new GameObject("EventSystem", typeof(UnityEngine.EventSystems.EventSystem), typeof(UnityEngine.EventSystems.StandaloneInputModule));
            Directory.CreateDirectory("Assets/Scenes");
            Check(EditorSceneManager.SaveScene(scene, "Assets/Scenes/FirstVision.unity"), "Scene save");
            EditorSceneManager.OpenScene("Assets/Scenes/FirstVision.unity");
            quick = UnityEngine.Object.FindObjectOfType<SdkCameraQuickStart>();
            Check(quick != null && quick.preview != null && quick.previewFitter != null && quick.overlay != null, "Serialized UI references");
            Check(quick.overlay.GetComponent<CanvasRenderer>() != null && quick.overlay.transform.parent == quick.preview.transform, "Overlay geometry parent");
            manager = quick.GetComponent<HumanVisionManager>();
            Check(manager != null && quick.GetComponent<VideoPlayerFrameSource>() != null && quick.GetComponent<UnityEngine.Video.VideoPlayer>() != null, "Required SDK components");
            preparation = HumanVisionRuntimeData.Prepare(value => prepared = value, value => error = value);
            deadline = EditorApplication.timeSinceStartup + 90;
            EditorApplication.update += Tick;
        }
        catch (Exception e) { Finish(false, e.ToString()); }
    }
    static void Tick()
    {
        try
        {
            if (EditorApplication.timeSinceStartup > deadline) throw new TimeoutException("Prepare timeout");
            if (waiting != null && !waiting.isDone) return;
            waiting = null;
            while (preparation.MoveNext())
            {
                waiting = preparation.Current as AsyncOperation;
                if (waiting != null && !waiting.isDone) return;
            }
            EditorApplication.update -= Tick;
            Check(!string.IsNullOrEmpty(prepared), "Prepare: " + error);
            Check(manager.TryInitialize(new HumanVisionConfig { RuntimeRoot = prepared, Profile = "windows-pc-cpu", MaxBodies = 1 }), "Initialize: " + manager.LastError);
            Check(manager.IsInitialized && manager.ActiveRuntimeProfile == "windows-pc-cpu", "Active profile");
            manager.Shutdown(); Check(!manager.IsInitialized, "Shutdown");
            Finish(true, "Two examples compiled; three demos; manual scene persisted with references; Prepare; CPU initialization; Shutdown.");
        }
        catch (Exception e) { Finish(false, e.ToString()); }
    }
    static void Stretch(RectTransform rect) { rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one; rect.offsetMin = rect.offsetMax = Vector2.zero; }
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void Finish(bool passed, string message)
    {
        EditorApplication.update -= Tick;
        if (manager != null) manager.Shutdown();
        File.WriteAllText("docs-probe-result.json", JsonUtility.ToJson(new Result { passed = passed, message = message, unity = Application.unityVersion, hardware_test = false }, true));
        Debug.Log("SDK_DOCS_PROBE " + (passed ? "PASS " : "FAIL ") + message);
        EditorApplication.Exit(passed ? 0 : 1);
    }
    [Serializable] class Result { public bool passed; public string message, unity; public bool hardware_test; }
}
