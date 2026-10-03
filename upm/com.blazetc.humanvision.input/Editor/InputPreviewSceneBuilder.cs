using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace HumanVision.Input.Editor
{
    public static class InputPreviewSceneBuilder
    {
        [MenuItem("HumanVision/Input/Create standalone preview")]
        public static void CreateScene()
        {
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            // Samples are imported explicitly so their controller compiles outside Samples~.
            var type = System.Type.GetType("InputPreviewController, Assembly-CSharp");
            if (type == null) throw new System.InvalidOperationException("Import Human Vision Input > InputPreview from Package Manager first.");
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            new GameObject("Standalone Input Preview").AddComponent(type);
            Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, "Assets/Scenes/InputPreview.unity");
        }
    }
}
