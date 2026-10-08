using System;
using System.IO;
using System.Linq;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEditor;
using UnityEngine;

// Test-only probe: copy into Assets/Editor in the user's test project. No synthetic observations.
public static class HumanVisionSdkProjectProbe
{
    [Serializable] private class Config { public string video; }
    private static HumanVisionSettingsController controller;
    private static HumanVisionSdk sdk;
    private static int phase, observations, queriedJoints, knownRegions;
    private static long previous;
    private static double deadline;
    private static string Root => Path.GetDirectoryName(Application.dataPath);
    private static string Report => Path.Combine(Root, "sdk-api-probe.txt");
    [MenuItem("HumanVision/Verification/Start SDK API probe")]
    public static void Start()
    {
        if (!EditorApplication.isPlaying) throw new Exception("Enter Play Mode first.");
        controller = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsController>();
        if (controller == null) throw new Exception("Open generated SDK Settings scene.");
        sdk = controller.Sdk; var config = JsonUtility.FromJson<Config>(File.ReadAllText(Path.Combine(Root,"sdk-api-probe-config.json")));
        var data = controller.Draft; data.SourceKind = InputKind.Video; data.Video.VideoPath = config.video;
        data.UseWindowsCpu = true; data.Recognition.MaxBodies = 4; data.Recognition.UseRegions = false;
        data.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(4);
        controller.View.ShowDraft(data); controller.Execute("ApplySave");
        phase=0; observations=queriedJoints=knownRegions=0; previous=0; deadline=EditorApplication.timeSinceStartup+120;
        File.WriteAllText(Report,"START: genuine CPU video through settings ApplySave\n");
        EditorApplication.update -= Tick; EditorApplication.update += Tick;
    }
    private static void Check(bool ok,string message) { if (!ok) throw new Exception(message); }
    private static void Note(string text) => File.AppendAllText(Report,text+"\n");
    private static void Tick()
    {
        try {
            if (!EditorApplication.isPlaying) throw new Exception("Play Mode exited before verification completed.");
            if (phase==10) return;
            if (EditorApplication.timeSinceStartup > deadline) throw new Exception("Timed out phase "+phase+": "+(sdk==null?"destroyed":sdk.State+" "+sdk.LastError));
            if (phase==0 || phase==1 || phase==3) {
                if (sdk==null || !sdk.IsRunning || !sdk.HasFreshResult || sdk.GetUsersCount()==0 || sdk.ResultSequence==previous) return;
                previous=sdk.ResultSequence; observations++;
                Check(!double.IsInfinity(sdk.ResultAgeMilliseconds) && sdk.ResultAgeMilliseconds>=0,"Fresh result age invalid");
                for(int i=0;i<sdk.GetMaxBodies();i++) {
                    if(sdk.TryGetRegionOccupancy(i,out bool occupied)) knownRegions++;
                    if(!sdk.TryGetBodyByIndex(i,out var body)) continue;
                    var joints = new HumanVisionCanonicalJoint[32];
                    Check(sdk.CopySkeletonByIndex(i,joints,out var metadata),"Skeleton copy failed");
                    Check(sdk.GetUserIndexById(metadata.StableTrackId)==i,"Stable ID/index mismatch");
                    foreach(HumanVisionCanonicalJointId joint in Enum.GetValues(typeof(HumanVisionCanonicalJointId)))
                        if(sdk.TryGetJointByIndex(i,joint,out _)) {
                            Check(sdk.TryGetJointScreenPosition(i,joint,out _),"Screen mapping failed");
                            Check(sdk.TryGetJointWorldPosition(i,joint,out _),"World mapping failed"); queriedJoints++;
                        }
                }
                if(observations<5 || queriedJoints==0 || knownRegions==0) return;
                if(phase==0) {
                    Check(controller.Saved!=null && controller.Active!=null,"ApplySave failed to commit successful input");
                    Note("PASS video4: fresh="+observations+" queriedJoints="+queriedJoints+" knownRegions="+knownRegions+" currentCount="+sdk.GetUsersCount()+" ageMs="+sdk.ResultAgeMilliseconds.ToString("F1"));
                    Note("WAIT_VISUAL: inspect screenshot then run Continue probe"); phase=10;
                } else if(phase==1) {
                    Check(sdk.GetMaxBodies()==2 && sdk.GetUsersCount()<=2,"Capacity not applied");
                    Note("PASS regions2: fresh="+observations+" knownRegions="+knownRegions+" count="+sdk.GetUsersCount());
                    sdk.StartCoroutine(sdk.StopSdk()); phase=2; deadline=EditorApplication.timeSinceStartup+30;
                } else {
                    UnityEngine.Object.Destroy(sdk.gameObject); phase=4; deadline=EditorApplication.timeSinceStartup+30;
                }
            } else if(phase==2 && !sdk.Busy) {
                Check(!sdk.IsInitialized && !sdk.HasFreshResult && !sdk.TryGetRegionOccupancy(0,out _),"Stop did not clear state");
                Note("PASS safe stop and unknown occupancy");
                var data=controller.Draft; data.Recognition.MaxBodies=2;data.Recognition.UseRegions=true;data.Recognition.Regions=HumanVisionSdkConfiguration.CreateEqualRegions(2);
                controller.View.ShowDraft(data);controller.Execute("Apply");phase=3;observations=queriedJoints=knownRegions=0;previous=0;deadline=EditorApplication.timeSinceStartup+120;
            } else if(phase==4 && !UnityEngine.Object.FindObjectsOfType<HumanVisionManager>().Any(m=>m.gameObject.name.EndsWith("Runtime Host"))) {
                Note("PASS destroy while video streaming: no Runtime Host remains\nCOMPLETE PASS");EditorApplication.update-=Tick;
            }
        } catch(Exception e) { Note("FAIL: "+e);EditorApplication.update-=Tick;Debug.LogException(e); }
    }
    [MenuItem("HumanVision/Verification/Continue SDK API probe")]
    public static void Continue()
    {
        Check(phase==10,"Probe is not waiting for visual inspection");
        var data=sdk.Configuration.Recognition;data.MaxBodies=2;data.UseRegions=true;data.Regions=HumanVisionSdkConfiguration.CreateEqualRegions(2);
        Check(sdk.TryApplyConfiguration(data),sdk.LastError);phase=1;observations=queriedJoints=knownRegions=0;previous=0;deadline=EditorApplication.timeSinceStartup+120;
    }
}
