using System;
using System.IO;
using HumanVision.Demo;
using UnityEditor;
using UnityEditor.Android;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Editor
{
    // Called only by tools/test/build_android_gpu_bridge_gate.ps1 in an ignored
    // verification project. The ordinary NCNN production validator stays strict.
    public static class HumanVisionAndroidGpuGateBuild
    {
        private static bool _active;
        private static bool HasGateFlag()
        {
            return Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionGpuGate") >= 0;
        }
        public static bool IsAuthorizedGateBuild(BuildReport report)
        {
            return _active && Application.isBatchMode &&
                HasGateFlag() &&
                report.summary.options.HasFlag(BuildOptions.Development) &&
                report.summary.outputPath.EndsWith("humanvision-gpu-bridge-gate.apk", StringComparison.OrdinalIgnoreCase);
        }
        public static void Build()
        {
            if (!Application.isBatchMode || !HasGateFlag())
                throw new InvalidOperationException("GPU gate build requires the isolated test script; batch=" +
                    Application.isBatchMode + " marker=" + HasGateFlag());
            string input = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "input-contract.json"));
            if (string.IsNullOrEmpty(input) || !File.Exists(input) ||
                !Path.GetFullPath(input).Contains("android-gpu-gate-runtime"))
                throw new InvalidOperationException("Missing generated test-only input contract under out/android-gpu-gate-runtime");
            string assetPath = "Assets/HumanVision/GpuGateGenerated/input-contract.json";
            Directory.CreateDirectory(Path.GetDirectoryName(assetPath));
            File.Copy(input, assetPath, true);
            AssetDatabase.Refresh();
            var fixture = AssetDatabase.LoadAssetAtPath<TextAsset>(assetPath);
            if (fixture == null) throw new InvalidOperationException("GPU gate input contract import failed");
            string scenePath = "Assets/Scenes/HumanVisionCameraDemo.unity";
            Directory.CreateDirectory("Assets/Scenes");
            HumanVisionCameraDemoBuilder.CreateGateScene(scenePath);
            foreach (var behaviour in UnityEngine.Object.FindObjectsOfType<MonoBehaviour>()) behaviour.enabled = false;
            var gate = new GameObject("Development GPU bridge gate").AddComponent<HumanVisionAndroidGpuGate>();
            gate.inputContract = fixture;
            EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), scenePath);
            HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId = "android-ncnn-vulkan";
            PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
            PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
            PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, ScriptingImplementation.IL2CPP);
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.Vulkan });
            PlayerSettings.SetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android, "HUMANVISION_GPU_GATE");
            var plugin = PluginImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/libhumanvision.so") as PluginImporter;
            if (plugin == null) throw new InvalidOperationException("Gate native libhumanvision.so is missing");
            plugin.SetCompatibleWithAnyPlatform(false);
            plugin.SetCompatibleWithPlatform(BuildTarget.Android, true);
            plugin.SetPlatformData("Android", "CPU", "ARM64");
            plugin.SaveAndReimport();
            string output = Path.GetFullPath("../humanvision-gpu-bridge-gate.apk");
            Directory.CreateDirectory(Path.GetDirectoryName(output));
            _active = true;
            try
            {
                var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
                    scenes = new[] { scenePath }, locationPathName = output,
                    target = BuildTarget.Android, options = BuildOptions.Development
                });
                if (report.summary.result != BuildResult.Succeeded)
                    throw new InvalidOperationException("GPU gate APK failed: " + report.summary.result +
                        " errors=" + report.summary.totalErrors);
                Debug.Log("HV_GPU_GATE_APK=" + output + " bytes=" + report.summary.totalSize);
            }
            finally { _active = false; }
        }
    }
}
