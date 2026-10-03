using System;
using System.Collections;
using System.Runtime.InteropServices;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
public sealed class Task9AdapterGate : MonoBehaviour {
 [DllImport("humanvision_input")] static extern void HV_Input_LogGpuCounters();
 [DllImport("humanvision")] static extern void HV_Task8GpuSnapshot([Out] ulong[] values,uint count);
 RtspFrameSource source; HumanVisionManager sdk; HumanVisionInputAdapter adapter;
 string status="Opening independent hardware RTSP preview";GUIStyle label;Texture preview;
 long previewId=-1,lastSequence;int previewFrames,observations,visibleBones;float opened,lastLog;bool detached;
 readonly double[] presentedAt=new double[8192];readonly long[] presentedIds=new long[8192],presentedPts=new long[8192];readonly int[] presentedTextures=new int[8192];
 readonly int[] presentedPhases=new int[8192];int phase;
 int presentedCount,updateCount,repaintCount,lastUpdates,lastRepaints,lastPresented;long previewPts=-1;double timingWindow;float presentationRate,renderRate;double maximumGap;long skippedFrames;
 static readonly int[] edges={5,6,5,7,7,9,6,8,8,10,5,11,6,12,11,12,11,13,13,15,12,14,14,16};
 IEnumerator Start(){var routine=Run();bool next=true;while(next){try{next=routine.MoveNext();}catch(Exception e){status="FAIL: "+e.Message;Debug.LogError("HVTask9 result=FAIL reason="+e);next=false;}if(next)yield return routine.Current;}DumpPresentation();}
 IEnumerator Run(){
  Application.targetFrameRate=60;Debug.Log("HVTask9 fixture_target_frame_rate=60; inference profile/cadence unchanged");
  opened=Time.realtimeSinceStartup;source=new GameObject("Independent RTSP source").AddComponent<RtspFrameSource>();
  source.Open(new RtspSourceSettings{Location=Resources.Load<TextAsset>("input-gate-url").text.Trim(),RequestedWidth=640,RequestedHeight=360,OpenTimeoutMs=5000,ReconnectDelayMs=250,Transport=RtspTransport.Tcp});
  sdk=new GameObject("Actual model runtime").AddComponent<HumanVisionManager>();adapter=new GameObject("Unified SDK adapter").AddComponent<HumanVisionInputAdapter>();adapter.manager=sdk;adapter.Bind(source);
  string root=null,error=null;status="Hardware preview independent; extracting unchanged qualified model data";
  yield return HumanVisionRuntimeData.Prepare(p=>root=p,e=>error=e);if(error!=null)throw new Exception(error);
  status="Independent preview timing baseline: SDK not initialized";yield return new WaitForSeconds(3);
  status="Loading android-ncnn-vulkan models; preview remains independent";yield return null;
  if(!sdk.TryInitialize(new HumanVisionConfig{RuntimeRoot=root,Profile="android-ncnn-vulkan",MaxBodies=8}))throw new Exception(sdk.LastError);
  phase=1;
  status="Real recognition; fresh complete observations counted separately from presentation";
  float deadline=Time.realtimeSinceStartup+65;
  while(Time.realtimeSinceStartup<deadline && (adapter.CopiedFrames<250||observations<20||visibleBones<100)){
   if(source.State==InputSourceState.Error)throw new Exception(source.LastError);
   if(!string.IsNullOrEmpty(sdk.LastError))throw new Exception(sdk.LastError);
   if(sdk.ResultSequence!=lastSequence){lastSequence=sdk.ResultSequence;if(sdk.BodyCount>0&&adapter.CanPresentResult(sdk.SourceFrameId)){observations++;for(int i=0;i<sdk.BodyCount;i++)for(int j=0;j<edges.Length;j+=2)if(sdk.Bodies[i].Joints[edges[j]].Valid&&sdk.Bodies[i].Joints[edges[j+1]].Valid)visibleBones++;}}
   if(Time.realtimeSinceStartup-lastLog>1){lastLog=Time.realtimeSinceStartup;Debug.Log("HVTask9 live preview="+previewId+" copied="+adapter.CopiedFrames+" dropped_unsubmitted="+adapter.DroppedUnsubmittedFrames+" result_sequence="+sdk.ResultSequence+" source_frame="+sdk.SourceFrameId+" bodies="+sdk.BodyCount+" observations="+observations+" visible_bones="+visibleBones+" adapter_error="+adapter.LastError);HV_Input_LogGpuCounters();}
   yield return null;
  }
  if(adapter.CopiedFrames<250||observations<20||visibleBones<100)throw new Exception("Continuous copy/real human recognition threshold missing: "+adapter.CopiedFrames+"/"+observations+"/"+visibleBones+"; "+adapter.LastError+"; "+sdk.RuntimeDiagnostics);
  Debug.Log("HVTask9 recognition_ready copied="+adapter.CopiedFrames+" observations="+observations+" visible_bones="+visibleBones+" profile="+sdk.ActiveRuntimeProfile+"\n"+sdk.RuntimeDiagnostics);
  LogRuntimeStats();
  yield return new WaitForSeconds(3);long before=previewId;adapter.Detach();detached=true;phase=2;status="SDK detached; independent RTSP preview continues; GPU copies retiring";
  deadline=Time.realtimeSinceStartup+15;while((adapter.RetirementPending||previewId<before+30)&&Time.realtimeSinceStartup<deadline)yield return null;
  if(adapter.RetirementPending||previewId<before+30)throw new Exception("Detach retirement/independent preview failed");
  Debug.Log("HVTask9 detach_preview_continues before="+before+" after="+previewId+" retirement_pending=0");yield return new WaitForSeconds(3);
  source.Close();deadline=Time.realtimeSinceStartup+15;while(source.State!=InputSourceState.Stopped&&Time.realtimeSinceStartup<deadline)yield return null;
  if(source.State!=InputSourceState.Stopped)throw new Exception("Input source close incomplete");sdk.Shutdown();yield return null;yield return null;HV_Input_LogGpuCounters();
  var snapshot=new ulong[8];HV_Task8GpuSnapshot(snapshot,(uint)snapshot.Length);Debug.Log("HVTask9 terminal_sdk_snapshot="+string.Join(",",snapshot));if(snapshot[0]!=0||snapshot[1]!=0||snapshot[2]!=0||snapshot[3]!=0||snapshot[4]!=0||snapshot[7]!=0)throw new Exception("SDK resource baseline not restored");
  status="PASS: real video + skeleton recognition; test source ended and disconnected";Debug.Log("HVTask9 result=PASS preview_frames="+previewFrames+" observations="+observations+" visible_bones="+visibleBones);
 }
 void LogRuntimeStats(){var property=typeof(HumanVisionManager).GetProperty("RuntimeStatsV2",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic);if(property==null){Debug.Log("HVTask9 actual_stats unavailable=RuntimeStatsV2 getter missing");return;}object stats=property.GetValue(sdk);var type=stats.GetType();string text="HVTask9 actual_stats";
  foreach(string name in new[]{"FreshObservationFrames","OutputSamples","SourceFrameId","CaptureTimestampUs","PublicationTimestampUs","GpuCaptureFps","FreshObservationFps","OutputSamplingFps","AgeP50Ms","AgeP95Ms","PoseAgeP50Ms","PoseAgeP95Ms","SensorCaptureAgeP50Ms","SensorCaptureAgeP95Ms","SourceFramesSeen","SourceRateLimitedDrops","GpuBridgeNoFreeSlotDrops","GpuBridgeSupersededReadyDrops","GpuCopyErrors","GpuImportErrors","CaptureProvenance","PoseValidationFailures"}){var field=type.GetField(name);text+=" "+name+"="+(field==null?"unavailable":field.GetValue(stats).ToString());}Debug.Log(text);}
 void Update(){updateCount++;if(source!=null){preview=source.CurrentTexture;if(source.TryGetLatestFrame(previewId,out var f)){preview=f.Texture;previewId=f.FrameId;previewPts=f.PresentationTimestampUs;previewFrames++;}}
  double now=Time.realtimeSinceStartupAsDouble;if(now-timingWindow>=1){double seconds=now-timingWindow;renderRate=(float)((repaintCount-lastRepaints)/seconds);presentationRate=(float)((presentedCount-lastPresented)/seconds);
   Debug.Log("HVTask9 presentation phase="+phase+" update_fps="+((updateCount-lastUpdates)/seconds)+" repaint_fps="+renderRate+" new_frame_repaint_fps="+presentationRate+" frame="+previewId+" pts_us="+previewPts+" texture="+(preview==null?0:preview.GetInstanceID())+" max_gap_ms="+(maximumGap*1000)+" skipped="+skippedFrames+" pending_leases="+(adapter==null?0:adapter.PendingSourceCopies));
   timingWindow=now;lastUpdates=updateCount;lastRepaints=repaintCount;lastPresented=presentedCount;}}
 void ObservePresentation(){repaintCount++;if(presentedCount>=presentedAt.Length||(presentedCount>0&&presentedIds[presentedCount-1]==previewId))return;double now=Time.realtimeSinceStartupAsDouble;
  if(presentedCount>0){maximumGap=Math.Max(maximumGap,now-presentedAt[presentedCount-1]);skippedFrames+=Math.Max(0,previewId-presentedIds[presentedCount-1]-1);}
  presentedAt[presentedCount]=now;presentedIds[presentedCount]=previewId;presentedPts[presentedCount]=previewPts;presentedTextures[presentedCount]=preview.GetInstanceID();presentedPhases[presentedCount]=phase;presentedCount++;}
 void DumpPresentation(){for(int i=0;i<presentedCount;i++)Debug.Log("HVTask9 presented index="+i+" phase="+presentedPhases[i]+" time_s="+presentedAt[i].ToString("R",System.Globalization.CultureInfo.InvariantCulture)+" frame="+presentedIds[i]+" pts_us="+presentedPts[i]+" texture="+presentedTextures[i]);}
 void OnGUI(){if(label==null)label=new GUIStyle(GUI.skin.label){fontSize=24,wordWrap=true};GUI.Box(new Rect(0,0,Screen.width,Screen.height),GUIContent.none);
  GUI.Label(new Rect(15,10,Screen.width-30,170),"Task9 RTSP + actual SDK recognition\nTemporary PC publisher and USB reverse required\n"+status+"\nUnity repaint: "+renderRate.ToString("F1")+"/s; new preview frames drawn: "+presentationRate.ToString("F1")+"/s; observations: "+observations+"; bodies: "+(sdk==null?0:sdk.BodyCount)+"\n"+(adapter==null?"":adapter.LastError)+"\nSource: "+(source==null?"not opened":source.State+": "+source.LastError),label);
  if(preview==null)return;float w=Screen.width-30,h=w*preview.height/preview.width;var rect=new Rect(15,190,w,Mathf.Min(h,Screen.height-205));GUI.DrawTexture(rect,preview,ScaleMode.ScaleToFit,false);
  if(Event.current.type==EventType.Repaint)ObservePresentation();
  if(detached||sdk==null||adapter==null||!adapter.CanPresentResult(sdk.SourceFrameId))return;
  float scale=Mathf.Min(rect.width/preview.width,rect.height/preview.height);Vector2 origin=new Vector2(rect.x+(rect.width-preview.width*scale)/2,rect.y+(rect.height-preview.height*scale)/2);
  for(int i=0;i<sdk.BodyCount;i++){var body=sdk.Bodies[i];for(int j=0;j<edges.Length;j+=2){var a=body.Joints[edges[j]];var b=body.Joints[edges[j+1]];if(a.Valid&&b.Valid)Line(origin+a.Pixel*scale,origin+b.Pixel*scale);}}
 }
 static void Line(Vector2 a,Vector2 b){var old=GUI.matrix;var color=GUI.color;GUI.color=Color.green;GUIUtility.RotateAroundPivot(Mathf.Atan2(b.y-a.y,b.x-a.x)*Mathf.Rad2Deg,a);GUI.DrawTexture(new Rect(a.x,a.y,(b-a).magnitude,3),Texture2D.whiteTexture);GUI.matrix=old;GUI.color=color;}
}
