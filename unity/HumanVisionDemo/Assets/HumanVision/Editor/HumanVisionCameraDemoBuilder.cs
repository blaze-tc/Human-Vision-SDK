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
        public static void CreateScene()
        {
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var root = new GameObject("HumanVision Live SDK");
            var camera = new GameObject("Presentation Camera", typeof(Camera)).GetComponent<Camera>();
            camera.transform.SetParent(root.transform);
            camera.clearFlags = CameraClearFlags.SolidColor; camera.backgroundColor = Color.black; camera.cullingMask = 0;
            var pipeline = new GameObject("Camera Pipeline"); pipeline.transform.SetParent(root.transform);
            var manager = pipeline.AddComponent<HumanVisionManager>();
            var bridge = pipeline.AddComponent<VideoPlayerFrameSource>();
            pipeline.AddComponent<HumanVisionLiveSource>();
            var facade = pipeline.AddComponent<HumanVisionCameraManager>();
            var canvasObject = new GameObject("Camera Canvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            canvasObject.transform.SetParent(root.transform);
            canvasObject.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = canvasObject.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize; scaler.referenceResolution = new Vector2(1920, 1080);
            var imageObject = new GameObject("Camera Image", typeof(RectTransform), typeof(RawImage), typeof(AspectRatioFitter));
            imageObject.transform.SetParent(canvasObject.transform, false);
            Stretch(imageObject.GetComponent<RectTransform>());
            var image = imageObject.GetComponent<RawImage>(); image.raycastTarget = false;
            var fit = imageObject.GetComponent<AspectRatioFitter>(); fit.aspectMode = AspectRatioFitter.AspectMode.FitInParent; fit.aspectRatio = 16f / 9;
            var overlayObject = new GameObject("Skeleton Overlay", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionOverlay));
            overlayObject.transform.SetParent(canvasObject.transform, false); Stretch(overlayObject.GetComponent<RectTransform>());
            bridge.Configure(manager, image, fit);
            overlayObject.GetComponent<HumanVisionOverlay>().Configure(manager, bridge);
            var ui = pipeline.AddComponent<HumanVisionRegionSettingsUI>(); ui.manager = facade; ui.preview = image;
            new GameObject("EventSystem", typeof(EventSystem), typeof(StandaloneInputModule)).transform.SetParent(root.transform);
            if (!AssetDatabase.IsValidFolder("Assets/Scenes")) AssetDatabase.CreateFolder("Assets", "Scenes");
            // A new unique scene preserves any previous configured camera demo.
            string path = AssetDatabase.GenerateUniqueAssetPath("Assets/Scenes/HumanVisionCameraDemo.unity");
            EditorSceneManager.SaveScene(scene, path);
            var scenes = new System.Collections.Generic.List<EditorBuildSettingsScene>(EditorBuildSettings.scenes);
            scenes.Add(new EditorBuildSettingsScene(path, true));
            EditorBuildSettings.scenes = scenes.ToArray();
            Selection.activeGameObject = pipeline;
        }
        private static void Stretch(RectTransform rect)
        {
            rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one; rect.offsetMin = rect.offsetMax = Vector2.zero;
        }
    }
}
