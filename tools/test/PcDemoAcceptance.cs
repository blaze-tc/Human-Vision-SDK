using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Demo.PC;
using HumanVision.Demo.Editor;
using HumanVision.Editor;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Video;

[InitializeOnLoad]
public static class PcDemoAcceptance
{
    [Serializable] class Sample { public long sequence,source; public int bodies,vertices; public double age; public bool visible,upright; }
    [Serializable] class Report { public string startedUtc,gpu,api,diagnostics,error,cpuDiagnostics; public int unique,sevens,visible,upright; public double seconds,fps,ageP50,ageP95; public Sample[] samples; public bool stopClears,invalidPathRejected,cpuInitialized; }
    [Serializable] class RuntimeIndex { public RuntimeEntry[] files; }
    [Serializable] class RuntimeEntry { public string path,sha256; }
    [Serializable] class Installation { public string utc,packagePath,packageManifestSha256,nativeSha256,indexSha256; public bool hashesAndGuidsVerified; }
    [Serializable] class BuildEvidence { public string completedUtc,result; public double seconds; public ulong bytes; public int warnings,errors; }
    static HumanVisionPcDemo demo; static HumanVisionManager manager; static VideoPlayerFrameSource source; static HumanVisionOverlay overlay;
    static readonly List<Sample> samples=new List<Sample>(); static Report report;
    static double deadline,first,last; static long sequence; static int stage;
    static PcDemoAcceptance() { EditorApplication.update += Tick; }
    public static void Run()
    {
        try {
            string project=Path.GetDirectoryName(Application.dataPath).Replace('\\','/');
            if(!project.Contains("/out/pc-demo/") || !File.Exists(Path.Combine(project,".pc-demo-acceptance-project")))
                throw new Exception("Acceptance is restricted to scratch projects created by run_pc_demo_acceptance.ps1.");
            SessionState.SetInt("HVPCStage",0);
            VerifyPolicies(); HumanVisionModelInstaller.Prepare(); AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
            VerifyInstallation();
            var path=HumanVisionPcDemoBuilder.CreateScene();
            // This is an isolated scratch project: run only the generated scene.
            EditorSceneManager.OpenScene(path,OpenSceneMode.Single);
            SessionState.SetString("HVPCScene",path);
            demo=UnityEngine.Object.FindObjectOfType<HumanVisionPcDemo>();
            var so=new SerializedObject(demo);so.FindProperty("videoPath").stringValue="E:/Project/Human Vision SDK/video-1.mp4";
            so.FindProperty("startSeconds").floatValue=37;so.FindProperty("startOnPlay").boolValue=false;so.FindProperty("showPanel").boolValue=false;
            so.ApplyModifiedPropertiesWithoutUndo();EditorSceneManager.SaveScene(demo.gameObject.scene);
            SessionState.SetBool("HVPCAcceptance",true); EditorApplication.EnterPlaymode();
        } catch(Exception e) { Fail(e); }
    }
    static string Hash(string path)
    {
        using(var sha=System.Security.Cryptography.SHA256.Create())
        using(var stream=File.OpenRead(path))return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
    }
    static void VerifyInstallation()
    {
        var package=UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.blazetc.humanvision/package.json");
        if(package==null || package.version!="0.4.0-pc.1")throw new Exception("PC package not resolved");
        string root="Assets/StreamingAssets/HumanVision/Runtime/";
        var index=JsonUtility.FromJson<RuntimeIndex>(File.ReadAllText(root+"index.json"));
        if(index==null || index.files==null || index.files.Length!=7)throw new Exception("PC runtime closure must have seven indexed files");
        foreach(var entry in index.files) {
            if(Hash(root+entry.path)!=entry.sha256)throw new Exception("Installed runtime hash mismatch: "+entry.path);
            string installed=AssetDatabase.AssetPathToGUID(root+entry.path);
            string packaged=AssetDatabase.AssetPathToGUID("Packages/com.blazetc.humanvision/RuntimeData/"+entry.path);
            if(string.IsNullOrEmpty(installed)||string.IsNullOrEmpty(packaged)||installed==packaged)throw new Exception("Installed runtime GUID isolation failed: "+entry.path);
        }
        var evidence=new Installation {utc=DateTime.UtcNow.ToString("o"),packagePath=package.resolvedPath,
            packageManifestSha256=Hash(Path.Combine(package.resolvedPath,"asset-sha256.json")),
            nativeSha256=Hash(Path.Combine(package.resolvedPath,"Runtime/Plugins/x86_64/humanvision.dll")),
            indexSha256=Hash(root+"index.json"),hashesAndGuidsVerified=true};
        File.WriteAllText("pc-demo-installation.json",JsonUtility.ToJson(evidence,true));
    }
    static void VerifyPolicies()
    {
        var type=typeof(HumanVisionPcDemo).Assembly.GetType("HumanVision.Demo.PC.PcDiagnostics");
        if(type==null)throw new Exception("RED: PC diagnostics formatter missing");
        var method=type.GetMethod("Format");
        string input="Android mode: windows-pc-directml; input: CPU\nSelected backend: DirectML\nResult age ms: 999999\nSample age: 999999\nSample state: Stale\nTracked bodies=0\nSampled bodies=0\nRaw body FPS=99\nRaw observation bodies=7\nPipeline: rtmo\nInference ms: 12";
        string output=(string)method.Invoke(null,new object[]{input});
        if(output.Contains("999999")||output.Contains("Stale")||output.Contains("Tracked bodies=")||output.Contains("Sampled bodies=")||output.Contains("Raw body FPS=")||output.Contains("Android mode:")||!output.Contains("Raw observation bodies=7")||!output.Contains("DirectML")||!output.Contains("rtmo")||!output.Contains("12"))throw new Exception("PC diagnostics formatter invalid: "+output);
        Debug.Log("PASS PC diagnostics clock-isolation policy");
    }
    static void Tick()
    {
        if(!SessionState.GetBool("HVPCAcceptance",false))return;
        try {
            if(!EditorApplication.isPlaying) {
                if(SessionState.GetInt("HVPCStage",0)==4) { Build();return; }
                return;
            }
            if(report==null) {
                demo=UnityEngine.Object.FindObjectOfType<HumanVisionPcDemo>();if(demo==null)return;
                manager=demo.GetComponent<HumanVisionManager>();source=demo.GetComponent<VideoPlayerFrameSource>();overlay=UnityEngine.Object.FindObjectOfType<HumanVisionOverlay>();
                report=new Report {startedUtc=DateTime.UtcNow.ToString("o"),gpu=SystemInfo.graphicsDeviceName,api=SystemInfo.graphicsDeviceType.ToString()};
                deadline=EditorApplication.timeSinceStartup+180;stage=1;demo.StartDemo();
            }
            if(EditorApplication.timeSinceStartup>deadline) throw new Exception("PC acceptance timed out: "+demo.Status+" / "+manager.LastError+" / "+source.LastError);
            if(stage==1) {
                if(!manager.IsInitialized) {if(!demo.Status.Contains("Preparing"))throw new Exception(demo.Status);return;}
                if(!string.IsNullOrEmpty(manager.LastError)||!string.IsNullOrEmpty(source.LastError))throw new Exception(manager.LastError+source.LastError);
                if(manager.ResultSequence>sequence && manager.BodyCount>0) {
                    sequence=manager.ResultSequence;double now=EditorApplication.timeSinceStartup;
                    if(first==0)first=now;last=now;
                    Canvas.ForceUpdateCanvases();int vertices; using(var vh=new UnityEngine.UI.VertexHelper()){typeof(HumanVisionOverlay).GetMethod("OnPopulateMesh",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic,null,new[]{typeof(VertexHelper)},null).Invoke(overlay,new object[]{vh});vertices=vh.currentVertCount;}
                    bool upright=true;for(int i=0;i<manager.BodyCount;i++){var j=manager.Bodies[i].Joints;if(j[0].Valid&&j[15].Valid&&j[16].Valid && j[0].Pixel.y>=Mathf.Max(j[15].Pixel.y,j[16].Pixel.y))upright=false;}
                    samples.Add(new Sample {sequence=sequence,source=manager.SourceFrameId,bodies=manager.BodyCount,age=source.ResultAgeMilliseconds,visible=source.CanPresentResult(manager.SourceFrameId),vertices=vertices,upright=upright});
                    if(samples.Count==30)CaptureEvidence();
                    if(now-first>=20 && samples.Count>=30) {
                        report.unique=samples.Count;report.seconds=last-first;report.fps=(samples.Count-1)/report.seconds;
                        if(samples.Select(s=>s.source).Distinct().Count()!=samples.Count)throw new Exception("Repeated source frame counted as a fresh observation");
                        report.sevens=samples.Count(s=>s.bodies==7);report.visible=samples.Count(s=>s.visible&&s.vertices>0);report.upright=samples.Count(s=>s.upright);
                        var ages=samples.Select(s=>s.age).OrderBy(x=>x).ToArray();report.ageP50=ages[ages.Length/2];report.ageP95=ages[Math.Min(ages.Length-1,(int)(ages.Length*.95))];report.samples=samples.ToArray();report.diagnostics=manager.RuntimeDiagnostics; if(!report.diagnostics.Contains("Actual backend=DirectML")) throw new Exception("DirectML actual backend not confirmed: "+report.diagnostics); Debug.Log("DirectML actual diagnostics: "+report.diagnostics);
                        if(report.visible==0||report.sevens==0||report.upright==0)throw new Exception("No verified visible upright seven-person observation");
                        demo.StopDemo();report.stopClears=!manager.IsInitialized&&manager.BodyCount==0&&!source.IsPlaying&&source.PresentationTexture==null;
                        var so=new SerializedObject(demo);so.FindProperty("videoPath").stringValue="Z:/missing-test-video.mp4";so.ApplyModifiedPropertiesWithoutUndo();demo.StartDemo();report.invalidPathRejected=!manager.IsInitialized&&demo.Status.Contains("existing local video");
                        so.FindProperty("backend").enumValueIndex=1;so.FindProperty("maxBodies").intValue=1;so.FindProperty("videoPath").stringValue="E:/Project/Human Vision SDK/video-2.mp4";so.FindProperty("startSeconds").floatValue=0;so.ApplyModifiedPropertiesWithoutUndo();demo.StartDemo();stage=2;deadline=now+180;
                    }
                }
            } else if(stage==2) {
                if(manager.IsInitialized && manager.BodyCount>0) {
                    report.cpuDiagnostics=manager.RuntimeDiagnostics; report.cpuInitialized=manager.ActiveRuntimeProfile=="windows-pc-cpu" && manager.MaxBodies==1 && manager.BodyCount==1 && report.cpuDiagnostics.Contains("Actual backend=CPU");
                    if(!report.cpuInitialized)throw new Exception("Explicit CPU restart failed");
                    demo.StopDemo();if(!report.stopClears||!report.invalidPathRejected)throw new Exception("Stop/missing-path validation failed");
                    File.WriteAllText("pc-demo-acceptance.json",JsonUtility.ToJson(report,true));stage=4;SessionState.SetInt("HVPCStage",4);EditorApplication.ExitPlaymode();
                } else if(!manager.IsInitialized && !demo.Status.Contains("Preparing"))throw new Exception(demo.Status);
            }
        } catch(Exception e) { Fail(e); }
    }
    static void CaptureEvidence()
    {
        // Batch Editor does not present the Game view. Render the same live UI
        // canvas and camera into a target to preserve video + actual overlay proof.
        var camera=UnityEngine.Object.FindObjectOfType<Camera>();
        var canvas=overlay.GetComponentInParent<Canvas>();
        var mode=canvas.renderMode;var canvasCamera=canvas.worldCamera;var distance=canvas.planeDistance;
        var target=camera.targetTexture;int mask=camera.cullingMask;var active=RenderTexture.active;
        var rt=new RenderTexture(1920,1080,24);var pixels=new Texture2D(1920,1080,TextureFormat.RGB24,false);
        try {
            rt.Create();camera.targetTexture=rt;camera.cullingMask=~0;
            canvas.renderMode=RenderMode.ScreenSpaceCamera;canvas.worldCamera=camera;canvas.planeDistance=1;
            Canvas.ForceUpdateCanvases();camera.Render();RenderTexture.active=rt;
            pixels.ReadPixels(new Rect(0,0,1920,1080),0,0);pixels.Apply();
            File.WriteAllBytes("pc-demo-visible.png",pixels.EncodeToPNG());
        } finally {
            RenderTexture.active=active;camera.targetTexture=target;camera.cullingMask=mask;
            canvas.renderMode=mode;canvas.worldCamera=canvasCamera;canvas.planeDistance=distance;
            UnityEngine.Object.DestroyImmediate(pixels);rt.Release();UnityEngine.Object.DestroyImmediate(rt);
            Canvas.ForceUpdateCanvases();
        }
    }
    static void Build()
    {
        SessionState.SetBool("HVPCAcceptance",false);
        string scene=SessionState.GetString("HVPCScene","");EditorSceneManager.OpenScene(scene,OpenSceneMode.Single);
        demo=UnityEngine.Object.FindObjectOfType<HumanVisionPcDemo>();var so=new SerializedObject(demo);
        so.FindProperty("backend").enumValueIndex=0;so.FindProperty("maxBodies").intValue=8;so.FindProperty("videoPath").stringValue="E:/Project/Human Vision SDK/video-1.mp4";
        so.FindProperty("startSeconds").floatValue=37;so.FindProperty("startOnPlay").boolValue=true;so.FindProperty("showPanel").boolValue=true;so.ApplyModifiedPropertiesWithoutUndo();EditorSceneManager.SaveScene(demo.gameObject.scene);
        Directory.CreateDirectory("Build");PlayerSettings.SetScriptingBackend(BuildTargetGroup.Standalone,ScriptingImplementation.Mono2x);
        var result=BuildPipeline.BuildPlayer(new BuildPlayerOptions { scenes=new[]{scene},locationPathName="Build/HumanVisionPcDemo.exe",target=BuildTarget.StandaloneWindows64,options=BuildOptions.Development });
        if(result.summary.result!=UnityEditor.Build.Reporting.BuildResult.Succeeded)throw new Exception("Windows player build failed: "+result.summary.result);
        File.WriteAllText("pc-demo-build.json",JsonUtility.ToJson(new BuildEvidence {completedUtc=DateTime.UtcNow.ToString("o"),result=result.summary.result.ToString(),seconds=result.summary.totalTime.TotalSeconds,bytes=result.summary.totalSize,warnings=result.summary.totalWarnings,errors=result.summary.totalErrors},true));
        File.WriteAllText("pc-demo-pass.txt","PASS: real DirectML video observations, visible upright seven-person mesh, CPU restart, stop/reject, Win64 player built");
        EditorApplication.Exit(0);
    }
    static void Fail(Exception e) {SessionState.SetBool("HVPCAcceptance",false);Debug.LogException(e);File.WriteAllText("pc-demo-fail.txt",e.ToString());EditorApplication.Exit(1);}
}
