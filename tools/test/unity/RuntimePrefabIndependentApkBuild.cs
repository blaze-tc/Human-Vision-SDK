using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;
using UnityEngine.Rendering;

// Controller-owned acceptance build. Never replaces the historical diagnostic builders.
[InitializeOnLoad]
public static class RuntimePrefabIndependentApkBuild
{
    private const string Scene = "Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity";
    private const string AppId = "com.blazetc.humanvision.runtimeacceptance.independent";
    private static string Root => Path.GetDirectoryName(Application.dataPath);
    private static string Output => Path.Combine(Root, "Builds", "IndependentRuntime-20261123");
    [Serializable] private sealed class Request { public string variant; }
    static RuntimePrefabIndependentApkBuild() { EditorApplication.update += Poll; }
    private static void Poll()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode || EditorApplication.isCompiling || EditorApplication.isUpdating || BuildPipeline.isBuildingPlayer) return;
        var request = Path.Combine(Output, "build-request.json");
        if (!File.Exists(request)) return;
        if (EditorUserBuildSettings.activeBuildTarget != BuildTarget.Android) {
            EditorUserBuildSettings.SwitchActiveBuildTarget(BuildTargetGroup.Android, BuildTarget.Android);
            return;
        }
        var value = JsonUtility.FromJson<Request>(File.ReadAllText(request));
        File.Delete(request);
        if (value == null || string.IsNullOrWhiteSpace(value.variant) || value.variant.Length > 48 ||
            value.variant.Any(c => !(c <= 127 && (char.IsLetterOrDigit(c) || c == '-')))) {
            Debug.LogError("Invalid independent acceptance build variant"); return;
        }
        Build(value.variant);
    }
    private static void Build(string variant)
    {
        var directory = Path.Combine(Output, variant);
        Directory.CreateDirectory(directory);
        var apk = Path.Combine(directory, "RuntimePrefabAcceptance-Android.apk");
        var status = Path.Combine(directory, "build-status.txt");
        if (File.Exists(apk)) { File.WriteAllText(status, "Failed: immutable output already exists"); return; }
        var group = BuildTargetGroup.Android;
        var oldId = PlayerSettings.GetApplicationIdentifier(group);
        var oldArch = PlayerSettings.Android.targetArchitectures;
        var oldBackend = PlayerSettings.GetScriptingBackend(group);
        var oldMin = PlayerSettings.Android.minSdkVersion;
        var oldAuto = PlayerSettings.GetUseDefaultGraphicsAPIs(BuildTarget.Android);
        var oldApis = PlayerSettings.GetGraphicsAPIs(BuildTarget.Android);
        var oldDefines = PlayerSettings.GetScriptingDefineSymbolsForGroup(group);
        var oldKeystore = PlayerSettings.Android.useCustomKeystore;
        var oldBundle = EditorUserBuildSettings.buildAppBundle;
        var oldInternet = PlayerSettings.Android.forceInternetPermission;
        File.WriteAllText(status, "Running");
        try {
            if (AssetDatabase.LoadAssetAtPath<SceneAsset>(Scene) == null) throw new InvalidOperationException("Acceptance scene missing");
            PlayerSettings.SetApplicationIdentifier(group, AppId);
            PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
            PlayerSettings.SetScriptingBackend(group, ScriptingImplementation.IL2CPP);
            PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.Vulkan });
            var defines = oldDefines.Split(';').Where(s => !string.IsNullOrWhiteSpace(s)).ToList();
            if (!defines.Contains("HV_ANDROID_RTSP_ACCEPTANCE")) defines.Add("HV_ANDROID_RTSP_ACCEPTANCE");
            PlayerSettings.SetScriptingDefineSymbolsForGroup(group, string.Join(";", defines));
            PlayerSettings.Android.useCustomKeystore = false;
            PlayerSettings.Android.forceInternetPermission = true;
            EditorUserBuildSettings.buildAppBundle = false;
            var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
                scenes = new[] { Scene }, target = BuildTarget.Android, locationPathName = apk,
                options = BuildOptions.Development
            });
            File.WriteAllText(Path.Combine(directory, "build-report.txt"),
                $"result={report.summary.result}\nerrors={report.summary.totalErrors}\nwarnings={report.summary.totalWarnings}\nbytes={report.summary.totalSize}\n");
            File.WriteAllText(status, report.summary.result.ToString());
        }
        catch (Exception e) { File.WriteAllText(status, "Failed\n" + e); Debug.LogException(e); }
        finally {
            PlayerSettings.SetApplicationIdentifier(group, oldId);
            PlayerSettings.Android.targetArchitectures = oldArch;
            PlayerSettings.SetScriptingBackend(group, oldBackend);
            PlayerSettings.Android.minSdkVersion = oldMin;
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, oldAuto);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, oldApis);
            PlayerSettings.SetScriptingDefineSymbolsForGroup(group, oldDefines);
            PlayerSettings.Android.useCustomKeystore = oldKeystore;
            PlayerSettings.Android.forceInternetPermission = oldInternet;
            EditorUserBuildSettings.buildAppBundle = oldBundle;
        }
    }
}
