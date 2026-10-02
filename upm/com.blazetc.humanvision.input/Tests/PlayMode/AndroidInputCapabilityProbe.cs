using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
namespace HumanVision.Input.Tests
{
    // Input-only bounded device fixture. No inference assembly or native SDK.
    public sealed class AndroidInputCapabilityProbe : MonoBehaviour
    {
        [DllImport("humanvision_input")] private static extern void HV_Input_SelectHardwareCodec(string name);
        [DllImport("humanvision_input")] private static extern void HV_Input_StartCapabilityProbe(string url);
        [DllImport("humanvision_input")] private static extern void HV_Input_StopCapabilityProbe();
        [DllImport("humanvision_input")] private static extern void HV_Input_EnableColorProbe();
        [DllImport("humanvision_input")] private static extern IntPtr HV_Input_GetCapabilityProbeHandle();
        [DllImport("humanvision_input")] private static extern int HV_Input_BindUnityTarget(IntPtr handle,IntPtr texture,uint width,uint height,ulong generation);
        [DllImport("humanvision_input")] private static extern IntPtr HV_Input_GetRenderEventFunc();
        [DllImport("humanvision_input")] private static extern int HV_Input_GetColorProbeEventId();
        [DllImport("humanvision_input")] private static extern ulong HV_Input_GetColorCompletedSequence();
        [DllImport("humanvision_input")] private static extern int HV_Input_SetColorProbeTransform(int rotation,int mirror);
        [DllImport("humanvision_input")] private static extern void HV_Input_LogColorProbeCounters();
        [DllImport("humanvision_input")] private static extern void HV_Input_RequestColorProbeDrain();
        [DllImport("humanvision_input")] private static extern int HV_Input_ColorProbeRetired();
        [DllImport("humanvision_input")] private static extern int HV_Input_GetColorProbeGeometry(out uint width,out uint height,out ulong generation);
        [DllImport("humanvision_input")] private static extern void HV_Input_RetireColorProbeTarget();
        [DllImport("humanvision_input")] private static extern int HV_Input_ColorProbeTargetRetired();
        [DllImport("humanvision_input")] private static extern int HV_Input_GetLastError(IntPtr handle,[Out] byte[] utf8,uint capacity);
        [DllImport("humanvision_input")] private static extern int HV_Input_ColorProbeActive();
        [DllImport("humanvision_input")] private static extern void HV_Input_RequestCapabilityProbeStop();
        [DllImport("humanvision_input")] private static extern int HV_Input_TryFinishCapabilityProbeStop();
        [DllImport("humanvision_input")] private static extern void HV_Input_FailNextColorTargetView();
        private RenderTexture cleanupTexture;
        private Texture2D cleanupReadback;
        private bool viewFailure;
        private bool cleanupFault;
        private bool color;
        private bool startupTimeout;
        private Texture preview;
        private GUIStyle diagnosticStyle;
        private string displayStatus="Starting controlled RTSP TCP input";
        private void OnGUI()
        {
            float margin=Mathf.Max(12,Screen.width*.02f),header=Mathf.Max(100,Screen.height*.16f);
            GUI.Box(new Rect(0,0,Screen.width,Screen.height),GUIContent.none);
            if(diagnosticStyle==null)diagnosticStyle=new GUIStyle(GUI.skin.label){fontSize=Mathf.Clamp(Screen.height/40,22,40),wordWrap=true};
            GUI.Label(new Rect(margin,margin,Screen.width-2*margin,header),"Input GPU Color Test / No skeleton yet\n"+displayStatus,diagnosticStyle);
            if(preview!=null)GUI.DrawTexture(new Rect(margin,header,Screen.width-2*margin,Screen.height-header-margin),preview,ScaleMode.ScaleToFit,false);
        }
        private void OnDestroy(){if(preview is Texture2D)Destroy(preview);preview=null;}
        private IEnumerator Start()
        {
            yield return null;
#if UNITY_ANDROID && !UNITY_EDITOR
            try
            {
                using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                    if (version.GetStatic<int>("SDK_INT") < 29)
                        throw new InvalidOperationException("Hardware accelerated codec identity requires Android API29 device query; native remains API26.");
                string selected = null;
                using (var list = new AndroidJavaObject("android.media.MediaCodecList", 0))
                {
                    foreach (var codec in list.Call<AndroidJavaObject[]>("getCodecInfos"))
                    using (codec)
                    {
                        if (codec.Call<bool>("isEncoder") || !codec.Call<bool>("isHardwareAccelerated")) continue;
                        foreach (string type in codec.Call<string[]>("getSupportedTypes"))
                        {
                            if (type != "video/avc") continue;
                            using (var capability = codec.Call<AndroidJavaObject>("getCapabilitiesForType", type))
                            using (var video = capability.Call<AndroidJavaObject>("getVideoCapabilities"))
                            {
                                if (!video.Call<bool>("areSizeAndRateSupported", 640, 360, 25.0)) continue;
                                selected = codec.Call<string>("getName");
                                Debug.Log("HVInputGate selected_hardware_codec=" + selected + " supports_640x360_25=true");
                                break;
                            }
                        }
                        if (selected != null) break;
                    }
                }
                if (selected == null) throw new InvalidOperationException("No hardware AVC decoder supporting controlled fixture.");
                HV_Input_SelectHardwareCodec(selected);
                string mode=Resources.Load<TextAsset>("input-gate-mode").text.Trim();cleanupFault=mode=="CleanupFault";viewFailure=mode=="ViewFailure";color=mode=="Color"||cleanupFault||viewFailure;startupTimeout=mode=="StartupTimeout";
                if(color||startupTimeout)HV_Input_EnableColorProbe();
                if(startupTimeout)displayStatus="Negative startup test: waiting for configured no-keyframe timeout (no GPU input admitted)";
                HV_Input_StartCapabilityProbe(Resources.Load<TextAsset>("input-gate-url").text.Trim());
            }
            catch (Exception exception) {displayStatus="FAIL: "+exception.GetType().Name; Debug.LogError("HVInputGate capability_result=FAIL managed_stage=" + exception.GetType().Name); }
#else
            Debug.LogError("HVInputGate capability_result=FAIL requires_actual_Android_player");
#endif
            if(color) {
                IEnumerator routine=RunColor();bool running=true,failed=false;
                while(running){try{running=routine.MoveNext();}catch(Exception exception){displayStatus="FAIL: "+exception.Message;Debug.LogError("HVInputGate color_result=FAIL managed_stage="+exception.GetType().Name);running=false;failed=true;}if(running)yield return routine.Current;}
                if(failed)yield return CleanupColorError();
            }
            else if(startupTimeout)yield return RunStartupTimeout();
            else yield return new WaitForSeconds(15);
#if UNITY_ANDROID && !UNITY_EDITOR
            HV_Input_StopCapabilityProbe();
            if(startupTimeout)HV_Input_LogColorProbeCounters();
#endif
            Debug.Log("HVInputGate bounded_probe_closed=true");
        }
        private IEnumerator CleanupColorError()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            // No synchronous join while a reader lease depends on render Poll.
            preview=null;
            HV_Input_RequestColorProbeDrain();
            HV_Input_RequestCapabilityProbeStop();
            IntPtr callback=HV_Input_GetRenderEventFunc();int eventId=HV_Input_GetColorProbeEventId();
            var end=new WaitForEndOfFrame();int frames=0;
            while(HV_Input_ColorProbeRetired()==0||HV_Input_TryFinishCapabilityProbeStop()==0)
            {
                GL.IssuePluginEvent(callback,eventId);++frames;yield return end;
                displayStatus="ERROR: retiring GPU resources asynchronously / responsive frame "+frames;
            }
            HV_Input_RetireColorProbeTarget();
            while(HV_Input_ColorProbeTargetRetired()==0){GL.IssuePluginEvent(callback,eventId);++frames;yield return end;}
            if(cleanupTexture!=null){cleanupTexture.Release();Destroy(cleanupTexture);cleanupTexture=null;}
            if(cleanupReadback!=null){Destroy(cleanupReadback);cleanupReadback=null;}
            HV_Input_LogColorProbeCounters();
            displayStatus="EXPECTED ERROR / GPU resources retired / COMPLETE";
            Debug.Log("HVInputGate error_cleanup_complete=true responsive_retirement_frames="+frames+" native_target_unbound_before_destroy=true");
#else
            yield break;
#endif
        }
        private IEnumerator RunStartupTimeout()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            var message=new byte[512];bool shown=false;float until=Time.realtimeSinceStartup+15;
            while(Time.realtimeSinceStartup<until){
                if(!shown&&HV_Input_GetLastError(HV_Input_GetCapabilityProbeHandle(),message,(uint)message.Length)==0&&message[0]!=0){int end=Array.IndexOf(message,(byte)0);string reason=Encoding.UTF8.GetString(message,0,end<0?message.Length:end);displayStatus="EXPECTED ERROR: "+reason+" / no GPU frame admitted";Debug.Log("HVInputGate visible_startup_error=true reason="+reason);shown=true;}
                yield return null;
            }
            if(!shown){displayStatus="FAIL: expected native startup timeout was not reported";Debug.LogError("HVInputGate visible_startup_error=false");}
#else
            yield break;
#endif
        }
        [Serializable] private sealed class ColorFixture {public int expectedWidth,expectedHeight,inset;public string matrix,range;public bool crop;}
        private bool ColorPixels(RenderTexture texture,Texture2D readback,ColorFixture fixture,int rotation,int mirror,ulong sequence)
        {
            RenderTexture old=RenderTexture.active;RenderTexture.active=texture;
            readback.ReadPixels(new Rect(0,0,texture.width,texture.height),0,0);readback.Apply();RenderTexture.active=old;
            int inset=fixture.inset,w=fixture.expectedWidth,h=fixture.expectedHeight;
            var points=new[]{new Vector2Int(inset,inset),new Vector2Int(w-inset,inset),new Vector2Int(inset,h-inset),new Vector2Int(w-inset,h-inset),new Vector2Int(96+inset,inset),new Vector2Int(144+inset,inset)};
            var reference=new[]{new Color(1,0,0),new Color(0,0.5f,0),new Color(0,0,1),Color.white,new Color(128f/255,128f/255,128f/255),Color.black};bool pass=true;
            if(fixture.crop){Array.Resize(ref points,8);Array.Resize(ref reference,8);points[6]=new Vector2Int(92,inset);points[7]=new Vector2Int(140,inset);reference[6]=new Color(128f/255,128f/255,128f/255);reference[7]=Color.black;}
            for(int i=0;i<points.Length;++i)
            {
                float x=(float)points[i].x/w,y=(float)points[i].y/h;
                if(rotation==90){float temp=x;x=1-y;y=temp;}else if(rotation==180){x=1-x;y=1-y;}else if(rotation==270){float temp=x;x=y;y=1-temp;}
                if(mirror!=0)x=1-x;
                int px=Mathf.Clamp((int)(x*texture.width),0,texture.width-1),py=Mathf.Clamp((int)((1-y)*texture.height),0,texture.height-1);
                Color actual=readback.GetPixel(px,py);float error=Mathf.Max(Mathf.Abs(actual.r-reference[i].r),Mathf.Abs(actual.g-reference[i].g),Mathf.Abs(actual.b-reference[i].b));pass&=error<0.07f;
                Debug.Log("HVInputGate actual_gpu_pixel sample="+i+" matrix="+fixture.matrix+" range="+fixture.range+" rotation="+rotation+" mirror="+mirror+" x="+px+" y="+py+" rgb="+actual.r.ToString("F5")+","+actual.g.ToString("F5")+","+actual.b.ToString("F5")+" max_error="+error.ToString("F5")+" sequence="+sequence);
            }
            Debug.Log("HVInputGate color_case_pixels="+(pass?"PASS":"FAIL")+" rotation="+rotation+" mirror="+mirror+" actual_width="+texture.width+" actual_height="+texture.height);return pass;
        }
        private IEnumerator RunColor()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            uint width=0,height=0;ulong generation=0;float deadline=Time.realtimeSinceStartup+8;
            while(HV_Input_GetColorProbeGeometry(out width,out height,out generation)!=0&&Time.realtimeSinceStartup<deadline)yield return null;
            if(width==0||height==0)throw new InvalidOperationException("Actual decoded geometry unavailable");
            var fixture=JsonUtility.FromJson<ColorFixture>(Resources.Load<TextAsset>("input-color-fixture").text);
            if(width!=fixture.expectedWidth||height!=fixture.expectedHeight)throw new InvalidOperationException("Actual decoder display/crop differs from encoded fixture contract");
            IntPtr callback=HV_Input_GetRenderEventFunc();int eventId=HV_Input_GetColorProbeEventId();var end=new WaitForEndOfFrame();
            bool all=true;int diagnosticReadbacks=0;RenderTexture texture=null;Texture2D readback=null;
            foreach(int mirror in new[]{0,1})foreach(int rotation in new[]{0,90,180,270})
            {
                if(texture!=null)
                {
                    preview=null;
                    HV_Input_RequestColorProbeDrain();deadline=Time.realtimeSinceStartup+5;
                    while(HV_Input_ColorProbeRetired()==0&&Time.realtimeSinceStartup<deadline){GL.IssuePluginEvent(callback,eventId);yield return end;}
                    if(HV_Input_ColorProbeRetired()==0)throw new InvalidOperationException("GPU conversion retirement timed out");
                    HV_Input_RetireColorProbeTarget();deadline=Time.realtimeSinceStartup+5;
                    while(HV_Input_ColorProbeTargetRetired()==0&&Time.realtimeSinceStartup<deadline){GL.IssuePluginEvent(callback,eventId);yield return end;}
                    if(HV_Input_ColorProbeTargetRetired()==0)throw new InvalidOperationException("Native target view retirement not complete");
                    texture.Release();Destroy(texture);Destroy(readback);cleanupTexture=null;cleanupReadback=null;
                }
                bool swap=rotation==90||rotation==270;uint tw=swap?height:width,th=swap?width:height;
                texture=new RenderTexture((int)tw,(int)th,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear){enableRandomWrite=true};texture.Create();cleanupTexture=texture;
                // Complete Unity's first-use initialization before an external
                // submission writes this newly allocated target.
                RenderTexture previousTarget=RenderTexture.active;RenderTexture.active=texture;GL.Clear(false,true,Color.magenta);RenderTexture.active=previousTarget;yield return end;
                Debug.Log("HVInputGate unity_target_initialized_before_bind=true width="+tw+" height="+th+" generation="+generation+" rotation="+rotation+" mirror="+mirror);
                readback=new Texture2D((int)tw,(int)th,TextureFormat.RGBA32,false,true);cleanupReadback=readback;
                if(HV_Input_SetColorProbeTransform(rotation,mirror)!=0||HV_Input_BindUnityTarget(HV_Input_GetCapabilityProbeHandle(),texture.GetNativeTexturePtr(),tw,th,generation)!=0)throw new InvalidOperationException("Actual target/transform bind failed");
                displayStatus="Controlled RTSP TCP / BT."+fixture.matrix+" "+fixture.range+" / rotation "+rotation+" / mirror "+mirror+" / waiting for first GPU completion";
                HV_Input_EnableColorProbe();ulong before=HV_Input_GetColorCompletedSequence();bool checkedPixels=false;
                deadline=Time.realtimeSinceStartup+(diagnosticReadbacks==0?8:0.9f);
                while(Time.realtimeSinceStartup<deadline)
                {
                    GL.IssuePluginEvent(callback,eventId);yield return end;if(cleanupFault&&HV_Input_ColorProbeActive()!=0){Debug.Log("HVInputGate cleanup_fault_injected=true actual_gpu_active=1");throw new InvalidOperationException("Injected diagnostic exception with actual submitted GPU lease");}ulong sequence=HV_Input_GetColorCompletedSequence();
                    if(sequence>before&&preview==null){preview=texture;Debug.Log("HVInputGate live_gpu_preview_bound=true sequence="+sequence+" rotation="+rotation+" mirror="+mirror);}
                    if(sequence>=before+3&&!checkedPixels){bool passed=ColorPixels(texture,readback,fixture,rotation,mirror,sequence);all&=passed;displayStatus="BT."+fixture.matrix+" "+fixture.range+" / rotation "+rotation+" / mirror "+mirror+" / "+(passed?"PASS":"FAIL")+" / GPU sequence "+sequence;checkedPixels=true;++diagnosticReadbacks;
                        if(viewFailure&&diagnosticReadbacks==1){
                            HV_Input_RequestColorProbeDrain();
                            while(HV_Input_ColorProbeRetired()==0){GL.IssuePluginEvent(callback,eventId);yield return end;}
                            HV_Input_FailNextColorTargetView();
                            if(HV_Input_BindUnityTarget(HV_Input_GetCapabilityProbeHandle(),texture.GetNativeTexturePtr(),tw,th,generation)!=0)throw new InvalidOperationException("Fault test target rebind failed");
                            HV_Input_EnableColorProbe();
                        }
                    }
                }
                all&=checkedPixels;
            }
            preview=null;HV_Input_RequestColorProbeDrain();deadline=Time.realtimeSinceStartup+5;
            while(HV_Input_ColorProbeRetired()==0&&Time.realtimeSinceStartup<deadline){GL.IssuePluginEvent(callback,eventId);yield return end;}
            if(HV_Input_ColorProbeRetired()==0)throw new InvalidOperationException("GPU conversion retirement timed out");
            HV_Input_RequestCapabilityProbeStop();
            while(HV_Input_TryFinishCapabilityProbeStop()==0){GL.IssuePluginEvent(callback,eventId);yield return end;}
            HV_Input_RetireColorProbeTarget();deadline=Time.realtimeSinceStartup+5;
            while(HV_Input_ColorProbeTargetRetired()==0&&Time.realtimeSinceStartup<deadline){GL.IssuePluginEvent(callback,eventId);yield return end;}
            if(HV_Input_ColorProbeTargetRetired()==0)throw new InvalidOperationException("Final native target view not retired");HV_Input_LogColorProbeCounters();
            Debug.Log("HVInputGate diagnostic_gpu_readbacks="+diagnosticReadbacks+" production_cpu_image_readbacks=0 color_pixels="+(all&&diagnosticReadbacks==8?"PASS":"FAIL"));
            displayStatus=(all&&diagnosticReadbacks==8?"PASS":"FAIL")+" / 8 color-transform cases / STATIC TEST SNAPSHOT / COMPLETE";
            preview=readback;
            Debug.Log("HVInputGate visible_diagnostic_completed=true native_target_unbound_before_destroy=true");
            Debug.Log("HVInputGate color_result="+(all&&diagnosticReadbacks==8?"COMPLETE":"FAIL"));texture.Release();Destroy(texture);cleanupTexture=null;cleanupReadback=null;
#else
            yield break;
#endif
        }
    }
}
