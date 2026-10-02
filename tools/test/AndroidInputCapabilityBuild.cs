using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using HumanVision.Input.Tests;
public static class AndroidInputCapabilityBuild
{
    public static void Build()
    {
        PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android,"com.blazetc.humanvision.inputgate");
        PlayerSettings.Android.minSdkVersion=AndroidSdkVersions.AndroidApiLevel26;
        PlayerSettings.Android.targetArchitectures=AndroidArchitecture.ARM64;
        PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android,ScriptingImplementation.IL2CPP);
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android,false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.Android,new[]{GraphicsDeviceType.Vulkan});
        var importer=(PluginImporter)AssetImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/libhumanvision_input.so");
        importer.SetCompatibleWithAnyPlatform(false);
        importer.SetCompatibleWithPlatform(BuildTarget.Android,true);
        importer.SetPlatformData(BuildTarget.Android,"CPU","ARM64");
        importer.isPreloaded=true; importer.SaveAndReimport();
        var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
        if (Resources.Load<TextAsset>("input-gate-mode").text.Trim() == "Lifecycle") new GameObject("Input lifecycle probe").AddComponent<AndroidInputLifecycleProbe>();
        else new GameObject("Input capability probe").AddComponent<AndroidInputCapabilityProbe>();
        EditorSceneManager.SaveScene(scene,"Assets/InputCapabilityGate.unity");
        var args=System.Environment.GetCommandLineArgs();
        var index=System.Array.IndexOf(args,"-inputGateApk");
        var result=BuildPipeline.BuildPlayer(new[]{"Assets/InputCapabilityGate.unity"},args[index+1],BuildTarget.Android,BuildOptions.Development);
        if(result.summary.result!=BuildResult.Succeeded) throw new System.Exception("Input capability APK build failed: "+result.summary.result);
    }
}
