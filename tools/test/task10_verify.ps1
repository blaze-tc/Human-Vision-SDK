param(
    [ValidateSet('Input','Combined','Canonical')][string]$Kind='Input',
    [string]$SourceRoot='',
    [string]$Output='out/input/task10-import',
    [ValidateSet('Generate','Windows','Android')][string]$Action='Generate'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$repo}
$project=Join-Path $repo "$Output-$Kind"
if(Test-Path -LiteralPath $project){throw 'Use a fresh immutable output path.'}
foreach($folder in @('Assets/Editor','Assets/Samples/InputPreview','Packages','ProjectSettings')){New-Item -ItemType Directory -Force (Join-Path $project $folder)|Out-Null}
$dependencies=@{'com.blazetc.humanvision.input'=('file:'+((Join-Path $source 'upm/com.blazetc.humanvision.input') -replace '\\','/'))}
$reference=Get-Content "$source/unity/HumanVisionDemo/Packages/manifest.json" -Raw|ConvertFrom-Json
foreach($property in $reference.dependencies.PSObject.Properties){if($property.Name -ne 'com.blazetc.humanvision'){$dependencies[$property.Name]=$property.Value}}
if($Kind -eq 'Combined'){$dependencies['com.blazetc.humanvision']='file:'+((Join-Path $source 'upm/com.blazetc.humanvision') -replace '\\','/')}
if($Kind -eq 'Canonical'){
    New-Item -ItemType Directory -Force "$project/Assets/HumanVision"|Out-Null
    foreach($section in @('Runtime','Demo','Editor')){Copy-Item "$source/unity/HumanVisionDemo/Assets/HumanVision/$section" "$project/Assets/HumanVision/" -Recurse}
}
@{dependencies=$dependencies}|ConvertTo-Json -Depth 5|Set-Content "$project/Packages/manifest.json"
Copy-Item "$source/upm/com.blazetc.humanvision.input/Samples~/InputPreview/*.cs*" "$project/Assets/Samples/InputPreview/"
'm_EditorVersion: 2021.3.45f1'|Set-Content "$project/ProjectSettings/ProjectVersion.txt"
$combined=if($Kind -ne 'Input'){'HumanVision.Editor.HumanVisionUnifiedDemoBuilder.BuildScenes();'}else{''}
@"
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Build.Reporting;
using UnityEngine;
public static class Task10Build {
 public static void Generate() {
  $combined
  var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
  new GameObject("Independent Input Preview").AddComponent<InputPreviewController>();
  System.IO.Directory.CreateDirectory("Assets/Scenes");
  EditorSceneManager.SaveScene(scene,"Assets/Scenes/InputPreview.unity");
  if(EditorBuildSettings.scenes.Length==0)EditorBuildSettings.scenes=new[]{new EditorBuildSettingsScene("Assets/Scenes/InputPreview.unity",true)};
  AssetDatabase.SaveAssets();
 }
 public static void Windows() { Generate(); Build(BuildTarget.StandaloneWindows64,"player/HumanVision.exe"); }
 public static void Android() {
  PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android,ScriptingImplementation.IL2CPP);
  PlayerSettings.Android.targetArchitectures=AndroidArchitecture.ARM64;
  PlayerSettings.Android.minSdkVersion=AndroidSdkVersions.AndroidApiLevel26;
  PlayerSettings.SetGraphicsAPIs(BuildTarget.Android,new[]{UnityEngine.Rendering.GraphicsDeviceType.Vulkan});
  Generate();Build(BuildTarget.Android,"HumanVision.apk");
 }
 static void Build(BuildTarget target,string path) {
  var paths=new System.Collections.Generic.List<string>();foreach(var s in EditorBuildSettings.scenes)if(s.enabled)paths.Add(s.path);
  var result=BuildPipeline.BuildPlayer(paths.ToArray(),path,target,BuildOptions.Development);
  if(result.summary.result!=BuildResult.Succeeded)throw new System.Exception("Actual player build failed: "+result.summary.result);
 }
}
"@|Set-Content "$project/Assets/Editor/Task10Build.cs"
$unity='D:/Developer/2021.3.45f1/Editor/Unity.exe'
$arguments="-batchmode -quit -force-d3d11 -projectPath `"$project`" -executeMethod Task10Build.$Action -logFile `"$project/unity.log`""
$info=[Diagnostics.ProcessStartInfo]::new($unity,$arguments);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.Environment['__COMPAT_LAYER']='RunAsInvoker'
$process=[Diagnostics.Process]::Start($info);$process.WaitForExit();$process.Refresh()
@{kind=$Kind;action=$Action;source=$source;project=$project;pid=$process.Id;exitCode=$process.ExitCode;arguments=$arguments}|ConvertTo-Json|Set-Content "$project/receipt.json"
if($process.ExitCode -ne 0 -or -not(Test-Path "$project/Assets/Scenes/InputPreview.unity")){throw 'Actual clean import/generation failed; inspect retained unity.log.'}
Write-Output "$Kind $Action PASS"
