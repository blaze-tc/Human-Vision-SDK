using System;
using System.Collections;
using System.IO;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Editor;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

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
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            SdkBasicUsageMenu.Create(new MenuCommand(null));
            var starter = Selection.activeGameObject.GetComponent<SdkBasicUsage>();
            Check(starter != null, "Hierarchy menu creates starter");
            Check(starter.GetComponent<HumanVisionManager>() != null &&
                starter.GetComponent<VideoPlayerFrameSource>() != null &&
                starter.GetComponent<UnityEngine.Video.VideoPlayer>() != null, "Required components");
            Check(UnityEngine.Object.FindObjectsOfType<Canvas>().Length == 0, "Starter has no Canvas requirement");
            Directory.CreateDirectory("Assets/Scenes");
            Check(EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), "Assets/Scenes/SdkBasicUsage.unity"), "Save starter");
            EditorSceneManager.OpenScene("Assets/Scenes/SdkBasicUsage.unity");
            starter = UnityEngine.Object.FindObjectOfType<SdkBasicUsage>();
            Check(starter != null, "Starter persists");
            manager = starter.GetComponent<HumanVisionManager>();
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
            Check(manager.BodyCount == 0 && manager.Bodies != null && manager.Bodies.Length >= 1, "Readable empty body result before input");
            manager.Shutdown(); Check(!manager.IsInitialized, "Shutdown");
            Finish(true, "Three runtime examples and optional Editor menu compile; menu creates one starter without Canvas; Prepare; CPU initialization; empty body access; Shutdown.");
        }
        catch (Exception e) { Finish(false, e.ToString()); }
    }
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
