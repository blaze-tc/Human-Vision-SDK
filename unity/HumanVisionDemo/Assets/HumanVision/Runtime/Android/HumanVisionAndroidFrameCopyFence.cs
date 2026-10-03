using System;
using System.Runtime.InteropServices;
using HumanVision.Input;
using HumanVision.Interop;
using UnityEngine;
namespace HumanVision {
internal enum AndroidCaptureProvenanceNative:uint { Unknown=0, UnityObserved=1, SensorVerified=2 }
[StructLayout(LayoutKind.Sequential,Pack=8)]
internal struct AndroidGpuFrameCopyTicketNative {
    internal uint Size,Version;
    internal ulong SourceId,SourceGeneration,BridgeGeneration;
    internal long FrameId;
    internal ulong Reservation,EventIdentity;
    internal uint Slot,Flags;
}
// Exactly one native reservation. Reset only after SourceCopyLease.IsRetired.
internal sealed class HumanVisionAndroidFrameCopyFence:ISourceCopyFence {
    internal AndroidGpuFrameCopyTicketNative Token;
    private Texture source;
    private RenderTexture target;
    private bool complete;
    internal bool Active {get;private set;}
    internal uint Outcome {get;private set;}
    internal void Bind(AndroidGpuFrameCopyTicketNative ticket,Texture preview,RenderTexture normalized) {
        if(Active)throw new InvalidOperationException("The prior source-copy lease has not been consumed.");
        Token=ticket;source=preview;target=normalized;Active=true;complete=false;Outcome=0;
    }
    public bool IsComplete {
        get {
            if(!Active||complete)return true;
            int result=RuntimeBindings.HV_AndroidGpuPollFrameCopy(ref Token,out uint outcome);
            if(result==1)return false;
            if(result!=0)throw new HumanVisionException("poll source frame copy",result,"Native source-copy proof unavailable; retain the source and normalized texture.");
            if(outcome!=1&&outcome!=2)throw new InvalidOperationException("Native source-copy outcome is invalid.");
            Outcome=outcome;complete=true;return true;
        }
    }
    internal void ReleaseAcknowledgedLease() {
        if(!IsComplete)throw new InvalidOperationException("Cannot reuse an incomplete source-copy fence.");
        Active=false;source=null;target=null;
    }
}
}
