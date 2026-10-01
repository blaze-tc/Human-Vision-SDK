using System.Collections.Generic;
using HumanVision.Demo.PC;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;
using UnityEngine.Video;

namespace HumanVision.Demo.Editor
{
    public static class HumanVisionPcDemoBuilder
    {
        [MenuItem("HumanVision/Create PC Demo")]
        public static void CreatePcDemo() { CreateScene(); }

        public static string CreateScene()
        {
            if (!AssetDatabase.IsValidFolder("Assets/Scenes")) AssetDatabase.CreateFolder("Assets", "Scenes");
            string path = AssetDatabase.GenerateUniqueAssetPath("Assets/Scenes/HumanVisionPcDemo.unity");
            // Unity 2021 refuses a second untitled scene beside an unsaved one.
            // Import an empty named scene first; opening it preserves the user's scene.
            System.IO.File.WriteAllText(path, "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!29 &1\nOcclusionCullingSettings:\n  m_ObjectHideFlags: 0\n  serializedVersion: 2\n  m_OcclusionBakeSettings:\n    smallestOccluder: 5\n    smallestHole: 0.25\n    backfaceThreshold: 100\n  m_SceneGUID: 00000000000000000000000000000000\n  m_OcclusionCullingData: {fileID: 0}\n");
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport);
            Scene scene = EditorSceneManager.OpenScene(path, OpenSceneMode.Additive);
            SceneManager.SetActiveScene(scene);
            var cameraObject = new GameObject("PC Demo Camera", typeof(Camera));
            var camera = cameraObject.GetComponent<Camera>();
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(.015f, .02f, .03f);
            camera.cullingMask = 0;

            var pipeline = new GameObject("HumanVision PC Demo");
            var manager = pipeline.AddComponent<HumanVisionManager>();
            pipeline.AddComponent<VideoPlayer>();
            var source = pipeline.AddComponent<VideoPlayerFrameSource>();
            var capture = new SerializedObject(source);
            capture.FindProperty("useRealtimeVideoTimestamps").boolValue = true;
            capture.ApplyModifiedPropertiesWithoutUndo();
            var live = pipeline.AddComponent<HumanVisionLiveSource>();
            live.autoRotateScreen = false;
            var demo = pipeline.AddComponent<HumanVisionPcDemo>();

            var canvasObject = new GameObject("PC Demo Canvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler));
            canvasObject.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = canvasObject.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920, 1080);
            scaler.matchWidthOrHeight = .5f;
            var viewport = new GameObject("Viewport", typeof(RectTransform), typeof(Image));
            viewport.transform.SetParent(canvasObject.transform, false);
            Stretch(viewport.GetComponent<RectTransform>());
            viewport.GetComponent<Image>().color = camera.backgroundColor;
            viewport.GetComponent<Image>().raycastTarget = false;
            var videoObject = new GameObject("Video Preview", typeof(RectTransform), typeof(RawImage), typeof(AspectRatioFitter));
            videoObject.transform.SetParent(viewport.transform, false);
            Stretch(videoObject.GetComponent<RectTransform>());
            var image = videoObject.GetComponent<RawImage>();
            image.raycastTarget = false;
            var fit = videoObject.GetComponent<AspectRatioFitter>();
            fit.aspectMode = AspectRatioFitter.AspectMode.FitInParent; fit.aspectRatio = 16f / 9f;
            var overlayObject = new GameObject("Skeleton Overlay", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionOverlay));
            overlayObject.transform.SetParent(viewport.transform, false);
            Stretch(overlayObject.GetComponent<RectTransform>());
            var overlay = overlayObject.GetComponent<HumanVisionOverlay>();
            var drawing = new SerializedObject(overlay);
            drawing.FindProperty("boneThickness").floatValue = 3;
            drawing.FindProperty("jointSize").floatValue = 5;
            drawing.ApplyModifiedPropertiesWithoutUndo();
            source.Configure(manager, image, fit);
            overlay.Configure(manager, source);
            demo.Configure(manager, source, live, image, overlay);
            if (!EditorSceneManager.SaveScene(scene, path)) throw new System.IO.IOException("Could not save PC Demo scene: " + path);
            var scenes = new List<EditorBuildSettingsScene>(EditorBuildSettings.scenes);
            scenes.Add(new EditorBuildSettingsScene(path, true));
            EditorBuildSettings.scenes = scenes.ToArray();
            Selection.activeGameObject = pipeline;
            Debug.Log("PC Demo created at " + path + ". Press Play, enter a local video path, then Start. DirectML is explicit; CPU is an optional selection.");
            return path;
        }

        [MenuItem("HumanVision/Choose PC Demo Video File")]
        public static void ChooseVideo()
        {
            HumanVisionPcDemo demo = Selection.activeGameObject != null ? Selection.activeGameObject.GetComponent<HumanVisionPcDemo>() : null;
            if (demo == null) demo = Object.FindObjectOfType<HumanVisionPcDemo>();
            if (demo == null) { Debug.LogError("Create a PC Demo scene first."); return; }
            string path = EditorUtility.OpenFilePanel("Choose local video", "", "");
            if (string.IsNullOrEmpty(path)) return;
            var settings = new SerializedObject(demo);
            settings.FindProperty("videoPath").stringValue = path;
            settings.ApplyModifiedProperties();
            EditorUtility.SetDirty(demo);
        }

        private static void Stretch(RectTransform rect)
        {
            rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero; rect.offsetMax = Vector2.zero;
        }
    }
}
