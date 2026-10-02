using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
public static class Task8SourceRetirementBuild
{
    public static void Build() {
        PlayerSettings.productName="Task8 Source Copy Gate - No Recognition";
        PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android,"com.blazetc.humanvision.task8gate");
        PlayerSettings.Android.minSdkVersion=AndroidSdkVersions.AndroidApiLevel26;
        PlayerSettings.Android.targetArchitectures=AndroidArchitecture.ARM64;
        PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android,ScriptingImplementation.IL2CPP);
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android,false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.Android,new[]{GraphicsDeviceType.Vulkan});
        foreach(string lib in new[]{"libhumanvision_input.so","libhumanvision.so"}) {
            var importer=(PluginImporter)AssetImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/"+lib);
            importer.SetCompatibleWithAnyPlatform(false);importer.SetCompatibleWithPlatform(BuildTarget.Android,true);
            importer.SetPlatformData(BuildTarget.Android,"CPU","ARM64");importer.isPreloaded=true;importer.SaveAndReimport();
        }
        var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
        var camera=new GameObject("Diagnostic clear camera").AddComponent<Camera>();
        camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=Color.black;
        new GameObject("Task8 actual RTSP preview and retirement").AddComponent<Task8SourceRetirementProbe>();
        EditorSceneManager.SaveScene(scene,"Assets/Task8SourceRetirement.unity");
        var args=System.Environment.GetCommandLineArgs();int index=System.Array.IndexOf(args,"-task8Apk");
        var result=BuildPipeline.BuildPlayer(new[]{"Assets/Task8SourceRetirement.unity"},args[index+1],BuildTarget.Android,BuildOptions.Development);
        if(result.summary.result!=BuildResult.Succeeded)throw new System.Exception("Task8 APK build failed: "+result.summary.result);
    }
}
