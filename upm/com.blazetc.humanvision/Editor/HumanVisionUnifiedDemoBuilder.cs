using System.IO;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace HumanVision.Editor
{
    public static class HumanVisionUnifiedDemoBuilder
    {
        [MenuItem("HumanVision/Create unified Camera, Video and RTSP demos")]
        public static void CreateScenes()
        {
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            BuildScenes();
        }
        [MenuItem("HumanVision/Create unified demos in dedicated folder")]
        public static void CreateDedicatedScenes()
        {
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            BuildScenes("Assets/HumanVisionUnifiedDemo");
        }
        public static void BuildScenes() => BuildScenes("Assets/Scenes");
        public static void BuildScenes(string outputFolder)
        {
            if (string.IsNullOrEmpty(outputFolder) || !outputFolder.StartsWith("Assets/") || outputFolder.Contains(".."))
                throw new System.ArgumentException("Demo output folder must be contained under Assets.");
            Directory.CreateDirectory(outputFolder); AssetDatabase.Refresh();
            var panel = new GameObject("Shared Recognition Settings", typeof(RectTransform), typeof(HumanVisionSharedSettingsPanel));
            var prefab = PrefabUtility.SaveAsPrefabAsset(panel, outputFolder + "/SharedSettingsPanel.prefab"); Object.DestroyImmediate(panel);
            var scenes = new System.Collections.Generic.List<EditorBuildSettingsScene>();
            if (outputFolder != "Assets/Scenes")
                foreach (var existing in EditorBuildSettings.scenes)
                    if (File.Exists(existing.path) && !existing.path.StartsWith(outputFolder + "/")) scenes.Add(existing);
            foreach (InputKind kind in System.Enum.GetValues(typeof(InputKind))) {
                var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
                var go = new GameObject("HumanVision " + kind);
                go.AddComponent<HumanVisionManager>(); go.AddComponent<VideoPlayerFrameSource>();
                go.AddComponent<HumanVisionLiveSource>();
                var facade = go.AddComponent<HumanVisionCameraManager>(); facade.enabled = false;
                go.AddComponent<HumanVisionRegionSettingsUI>(); go.AddComponent<InputPreviewControls>();
                var navigation = go.AddComponent<HumanVisionDemoNavigator>(); navigation.Kind = kind; navigation.SharedSettingsPrefab = prefab;
                var camera = new GameObject("Presentation Camera", typeof(Camera)).GetComponent<Camera>(); camera.clearFlags = CameraClearFlags.SolidColor; camera.backgroundColor = Color.black;
                string name = kind == InputKind.WebCamera ? "HumanVisionCameraDemo" : kind == InputKind.Video ? "HumanVisionVideoDemo" : "HumanVisionRtspDemo";
                string path = outputFolder + "/" + name + ".unity";
                EditorSceneManager.SaveScene(scene, path); scenes.Add(new EditorBuildSettingsScene(path, true));
            }
            EditorBuildSettings.scenes = scenes.ToArray(); AssetDatabase.SaveAssets();
        }
    }
}
