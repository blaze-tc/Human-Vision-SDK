using System;
using System.IO;
using HumanVision;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Editor
{
    // Copied only into an ignored, isolated evaluation project.
    public static class TopDownEvalBuild
    {
        public static void Build()
        {
            if (!Application.isBatchMode || Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionTopDownEval") < 0)
                throw new InvalidOperationException("TopDown evaluation requires its isolated build script");
            string scene = "Assets/Scenes/HumanVisionCameraDemo.unity";
            string settings = "Assets/Scenes/HumanVisionCameraSettings.unity";
            Directory.CreateDirectory("Assets/Scenes");
            HumanVisionCameraDemoBuilder.CreateEvaluationPair(scene, settings);
            EditorSceneManager.OpenScene(scene);
            var facade = UnityEngine.Object.FindObjectOfType<HumanVisionCameraManager>();
            if (facade == null) throw new InvalidOperationException("Camera Demo scene has no manager");
            bool videoDiagnostic = Array.IndexOf(Environment.GetCommandLineArgs(),
                "-humanvisionTopDownVideo") >= 0;
            facade.startAutomatically = !videoDiagnostic;
            facade.gameObject.AddComponent<TopDownEvalProbe>();
            if (videoDiagnostic) {
                facade.gameObject.AddComponent<TopDownEvalVideoSource>();
                Debug.Log("HV_TOPDOWN_VIDEO_SCENE source=" + TopDownEvalVideoSource.VideoName +
                    " sha256=" + TopDownEvalVideoSource.VideoSha256);
            }
            EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), scene);
            HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId = "android-ncnn-vulkan";
            PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android,
                "com.blazetc.humanvision.topdowneval.i" + TopDownEvalProbe.Interval +
                "c" + TopDownEvalProbe.Capacity + (videoDiagnostic ? ".video" : ""));
            PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
            PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
            PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, ScriptingImplementation.IL2CPP);
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.Vulkan });
            PlayerSettings.SetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android, "HV_TOPDOWN_EVAL");
            var plugin = PluginImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/libhumanvision.so") as PluginImporter;
            if (plugin == null) throw new InvalidOperationException("libhumanvision.so missing");
            plugin.SetCompatibleWithAnyPlatform(false);
            plugin.SetCompatibleWithPlatform(BuildTarget.Android, true);
            plugin.SetPlatformData("Android", "CPU", "ARM64");
            plugin.isPreloaded = true;
            plugin.SaveAndReimport();
            string output = Path.GetFullPath("../humanvision-topdown.apk");
            var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
                scenes = new[] { scene, settings }, locationPathName = output,
                target = BuildTarget.Android, options = BuildOptions.Development
            });
            if (report.summary.result != BuildResult.Succeeded)
                throw new InvalidOperationException("TopDown APK failed: " + report.summary.result);
            Debug.Log("HV_TOPDOWN_EVAL_APK=" + output + " bytes=" + report.summary.totalSize);
        }
    }
}
