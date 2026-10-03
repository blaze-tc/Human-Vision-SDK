using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
public static class Task9AdapterGateBuild {
 public static void Build(){
  PlayerSettings.productName="Task9 RTSP Actual Recognition Gate";PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android,"com.blazetc.humanvision.task9gate");
  PlayerSettings.Android.minSdkVersion=AndroidSdkVersions.AndroidApiLevel26;PlayerSettings.Android.targetArchitectures=AndroidArchitecture.ARM64;
  PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android,ScriptingImplementation.IL2CPP);PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android,false);PlayerSettings.SetGraphicsAPIs(BuildTarget.Android,new[]{GraphicsDeviceType.Vulkan});
  foreach(string lib in new[]{"libhumanvision_input.so","libhumanvision.so"}){var importer=(PluginImporter)AssetImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/"+lib);importer.SetCompatibleWithAnyPlatform(false);importer.SetCompatibleWithPlatform(BuildTarget.Android,true);importer.SetPlatformData(BuildTarget.Android,"CPU","ARM64");importer.isPreloaded=true;importer.SaveAndReimport();}
  var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);var camera=new GameObject("Diagnostic clear camera").AddComponent<Camera>();camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=Color.black;
  new GameObject("Actual source and SDK adapter").AddComponent<Task9AdapterGate>();EditorSceneManager.SaveScene(scene,"Assets/Task9.unity");
  var args=System.Environment.GetCommandLineArgs();int index=System.Array.IndexOf(args,"-task9Apk");var report=BuildPipeline.BuildPlayer(new[]{"Assets/Task9.unity"},args[index+1],BuildTarget.Android,BuildOptions.Development);
  if(report.summary.result!=BuildResult.Succeeded)throw new System.Exception("Actual Task9 APK build failed: "+report.summary.result);
 }
}
