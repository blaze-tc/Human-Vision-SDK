using System.IO;
using HumanVision.Demo;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanVision.Editor
{
    public static class HumanVisionCameraDemoBuilder
    {
        [MenuItem("HumanVision/Create Live Camera Demo")]
        public static void CreateScene() { CreatePair(false); }
        [MenuItem("HumanVision/Create Camera Settings Scene")]
        public static void CreateSettingsScene() { CreatePair(true); }
        private static void CreatePair(bool openSettings)
        {
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            if (!AssetDatabase.IsValidFolder("Assets/Scenes")) AssetDatabase.CreateFolder("Assets", "Scenes");
            string live = AssetDatabase.GenerateUniqueAssetPath("Assets/Scenes/HumanVisionCameraDemo.unity");
            string settings = AssetDatabase.GenerateUniqueAssetPath("Assets/Scenes/HumanVisionCameraSettings.unity");
            Build(live, settings, false);
            Build(settings, live, true);
            var scenes = new System.Collections.Generic.List<EditorBuildSettingsScene>(EditorBuildSettings.scenes);
            scenes.Add(new EditorBuildSettingsScene(live, true)); scenes.Add(new EditorBuildSettingsScene(settings, true));
            EditorBuildSettings.scenes = scenes.ToArray();
            EditorSceneManager.OpenScene(openSettings ? settings : live);
        }
        private static void Build(string path, string other, bool settings)
        {
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var root = new GameObject("HumanVision Live SDK");
            var camera = new GameObject("Presentation Camera", typeof(Camera)).GetComponent<Camera>();
            camera.transform.SetParent(root.transform);
            camera.clearFlags = CameraClearFlags.SolidColor; camera.backgroundColor = Color.black;
            camera.orthographic = true; camera.orthographicSize = 5;
            var pipeline = new GameObject("Camera Pipeline"); pipeline.transform.SetParent(root.transform);
            var manager = pipeline.AddComponent<HumanVisionManager>();
            var bridge = pipeline.AddComponent<VideoPlayerFrameSource>();
            pipeline.AddComponent<HumanVisionLiveSource>();
            var facade = pipeline.AddComponent<HumanVisionCameraManager>(); facade.startAutomatically = true;
            var canvasObject = new GameObject("Camera Image Canvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler));
            canvasObject.transform.SetParent(root.transform);
            var canvas = canvasObject.GetComponent<Canvas>(); canvas.renderMode = RenderMode.ScreenSpaceCamera;
            canvas.worldCamera = camera; canvas.planeDistance = 2;
            var scaler = canvasObject.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize; scaler.referenceResolution = new Vector2(1920, 1080);
            var imageObject = new GameObject("Camera Image (independent)", typeof(RectTransform), typeof(RawImage), typeof(AspectRatioFitter));
            imageObject.transform.SetParent(canvasObject.transform, false); Stretch(imageObject.GetComponent<RectTransform>());
            var image = imageObject.GetComponent<RawImage>(); image.raycastTarget = false;
            var fit = imageObject.GetComponent<AspectRatioFitter>(); fit.aspectMode = AspectRatioFitter.AspectMode.FitInParent; fit.aspectRatio = 16f / 9;
            bridge.Configure(manager, image, fit);
            var skeleton = new GameObject("Skeleton Objects (independent)").AddComponent<HumanVisionSkeletonOverlayer>();
            skeleton.transform.SetParent(root.transform); skeleton.manager = facade; skeleton.preview = image; skeleton.foregroundCamera = camera;
            if (settings) { var ui = pipeline.AddComponent<HumanVisionRegionSettingsUI>(); ui.manager = facade; ui.preview = image; }
            else { var gesture = pipeline.AddComponent<HumanVisionRaisedHandDetector>(); gesture.manager = facade; }
            var navigation = pipeline.AddComponent<HumanVisionSceneControls>(); navigation.manager = facade; navigation.preview = image;
            navigation.settingsScene = settings; navigation.targetScene = Path.GetFileNameWithoutExtension(other);
            new GameObject("EventSystem", typeof(EventSystem), typeof(StandaloneInputModule)).transform.SetParent(root.transform);
            EditorSceneManager.SaveScene(scene, path);
        }
        private static void Stretch(RectTransform rect)
        {
            rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one; rect.offsetMin = rect.offsetMax = Vector2.zero;
        }
    }
}
