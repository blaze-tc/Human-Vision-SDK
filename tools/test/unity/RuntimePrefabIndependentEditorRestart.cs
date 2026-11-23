using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;

// Only the owned acceptance project can consume this explicit restart request.
[InitializeOnLoad]
public static class RuntimePrefabIndependentEditorRestart
{
    static RuntimePrefabIndependentEditorRestart() { EditorApplication.update += Poll; }
    private static void Poll()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode || EditorApplication.isCompiling || EditorApplication.isUpdating || BuildPipeline.isBuildingPlayer) return;
        string root = Path.GetDirectoryName(Application.dataPath);
        if (Path.GetFileName(root) != "Human-Vision-SDK-Test") return;
        string request = Path.Combine(root, "Library/HumanVisionIndependentRestart.request");
        if (!File.Exists(request)) return;
        string operation = File.ReadAllText(request).Trim();
        File.Delete(request);
        string status = Path.Combine(root, "Library/HumanVisionIndependentRestart.status");
        if (operation != "quit") { File.WriteAllText(status, "Rejected unknown restart operation"); return; }
        for (int i = 0; i < SceneManager.sceneCount; i++) if (SceneManager.GetSceneAt(i).isDirty) {
            File.WriteAllText(status, "Blocked: a scene contains unsaved changes"); return;
        }
        File.WriteAllText(status, "Quitting clean acceptance Editor");
        EditorApplication.Exit(0);
    }
}
