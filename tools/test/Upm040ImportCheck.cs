using System;
using System.IO;
using System.Security.Cryptography;
using HumanVision;
using HumanVision.Editor;
using UnityEditor;
using UnityEngine;
public static class Upm040ImportCheck
{
    [Serializable] private class Index { public Entry[] files; }
    [Serializable] private class Entry { public string path; public string sha256; }
    public static void Run()
    {
        try {
            HumanVisionModelInstaller.Prepare();
            AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
            const string root = "Assets/StreamingAssets/HumanVision/Runtime";
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(root+"/index.json"));
            foreach(var entry in index.files) {
                using(var sha=SHA256.Create()) using(var stream=File.OpenRead(root+"/"+entry.path))
                    if(BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant()!=entry.sha256) throw new Exception("Installed data hash mismatch: "+entry.path);
                string a=AssetDatabase.AssetPathToGUID(root+"/"+entry.path), b=AssetDatabase.AssetPathToGUID("Packages/com.blazetc.humanvision/RuntimeData/"+entry.path);
                if(string.IsNullOrEmpty(a)||string.IsNullOrEmpty(b)||a==b) throw new Exception("Runtime data GUID collision: "+entry.path);
            }
            HumanVisionCameraDemoBuilder.CreateScene();
            var scenes=EditorBuildSettings.scenes;
            if(scenes.Length<2) throw new Exception("Missing camera/settings scenes");
            var overlay=UnityEngine.Object.FindObjectOfType<HumanVisionSkeletonOverlayer>();
            if(overlay==null||overlay.manager==null||overlay.preview==null||overlay.jointPrefab!=null||overlay.linePrefab!=null) throw new Exception("Default batched overlay bindings invalid");
            var obj=new GameObject("Native package check");
            try { var manager=obj.AddComponent<HumanVisionManager>();
                if(!manager.TryInitialize(new HumanVisionConfig{RuntimeRoot=Path.GetFullPath(root),Profile="cpu",MaxBodies=1})) throw new Exception(manager.LastError);
            } finally { UnityEngine.Object.DestroyImmediate(obj); }
            File.WriteAllText("upm-import-pass.txt", "UPM import PASS: packaged runtime initialized; models/hash/GUID isolation verified; camera and settings scenes generated.");
            EditorApplication.Exit(0);
        } catch(Exception e) { Debug.LogException(e); EditorApplication.Exit(1); }
    }
}
