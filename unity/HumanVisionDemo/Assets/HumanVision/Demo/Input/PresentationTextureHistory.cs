using System;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Demo {
    /// <summary>Eight owned GPU snapshots. Decoder leases end at copy completion, never at presentation or inference completion.</summary>
    internal sealed class PresentationTextureHistory : IDisposable {
        private sealed class Slot : ISourceCopyFence {
            internal RenderTexture texture;
            internal GraphicsFence fence;
            internal bool pending;
            internal SourceCopyLease lease;
            internal long frame=-1, time;
            public bool IsComplete { get { return !pending || fence.passed; } }
        }
        private readonly Slot[] slots=new Slot[8];
        private readonly CommandBuffer commands=new CommandBuffer { name="HumanVision presentation snapshot" };
        private long lastCapture=-1, lastPresented=-1;
        private Slot displayed;
        internal long FrameId=>displayed?.frame??-1;
        internal long TimestampUs=>displayed?.time??-1;
        internal Texture Texture=>displayed?.texture;
        internal PresentationTextureHistory(){for(int i=0;i<slots.Length;i++)slots[i]=new Slot();}
        internal void Capture(IHumanVisionFrameSource source,in HumanVisionTextureFrame frame,long timestamp) {
            if(frame.FrameId<=lastCapture || !SystemInfo.supportsGraphicsFence)return;
            Slot free=null;
            foreach(var s in slots) {
                if(s.pending && s.IsComplete && s.lease.IsRetired)s.pending=false;
                if(s!=displayed&&!s.pending&&(free==null||s.time<free.time))free=s;
            }
            if(free==null||!source.TryAcquireSourceCopyLease(in frame,out var lease))return;
            try {
                var renderSource=frame.Texture as RenderTexture;
                if(free.texture==null){free.texture=renderSource!=null?new RenderTexture(renderSource.descriptor):new RenderTexture(frame.Width,frame.Height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Default);free.texture.Create();}
                commands.Clear();
                // A same-format GPU copy preserves pixels exactly. A textured
                // blit can re-filter neighbouring texels even at equal sizes.
                if(renderSource!=null&&(SystemInfo.copyTextureSupport&CopyTextureSupport.Basic)!=0)commands.CopyTexture(renderSource,free.texture);
                else commands.Blit(frame.Texture,free.texture);
                // We poll this fence on the CPU. AsyncQueueSynchronisation.passed
                // throws on mobile GPUs without async compute, even when graphics fences are supported.
                free.fence=commands.CreateGraphicsFence(GraphicsFenceType.CPUSynchronisation,SynchronisationStageFlags.PixelProcessing);
                // Bind the lease before executing commands, so even an execution error cannot free a queued source early.
                free.lease=lease;free.pending=true;lease.RetireAfter(free);
                Graphics.ExecuteCommandBuffer(commands);
                free.frame=frame.FrameId;free.time=timestamp;lastCapture=frame.FrameId;
            } finally { if(!free.pending)lease.Dispose(); }
        }
        internal void Present(long targetTime) {
            Slot best=null;
            foreach(var s in slots)if(s.frame>=0&&s.time<=targetTime&&s.time>=lastPresented&&s.IsComplete&&(best==null||s.time>best.time))best=s;
            if(best!=null){displayed=best;lastPresented=best.time;}
        }
        public void Dispose(){
            foreach(var s in slots)if(s.texture!=null){
                if(s.pending)DeferredGpuTextureRelease.Retire(s.texture,s.fence);
                else {s.texture.Release();if(Application.isPlaying)UnityEngine.Object.Destroy(s.texture);else UnityEngine.Object.DestroyImmediate(s.texture);}
                s.texture=null;
            }
            commands.Dispose();displayed=null;
        }
    }
}
