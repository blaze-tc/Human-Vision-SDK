#if UNITY_EDITOR_WIN
using System;
using System.Diagnostics;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;

// Stage under an Editor folder in the independent acceptance project only.
internal static class RuntimePrefabOriginalSourceNativeReload
{
    private const string Project = "E:/UnityProject/Human-Vision-SDK-Test";
    private const string RequestRelative = "Builds/OriginalSourceNativeReload/request.json";
    private const string Menu = "HumanVision/Acceptance/Cold Reload Original Source Native Pair";
    [Serializable] private sealed class Request
    {
        public int version;
        public string projectRoot, powerShellExe, helperScript, statusPath;
    }
    [Serializable] private sealed class Status
    {
        public string phase, token, message;
        public int editorPid;
    }
    private static Process helper;
    private static string statusPath, token;
    private static double deadline;
    private static int editorPid;

    private static string Full(string path) => Path.GetFullPath(path).TrimEnd('\\', '/');
    private static bool Same(string a, string b) => string.Equals(Full(a), Full(b), StringComparison.OrdinalIgnoreCase);
    private static string Quoted(string value)
    {
        if (string.IsNullOrEmpty(value) || value.IndexOf('"') >= 0) throw new ArgumentException("Invalid helper argument.");
        return "\"" + value + "\"";
    }
    private static void RequireStopped()
    {
        if (!Same(Directory.GetParent(Application.dataPath).FullName, Project))
            throw new InvalidOperationException("Only Human-Vision-SDK-Test is supported.");
        if (EditorApplication.isPlayingOrWillChangePlaymode || EditorApplication.isCompiling ||
            EditorApplication.isUpdating || BuildPipeline.isBuildingPlayer)
            throw new InvalidOperationException("Stop Play Mode and wait for compilation, importing and builds to finish.");
        for (int i = 0; i < SceneManager.sceneCount; ++i) {
            var scene = SceneManager.GetSceneAt(i);
            if (scene.isLoaded && scene.isDirty) throw new InvalidOperationException("Save the loaded scenes before cold reload.");
        }
    }
    [MenuItem(Menu)] private static void StartReload()
    {
        if (helper != null) { UnityEngine.Debug.LogError("A native reload helper is already pending."); return; }
        try {
            RequireStopped();
            string requestPath = Path.Combine(Project, RequestRelative);
            var request = JsonUtility.FromJson<Request>(File.ReadAllText(requestPath));
            if (request == null || request.version != 1 || !Same(request.projectRoot, Project))
                throw new InvalidOperationException("Invalid native reload request.");
            statusPath = Full(request.statusPath);
            if (!statusPath.StartsWith(Full(Path.Combine(Project, "Builds")) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("The helper status must remain under this project's Builds folder.");
            if (!Path.IsPathRooted(request.helperScript) || !Path.IsPathRooted(request.powerShellExe) ||
                !File.Exists(request.helperScript) || !File.Exists(request.powerShellExe))
                throw new InvalidOperationException("The request needs existing absolute helper and pwsh paths.");
            token = Guid.NewGuid().ToString("N");
            editorPid = Process.GetCurrentProcess().Id;
            helper = Process.Start(new ProcessStartInfo {
                FileName = request.powerShellExe,
                Arguments = "-NoLogo -NoProfile -File " + Quoted(request.helperScript) +
                    " -RequestPath " + Quoted(requestPath) + " -EditorPid " + editorPid +
                    " -UnityExe " + Quoted(EditorApplication.applicationPath) + " -Token " + token,
                UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                WorkingDirectory = Project
            });
            if (helper == null) throw new InvalidOperationException("The reload helper did not start.");
            deadline = EditorApplication.timeSinceStartup + 45;
            EditorApplication.update += Poll;
            UnityEngine.Debug.Log("Native reload helper started; the Editor stays open until validation and backups are ready.");
        } catch (Exception error) { StopPolling(); UnityEngine.Debug.LogError("Native reload did not start: " + error.Message); }
    }
    private static void Poll()
    {
        try {
            Status status = null;
            try { if (File.Exists(statusPath)) status = JsonUtility.FromJson<Status>(File.ReadAllText(statusPath)); }
            catch (IOException) { }
            catch (ArgumentException) { }
            if (status != null && status.token == token && status.editorPid == editorPid) {
                if (status.phase == "failed") throw new InvalidOperationException(status.message);
                if (status.phase == "ready") {
                    RequireStopped(); // Recheck immediately before the only exit call.
                    File.WriteAllText(statusPath + ".commit." + token, "The invoking Editor explicitly committed this cold reload.");
                    StopPolling(true);
                    UnityEngine.Debug.Log("Native pair validated and backed up. Closing for cold reload.");
                    EditorApplication.Exit(0);
                    return;
                }
            }
            if (helper == null || helper.HasExited) throw new InvalidOperationException("The helper exited before its ready handshake; the Editor remains open.");
            if (EditorApplication.timeSinceStartup > deadline) throw new TimeoutException("No verified ready handshake; the Editor remains open. The helper is not force-killed.");
        } catch (Exception error) { StopPolling(); UnityEngine.Debug.LogError("Native reload cancelled: " + error.Message); }
    }
    private static void StopPolling(bool plannedExit = false)
    {
        EditorApplication.update -= Poll;
        if (!plannedExit && !string.IsNullOrEmpty(statusPath) && !string.IsNullOrEmpty(token)) {
            try { File.WriteAllText(statusPath + ".cancel." + token, "The invoking Editor cancelled this cold reload."); }
            catch (Exception error) { UnityEngine.Debug.LogError("Could not write reload cancellation: " + error.Message + ". No exit commitment was issued."); }
        }
        helper?.Dispose(); helper = null;
    }
}
#endif
