using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.SceneManagement;
using UnityEngine.UI;
using UnityEngine.Video;

namespace HumanVision.Demo.Editor
{
    public static class HumanVisionDemoSceneBuilder
    {
        public const string ScenePath = "Assets/Scenes/HumanVisionD04Demo.unity";

        [MenuItem("HumanVision/Build D0.4 Demo Scene")]
        public static void BuildScene()
        {
            EnsureScenesFolder();

            Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            HumanVisionGpuDemoSetup.EnsurePresentationCamera();

            var pipeline = new GameObject("HumanVision Pipeline");
            HumanVisionManager manager = pipeline.AddComponent<HumanVisionManager>();
            pipeline.AddComponent<VideoPlayer>();
            VideoPlayerFrameSource frameSource = pipeline.AddComponent<VideoPlayerFrameSource>();
            HumanVisionHud hud = pipeline.AddComponent<HumanVisionHud>();
            HumanVisionDemoBootstrap bootstrap = pipeline.AddComponent<HumanVisionDemoBootstrap>();

            var canvasObject = new GameObject(
                "HumanVision Canvas",
                typeof(RectTransform),
                typeof(Canvas),
                typeof(CanvasScaler),
                typeof(GraphicRaycaster));
            Canvas canvas = canvasObject.GetComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;

            CanvasScaler scaler = canvasObject.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920f, 1080f);
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;

            var viewportObject = new GameObject(
                "Video Viewport",
                typeof(RectTransform),
                typeof(Image));
            viewportObject.transform.SetParent(canvasObject.transform, false);
            SetStretch(viewportObject.GetComponent<RectTransform>());
            Image viewportBackground = viewportObject.GetComponent<Image>();
            viewportBackground.color = new Color(0.015f, 0.02f, 0.03f, 1f);
            viewportBackground.raycastTarget = false;

            var videoObject = new GameObject(
                "Video Surface",
                typeof(RectTransform),
                typeof(RawImage),
                typeof(AspectRatioFitter));
            videoObject.transform.SetParent(viewportObject.transform, false);
            SetStretch(videoObject.GetComponent<RectTransform>());
            RawImage rawImage = videoObject.GetComponent<RawImage>();
            rawImage.color = Color.white;
            rawImage.raycastTarget = false;
            AspectRatioFitter aspectRatioFitter = videoObject.GetComponent<AspectRatioFitter>();
            aspectRatioFitter.aspectMode = AspectRatioFitter.AspectMode.FitInParent;
            aspectRatioFitter.aspectRatio = 16f / 9f;

            var overlayObject = new GameObject(
                "HumanVision Overlay",
                typeof(RectTransform),
                typeof(CanvasRenderer),
                typeof(HumanVisionOverlay));
            overlayObject.transform.SetParent(viewportObject.transform, false);
            SetStretch(overlayObject.GetComponent<RectTransform>());
            HumanVisionOverlay overlay = overlayObject.GetComponent<HumanVisionOverlay>();

            var eventSystemObject = new GameObject(
                "EventSystem",
                typeof(EventSystem),
                typeof(StandaloneInputModule));
            eventSystemObject.transform.SetAsLastSibling();

            frameSource.Configure(manager, rawImage, aspectRatioFitter);
            overlay.Configure(manager, frameSource);
            hud.Configure(
                manager,
                frameSource,
                "HumanVision/Media/d0_3_one_person.mp4",
                "HumanVision/Media/d0_3_two_people.mp4");
            bootstrap.Configure(manager, frameSource);

            EditorSceneManager.SaveScene(scene, ScenePath);
            EnableSceneForBuild(ScenePath);
            AssetDatabase.SaveAssets();
            Selection.activeGameObject = pipeline;
            Debug.Log("HumanVision D0.4 Demo scene generated at " + ScenePath);
        }

        private static void EnsureScenesFolder()
        {
            if (!AssetDatabase.IsValidFolder("Assets/Scenes"))
            {
                AssetDatabase.CreateFolder("Assets", "Scenes");
            }
        }

        private static void EnableSceneForBuild(string scenePath)
        {
            List<EditorBuildSettingsScene> scenes = EditorBuildSettings.scenes.ToList();
            int existingIndex = scenes.FindIndex(scene => scene.path == scenePath);
            var enabledScene = new EditorBuildSettingsScene(scenePath, true);
            if (existingIndex >= 0)
            {
                scenes[existingIndex] = enabledScene;
            }
            else
            {
                scenes.Add(enabledScene);
            }

            EditorBuildSettings.scenes = scenes.ToArray();
        }

        private static void SetStretch(RectTransform transform)
        {
            transform.anchorMin = Vector2.zero;
            transform.anchorMax = Vector2.one;
            transform.offsetMin = Vector2.zero;
            transform.offsetMax = Vector2.zero;
        }
    }
}
