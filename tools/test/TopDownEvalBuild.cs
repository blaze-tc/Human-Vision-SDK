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
        [Serializable] private sealed class EditorRequest { public string project, output, manifest_sha256; }
        [MenuItem("HumanVision/Evaluation/Build R4 Static Parity")]
        public static void BuildR4FromEditor()
        {
            var request=JsonUtility.FromJson<EditorRequest>(File.ReadAllText("r4-parity-build-request.json"));
            if(EditorApplication.isPlayingOrWillChangePlaymode ||
                !string.Equals(Path.GetFullPath(request.project).TrimEnd('\\','/'),Path.GetFullPath(".").TrimEnd('\\','/'),StringComparison.OrdinalIgnoreCase) ||
                request.manifest_sha256!=TopDownEvalParitySource.ManifestSha256 || !Path.IsPathRooted(request.output))
                throw new InvalidOperationException("R4 editor request identity does not match staged project/fixture");
            var setup=EditorSceneManager.GetSceneManagerSetup();
            for(int i=0;i<UnityEngine.SceneManagement.SceneManager.sceneCount;i++)
                if(UnityEngine.SceneManagement.SceneManager.GetSceneAt(i).isDirty)
                    throw new InvalidOperationException("R4 build requires saved scenes to preserve user edits");
            var group=BuildTargetGroup.Android;
            var identifier=PlayerSettings.GetApplicationIdentifier(group);
            var sdk=PlayerSettings.Android.minSdkVersion; var architectures=PlayerSettings.Android.targetArchitectures;
            var backend=PlayerSettings.GetScriptingBackend(group); var defaults=PlayerSettings.GetUseDefaultGraphicsAPIs(BuildTarget.Android);
            var apis=PlayerSettings.GetGraphicsAPIs(BuildTarget.Android); var defines=PlayerSettings.GetScriptingDefineSymbolsForGroup(group);
            var mode=HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId;
            try { BuildCore(false,true,"Assets/HumanVisionR4/Scenes",request.output); }
            finally {
                PlayerSettings.SetApplicationIdentifier(group,identifier);
                PlayerSettings.Android.minSdkVersion=sdk; PlayerSettings.Android.targetArchitectures=architectures;
                PlayerSettings.SetScriptingBackend(group,backend); PlayerSettings.SetGraphicsAPIs(BuildTarget.Android,apis);
                PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android,defaults);
                PlayerSettings.SetScriptingDefineSymbolsForGroup(group,defines);
                HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId=mode;
                EditorSceneManager.RestoreSceneManagerSetup(setup);
                AssetDatabase.SaveAssets();
                Debug.Log("HV_R4_EDITOR_SETTINGS_RESTORED");
            }
        }
        public static void Build()
        {
            if (!Application.isBatchMode || Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionTopDownEval") < 0)
                throw new InvalidOperationException("TopDown evaluation requires its isolated build script");
            BuildCore(Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionTopDownVideo") >= 0,
                Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionR4Parity") >= 0,
                "Assets/Scenes",Path.GetFullPath("../humanvision-topdown.apk"));
        }
        private static void BuildCore(bool videoDiagnostic,bool parity,string directory,string output)
        {
            string scene = directory+"/HumanVisionCameraDemo.unity";
            string settings = directory+"/HumanVisionCameraSettings.unity";
            Directory.CreateDirectory(directory);
            HumanVisionCameraDemoBuilder.CreateEvaluationPair(scene, settings);
            EditorSceneManager.OpenScene(scene);
            var facade = UnityEngine.Object.FindObjectOfType<HumanVisionCameraManager>();
            if (facade == null) throw new InvalidOperationException("Camera Demo scene has no manager");
            if(parity && videoDiagnostic) throw new InvalidOperationException("R4 static and video routes are exclusive");
            facade.startAutomatically = !(videoDiagnostic || parity);
            facade.gameObject.AddComponent<TopDownEvalProbe>();
            if (videoDiagnostic) {
                facade.gameObject.AddComponent<TopDownEvalVideoSource>();
                Debug.Log("HV_TOPDOWN_VIDEO_SCENE source=" + TopDownEvalVideoSource.VideoName +
                    " sha256=" + TopDownEvalVideoSource.VideoSha256);
            }
            if(parity) facade.gameObject.AddComponent<TopDownEvalParitySource>();
            EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), scene);
            HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId = "android-ncnn-vulkan";
            PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android,
                "com.blazetc.humanvision.topdowneval.i" + TopDownEvalProbe.Interval +
                "c" + TopDownEvalProbe.Capacity + (parity ? ".parity" : videoDiagnostic ? ".video" : ""));
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
