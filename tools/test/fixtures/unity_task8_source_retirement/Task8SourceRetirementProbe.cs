using System;
using System.Collections;
using System.Diagnostics;
using System.Runtime.InteropServices;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Rendering;
using Debug = UnityEngine.Debug;

public sealed class Task8SourceRetirementProbe : MonoBehaviour
{
    [StructLayout(LayoutKind.Sequential)] private struct Submission {
        public uint Size,Version; public IntPtr Texture; public int Width,Height;
        public long FrameId,TimestampUs; public uint Rotation,Mirror;
    }
    [StructLayout(LayoutKind.Sequential)] private struct Retirement {
        public uint Size,Version; public ulong Generation,CopyToken;
    }
    [DllImport("humanvision")] private static extern int HV_Task8GpuBegin(IntPtr texture);
    [DllImport("humanvision")] private static extern int HV_Task8GpuPrepare(ref Submission frame,out IntPtr data);
    [DllImport("humanvision")] private static extern int HV_Task8GpuHoldNewest();
    [DllImport("humanvision")] private static extern uint HV_Task8GpuHeld();
    [DllImport("humanvision")] private static extern int HV_Task8GpuRetire(ref Retirement token);
    [DllImport("humanvision")] private static extern int HV_AndroidGpuPollSourceRetirement(ref Retirement token);
    [DllImport("humanvision")] private static extern int HV_Task8GpuReleaseHold();
    [DllImport("humanvision")] private static extern void HV_Task8GpuEnd();
    [DllImport("humanvision")] private static extern IntPtr HV_GetAndroidGpuRenderEventAndDataFunction();
    private sealed class CopyFence : ISourceCopyFence {
        internal Retirement Token;
        public bool IsComplete {
            get { int result=HV_AndroidGpuPollSourceRetirement(ref Token); if(result==1)return false;
                if(result!=0)throw new Exception("Native source-copy completion unproven: "+result);return true; }
        }
    }
    [DllImport("humanvision")] private static extern void HV_Task8GpuSnapshot([Out] ulong[] values,uint count);
    [DllImport("humanvision_input")] private static extern void HV_Input_LogGpuCounters();
    private RtspFrameSource source;
    private Texture preview;
    private string status="Opening controlled RTSP TCP stream; waiting for actual GPU frame";
    private GUIStyle label;
    private bool held;
    private void OnGUI() {
        GUI.Box(new Rect(0,0,Screen.width,Screen.height),GUIContent.none);
        if(label==null)label=new GUIStyle(GUI.skin.label){fontSize=28,wordWrap=true};
        GUI.Label(new Rect(20,20,Screen.width-40,160),"Source-copy retirement / no recognition\nTemporary PC stream + USB reverse required\n"+status,label);
        if(preview!=null)GUI.DrawTexture(new Rect(20,180,Screen.width-40,Screen.height-200),preview,ScaleMode.ScaleToFit,false);
    }
    private IEnumerator Start() {
        var run=Run(); bool next=true;
        while(next){try{next=run.MoveNext();}catch(Exception ex){status="FAIL: "+ex.Message;Debug.LogError("HVTask8 result=FAIL reason="+ex.Message);next=false;}if(next)yield return run.Current;}
    }
    private IEnumerator Run() {
        string url=Resources.Load<TextAsset>("input-gate-url").text.Trim();
        var settings=new RtspSourceSettings {Location=url,RequestedWidth=640,RequestedHeight=360,OpenTimeoutMs=3000,ReconnectDelayMs=250,Transport=RtspTransport.Tcp};
        source=new GameObject("Actual production RTSP source").AddComponent<RtspFrameSource>();source.Open(settings);
        float deadline=Time.realtimeSinceStartup+25;
        HumanVisionTextureFrame frame=default;
        while(!source.TryGetLatestFrame(-1,out frame)&&Time.realtimeSinceStartup<deadline){status=source.State+": "+source.LastError;yield return null;}
        if(frame.Texture==null)throw new Exception("Actual RTSP GPU frame timeout");
        preview=frame.Texture; status="Actual RTSP preview visible; native SDK copy initialization";
        Debug.Log("HVTask8 preview_ready generation="+frame.Generation+" frame="+frame.FrameId+" width="+frame.Width+" height="+frame.Height);
        yield return new WaitForSeconds(4);
        source.TryGetLatestFrame(-1,out frame);
        if(!source.TryAcquireSourceCopyLease(in frame,out var lease))throw new Exception("Actual source-copy lease unavailable");
        preview=frame.Texture;
        if(HV_Task8GpuBegin(frame.Texture.GetNativeTexturePtr())!=0)throw new Exception("SDK GPU source lease rejected");
        using(var command=new CommandBuffer()){ long id=1;deadline=Time.realtimeSinceStartup+25;
            while(!held&&Time.realtimeSinceStartup<deadline){
                var submission=new Submission {Size=48,Version=1,Texture=frame.Texture.GetNativeTexturePtr(),Width=frame.Width,Height=frame.Height,FrameId=id++,TimestampUs=(long)(Time.realtimeSinceStartupAsDouble*1000000)};
                int result=HV_Task8GpuPrepare(ref submission,out var data);
                if(data!=IntPtr.Zero){command.Clear();command.IssuePluginEventAndData(HV_GetAndroidGpuRenderEventAndDataFunction(),result==1?1:0,data);Graphics.ExecuteCommandBuffer(command);}
                if(result!=0&&result!=1)throw new Exception("Actual SDK GPU prepare rejected: "+result);
                held=HV_Task8GpuHoldNewest()==0;
                yield return null;
            }
        }
        if(!held||HV_Task8GpuHeld()!=1)throw new Exception("Real AHB/sync-fd consumer hold unavailable");
        var fence=new CopyFence();deadline=Time.realtimeSinceStartup+10;
        while(HV_Task8GpuRetire(ref fence.Token)==1&&Time.realtimeSinceStartup<deadline)yield return null;
        if(fence.Token.Generation==0)throw new Exception("Source retirement token unavailable");
        IntPtr oldTexturePointer=frame.Texture.GetNativeTexturePtr();
        lease.RetireAfter(fence);
        var watch=Stopwatch.StartNew();preview=null;source.Close();
        status="Closing source; actual AHB consumer lease intentionally remains held";
        deadline=Time.realtimeSinceStartup+10;
        while(source.State!=InputSourceState.Stopped&&Time.realtimeSinceStartup<deadline){if(HV_Task8GpuHeld()!=1)throw new Exception("Inference occupancy disappeared before Close");yield return null;}
        if(source.State!=InputSourceState.Stopped||!fence.IsComplete||HV_Task8GpuHeld()!=1)throw new Exception("Source Close did not complete independently of AHB hold");
        Debug.Log("HVTask8 close_complete copy_token="+fence.Token.CopyToken+" close_ms="+watch.Elapsed.TotalMilliseconds.ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+" native_copy_complete=1 consumer_held=1 source_stopped=1");
        status="PASS: source closed after native GPU copy completion; AHB consumer still held";
        // A retired source cannot issue a new copy, even while its AHB consumer survives.
        var oldSubmission=new Submission {Size=48,Version=1,Texture=oldTexturePointer,Width=frame.Width,Height=frame.Height,FrameId=900};
        if(HV_Task8GpuPrepare(ref oldSubmission,out var oldData)>=0||oldData!=IntPtr.Zero)throw new Exception("Retired source accepted an old frame");
        Debug.Log("HVTask8 old_generation_blocked=1");
        ulong oldGeneration=frame.Generation;source.Open(settings);deadline=Time.realtimeSinceStartup+20;
        HumanVisionTextureFrame newFrame=default;
        while(!source.TryGetLatestFrame(-1,out newFrame)&&Time.realtimeSinceStartup<deadline)yield return null;
        if(newFrame.Texture==null||newFrame.Generation==oldGeneration)throw new Exception("New source generation failed");
        preview=newFrame.Texture;
        if(!source.TryAcquireSourceCopyLease(in newFrame,out var newLease))throw new Exception("New source lease unavailable");
        if(HV_Task8GpuBegin(newFrame.Texture.GetNativeTexturePtr())!=0||HV_Task8GpuHeld()!=1)throw new Exception("New SDK admission blocked by old consumer");
        Debug.Log("HVTask8 new_sdk_admission=1 configuration_pending=1 consumer_held=1");
        Debug.Log("HVTask8 new_generation old="+oldGeneration+" current="+newFrame.Generation+" consumer_held=1");
        status="New source admitted; SDK configuration waits for held AHB consumer";
        yield return new WaitForSeconds(3);
        if(HV_Task8GpuReleaseHold()!=0)throw new Exception("Old AHB hold retirement failed");held=false;
        using(var command=new CommandBuffer()){long id=1000;deadline=Time.realtimeSinceStartup+25;
            while(!held&&Time.realtimeSinceStartup<deadline){
                var submission=new Submission {Size=48,Version=1,Texture=newFrame.Texture.GetNativeTexturePtr(),Width=newFrame.Width,Height=newFrame.Height,FrameId=id++};
                int result=HV_Task8GpuPrepare(ref submission,out var data);
                if(data!=IntPtr.Zero){command.Clear();command.IssuePluginEventAndData(HV_GetAndroidGpuRenderEventAndDataFunction(),result==1?1:0,data);Graphics.ExecuteCommandBuffer(command);}
                if(result!=0&&result!=1)throw new Exception("New SDK prepare rejected: "+result);
                held=HV_Task8GpuHoldNewest()==0;yield return null;
            }
        }
        if(!held||HV_Task8GpuHeld()!=1)throw new Exception("New generation actual GPU copy missing");
        var newFence=new CopyFence();deadline=Time.realtimeSinceStartup+10;
        while(HV_Task8GpuRetire(ref newFence.Token)==1&&Time.realtimeSinceStartup<deadline)yield return null;
        if(newFence.Token.Generation==0||newFence.Token.Generation<=fence.Token.Generation)throw new Exception("New native copy token missing");
        newLease.RetireAfter(newFence);
        while(!newFence.IsComplete&&Time.realtimeSinceStartup<deadline)yield return null;
        if(!newFence.IsComplete)throw new Exception("New native GPU copy completion missing");
        Debug.Log("HVTask8 new_sdk_copy_completed=1 copy_token="+newFence.Token.CopyToken);
        status="New SDK GPU copy completed; no recognition or model initialized";
        yield return new WaitForSeconds(4);
        if(HV_Task8GpuReleaseHold()!=0)throw new Exception("New AHB hold retirement failed");held=false;
        HV_Task8GpuEnd();preview=null;source.Close();
        deadline=Time.realtimeSinceStartup+10;while(source.State!=InputSourceState.Stopped&&Time.realtimeSinceStartup<deadline)yield return null;
        if(source.State!=InputSourceState.Stopped)throw new Exception("Terminal source retirement timed out");
        HV_Input_LogGpuCounters();
        var snapshot=new ulong[8];HV_Task8GpuSnapshot(snapshot,8);
        Debug.Log("HVTask8 sdk_terminal slots_live="+snapshot[0]+" source_views_live="+snapshot[1]+" consumer_held="+snapshot[2]+" consumer_fd_live="+snapshot[3]+" copy_errors="+snapshot[4]+" bridge_closed="+snapshot[5]+" successful_copies="+snapshot[6]+" consumer_ahb_live="+snapshot[7]);
        Debug.Log("HVTask8 result=PASS terminal_source_stopped=1 consumer_held="+HV_Task8GpuHeld());
    }
}
