using System;
using System.IO;
using System.Security.Cryptography;
using HumanVision;
using HumanVision.Editor;
using UnityEditor;
using UnityEngine;

// Runs only in an isolated import project, for either UPM or Assets installation.
public static class ReleaseSnapshotImportCheck
{
    [Serializable] private sealed class Index { public Entry[] files; }
    [Serializable] private sealed class Entry { public string path; public string sha256; }
    public static void Run()
    {
        try
        {
            HumanVisionModelInstaller.Prepare();
            AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
            string root = Path.Combine(Application.streamingAssetsPath, "HumanVision/Runtime");
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(root, "index.json")));
            if (index.files == null || index.files.Length != 20) throw new Exception("Missing release runtime closure.");
            foreach (var entry in index.files)
            {
                using (var sha = SHA256.Create())
                using (var input = File.OpenRead(Path.Combine(root, entry.path)))
                    if (BitConverter.ToString(sha.ComputeHash(input)).Replace("-", "").ToLowerInvariant() != entry.sha256)
                        throw new Exception("Installed runtime hash mismatch: " + entry.path);
            }
            HumanVisionUnifiedDemoBuilder.BuildScenes("Assets/ReleaseVerifiedDemos");
            if (EditorBuildSettings.scenes.Length < 3) throw new Exception("Three demos were not registered.");
            var settings = new HumanVision.Demo.DemoModeSettings();
            if (settings.LineWidth != 4.5f || settings.PointDiameter != 13.5f)
                throw new Exception("Skeleton defaults do not match accepted half thickness.");
            var obj = new GameObject("Release native initialization check");
            try
            {
                var manager = obj.AddComponent<HumanVisionManager>();
                if (!manager.TryInitialize(new HumanVisionConfig {
                    RuntimeRoot = root, Profile = "windows-pc-cpu", MaxBodies = 4 }))
                    throw new Exception(manager.LastError);
                manager.Shutdown();
            }
            finally { UnityEngine.Object.DestroyImmediate(obj); }
            File.WriteAllText(Path.Combine(Path.GetDirectoryName(Application.dataPath), "release-import-pass.txt"),
                "PASS: 20 runtime files verified; three demos generated; half-size defaults; Windows CPU native/model initialization.\n");
            EditorApplication.Exit(0);
        }
        catch (Exception error) { Debug.LogException(error); EditorApplication.Exit(1); }
    }
}
