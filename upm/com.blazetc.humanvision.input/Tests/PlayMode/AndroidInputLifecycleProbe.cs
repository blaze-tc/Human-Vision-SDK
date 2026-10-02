using System;
using System.Collections;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;
namespace HumanVision.Input.Tests
{
    public sealed class AndroidInputLifecycleProbe : MonoBehaviour
    {
        private RtspFrameSource source;
        private string url, status = "Opening real RTSP TCP GPU source";
        private Texture preview;
        private long after;
        private int frames, transitions, overlaps;
        private bool failed;
        [DllImport("humanvision_input")] private static extern int HV_Input_QueueDiagnosticSourceCopy(IntPtr h,IntPtr source,IntPtr destination);
        [DllImport("humanvision_input")] private static extern int HV_Input_DiagnosticSourceCopyComplete();
        [DllImport("humanvision_input")] private static extern int HV_Input_RetireDiagnosticSourceCopy();
        private sealed class CopyFence : ISourceCopyFence { public bool IsComplete => HV_Input_DiagnosticSourceCopyComplete() != 0; }
        private GUIStyle label;
        private void OnGUI() {
            GUI.Box(new Rect(0, 0, Screen.width, Screen.height), GUIContent.none);
            if(label==null)label=new GUIStyle(GUI.skin.label){fontSize=30,wordWrap=true};GUI.Label(new Rect(20,20,Screen.width-40,140),"Input RTSP lifecycle / no recognition\n"+status,label);
            if(preview!=null)GUI.DrawTexture(new Rect(20,160,Screen.width-40,Screen.height-180),preview,ScaleMode.ScaleToFit,false);
        }
        private RtspSourceSettings Settings() => new RtspSourceSettings { Location=url,RequestedWidth=640,RequestedHeight=360,OpenTimeoutMs=3000,ReconnectDelayMs=250,Transport=RtspTransport.Tcp };
        private IEnumerator Start() {
            url=Resources.Load<TextAsset>("input-gate-url").text.Trim();
            var routine=Run();bool running=true;
            while(running){try{running=routine.MoveNext();}catch(Exception ex){failed=true;status="FAIL: "+ex.Message;Debug.LogError("HVInputGate lifecycle_result=FAIL reason="+ex.GetType().Name+" detail="+ex.Message);running=false;}if(running)yield return routine.Current;}
            if(failed&&source!=null)source.Close();
            if(failed){float deadline=Time.realtimeSinceStartup+15;while(HV_Input_DiagnosticSourceCopyComplete()==0&&Time.realtimeSinceStartup<deadline)yield return null;HV_Input_RetireDiagnosticSourceCopy();}
        }
        private IEnumerator Run() {
            source=new GameObject("Production RTSP source").AddComponent<RtspFrameSource>();source.Open(Settings());
            float deadline=Time.realtimeSinceStartup+20;
            while(source.CurrentTexture==null&&Time.realtimeSinceStartup<deadline){status=source.State+": "+source.LastError;if(source.State==InputSourceState.Error)throw new Exception(source.LastError);yield return null;}
            if(source.CurrentTexture==null)throw new Exception("Production GPU publication timeout");
            var began=Time.realtimeSinceStartup;Debug.Log("HVInputGate lifecycle_playback_started actual_production_source=1");
            float played=0;float last=Time.realtimeSinceStartup;
            while(played<60){Observe();float now=Time.realtimeSinceStartup;if(source.State==InputSourceState.Streaming && source.CurrentTexture!=null)played+=Mathf.Min(now-last,.1f);last=now;if(now-began>120)throw new Exception("Streaming playback did not accumulate60s within bounded recovery window");if(source.State==InputSourceState.Error)throw new Exception(source.LastError);yield return null;}
            Debug.Log("HVInputGate lifecycle_playback_seconds="+played.ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+" published_frames="+frames);
            var held = new SourceCopyLease[3]; var heldTextures = new Texture[3]; int heldCount=0;
            deadline=Time.realtimeSinceStartup+10;
            while(heldCount<3 && Time.realtimeSinceStartup<deadline){
                Observe();
                if(source.TryGetLatestFrame(-1,out var heldFrame)){
                    bool unique=true;for(int j=0;j<heldCount;++j)if(heldTextures[j]==heldFrame.Texture)unique=false;
                    if(unique && source.TryAcquireSourceCopyLease(in heldFrame,out held[heldCount])){heldTextures[heldCount]=heldFrame.Texture;Debug.Log("HVInputGate real_output_slot_pinned index="+heldCount+" frame="+heldFrame.FrameId+" texture_id="+heldFrame.Texture.GetInstanceID());++heldCount;}
                }
                yield return null;
            }
            if(heldCount!=3)throw new Exception("Could not pin three actual published output slots");
            source.TryGetLatestFrame(-1,out var saturatedBefore);yield return new WaitForSeconds(.5f);source.TryGetLatestFrame(-1,out var saturatedAfter);
            if(saturatedAfter.FrameId!=saturatedBefore.FrameId)throw new Exception("Consumer-held native output slot was overwritten");
            NativeInputBindings.HV_Input_LogGpuCounters();Debug.Log("HVInputGate real_output_saturation_pass=1 held_slots=3 published_frame_unchanged=1");
            for(int j=0;j<3;++j)held[j].Dispose();
            RenderTexture copy=null;var fence=new CopyFence();
            for(int i=0;i<10;++i){
                deadline=Time.realtimeSinceStartup+15;
                while(source.CurrentTexture==null&&Time.realtimeSinceStartup<deadline){Observe();yield return null;}
                if(source.CurrentTexture==null)throw new Exception("Lifecycle reopen publication timeout");
                Observe();
                if(copy==null){copy = new RenderTexture(source.CurrentTexture.width,source.CurrentTexture.height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);copy.Create();}
                if(!source.TryGetLatestFrame(-1,out var frame)||!source.TryAcquireSourceCopyLease(in frame,out var lease))throw new Exception("Actual output copy lease unavailable");
                var previous=RenderTexture.active;RenderTexture.active=copy;GL.Clear(false,true,Color.clear);RenderTexture.active=previous;
                if(HV_Input_QueueDiagnosticSourceCopy(source.AndroidHandle,frame.Texture.GetNativeTexturePtr(),copy.GetNativeTexturePtr())!=0)throw new Exception("Actual diagnostic consumer source copy unavailable");
                lease.RetireAfter(fence);
                GL.IssuePluginEvent(NativeInputBindings.HV_Input_GetRenderEventFunc(),0x48564a);
                // Observe queued converter state after its render event, without polling it to completion.
                yield return new WaitForEndOfFrame();
                bool active=source.AndroidHandle!=IntPtr.Zero&&NativeInputBindings.HV_Input_GpuCopyActive(source.AndroidHandle)!=0;
                if(active)++overlaps;
                string mode=i%3==0?"disable":i%3==1?"close":"destroy";
                Debug.Log("HVInputGate lifecycle_transition="+i+" mode="+mode+" real_copy_fence_pending="+(!fence.IsComplete?1:0)+" actual_native_copy_active="+(active?1:0));
                preview=null;
                if(mode=="disable")source.enabled=false;else if(mode=="destroy")Destroy(source.gameObject);else source.Close();
                // The global retirement pump remains active after disabled/destroyed source component.
                var closeStart=Time.realtimeSinceStartup;
                while(Time.realtimeSinceStartup-closeStart<.5f||!fence.IsComplete){if(Time.realtimeSinceStartup-closeStart>15)throw new Exception("Source-copy fence retirement timeout");yield return null;}

                if(mode=="destroy"){source=new GameObject("Reopened production RTSP source").AddComponent<RtspFrameSource>();after=0;}
                else {deadline=Time.realtimeSinceStartup+15;while(source.State==InputSourceState.Closing&&Time.realtimeSinceStartup<deadline)yield return null;source.enabled=true;}
                source.Open(Settings());++transitions;
                deadline=Time.realtimeSinceStartup+15;while(source.CurrentTexture==null&&Time.realtimeSinceStartup<deadline){Observe();if(source.State==InputSourceState.Error)throw new Exception(source.LastError);yield return null;}
                if(source.CurrentTexture==null)throw new Exception("Lifecycle next generation did not publish");
            }
            FrameTextureNormalizer.Destroy(copy);
            source.Close();deadline=Time.realtimeSinceStartup+15;while(source.State==InputSourceState.Closing&&Time.realtimeSinceStartup<deadline)yield return null;
            if(source.State!=InputSourceState.Stopped)throw new Exception("Final source Close did not retire");
            if(HV_Input_RetireDiagnosticSourceCopy()!=0)throw new Exception("Actual diagnostic source copy resources still in flight");
            NativeInputBindings.HV_Input_LogGpuCounters();status="PASS: actual RTSP GPU lifecycle retired; temporary test stream ends after runner.";
            Debug.Log("HVInputGate lifecycle_result=PASS transitions="+transitions+" overlapping_native_copies="+overlaps+" published_frames="+frames+" cpu_image_readbacks=0");
        }
        private void Observe(){if(source==null)return;status=source.State+": "+source.LastError;if(source.TryGetLatestFrame(after,out var frame)){after=frame.FrameId;++frames;preview=frame.Texture;}}
    }
}
