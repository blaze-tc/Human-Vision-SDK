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
            internal bool pending, markerPassed, markerPending;
            internal SourceCopyLease lease;
            internal long frame=-1, time, inputFrame;
            internal readonly Action<AsyncGPUReadbackRequest> MarkerCompleted;
            internal Slot() { MarkerCompleted = OnMarkerCompleted; }
            private void OnMarkerCompleted(AsyncGPUReadbackRequest request) {
                markerPassed = !request.hasError;
                markerPending = false;
            }
            public bool IsComplete => !pending || markerPassed || fence.passed;
        }
        private readonly Slot[] slots=new Slot[8];
        private readonly CommandBuffer commands=new CommandBuffer { name="HumanVision presentation snapshot" };
        private long lastCapture=-1, lastPresented=-1;
        private Slot displayed;
        internal long FrameId=>displayed?.frame??-1;
        internal long TimestampUs=>displayed?.time??-1;
        internal Texture Texture=>displayed?.texture;
        private readonly bool useReadbackMarker;
        internal PresentationTextureHistory() {
            // Unity 2021 D3D11 can leave a CPU fence pending after CopyTexture.
            // A successful 1x1 asynchronous readback of that destination also
            // proves the queued copy is complete, without blocking the CPU.
            useReadbackMarker = SystemInfo.graphicsDeviceType == GraphicsDeviceType.Direct3D11 &&
                SystemInfo.supportsAsyncGPUReadback;
            for(int i=0;i<slots.Length;i++)slots[i]=new Slot();
        }
        internal void Capture(IHumanVisionFrameSource source,in HumanVisionTextureFrame frame,long timestamp)
            => Capture(source, in frame, frame.FrameId, timestamp);
        internal void Capture(IHumanVisionFrameSource source,in HumanVisionTextureFrame frame,long resultFrameId,long timestamp) {
            if(resultFrameId<=lastCapture || !SystemInfo.supportsGraphicsFence)return;
            Slot free=null;
            foreach(var s in slots) {
                if(s.pending && s.IsComplete && s.lease.IsRetired)s.pending=false;
                if(s!=displayed&&!s.pending&&!s.markerPending&&(free==null||s.time<free.time))free=s;
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
                free.markerPassed=false;
                if(useReadbackMarker) {
                    commands.RequestAsyncReadback(free.texture,0,0,1,0,1,0,1,free.MarkerCompleted);
                    free.markerPending=true;
                }
                // Bind the lease before executing commands, so even an execution error cannot free a queued source early.
                free.lease=lease;free.pending=true;lease.RetireAfter(free);
                Graphics.ExecuteCommandBuffer(commands);
                free.frame=resultFrameId;free.inputFrame=frame.FrameId;free.time=timestamp;lastCapture=resultFrameId;
            } finally { if(!free.pending)lease.Dispose(); }
        }
        internal bool TryPresentExact(long resultFrameId, out Texture texture, out long inputFrameId) {
            texture=null;inputFrameId=-1;
            if(resultFrameId<0)return false;
            foreach(var slot in slots) {
                if(slot.frame!=resultFrameId || !slot.IsComplete || slot.texture==null || !slot.texture.IsCreated())continue;
                displayed=slot;texture=slot.texture;inputFrameId=slot.inputFrame;return true;
            }
            return false;
        }
        internal void Present(long targetTime) {
            Slot best=null;
            foreach(var s in slots)if(s.frame>=0&&s.time<=targetTime&&s.time>=lastPresented&&s.IsComplete&&(best==null||s.time>best.time))best=s;
            if(best!=null){displayed=best;lastPresented=best.time;}
        }
        public void Dispose(){
            foreach(var s in slots)if(s.texture!=null){
                if(s.pending)DeferredGpuTextureRelease.Retire(s.texture,s);
                else {s.texture.Release();if(Application.isPlaying)UnityEngine.Object.Destroy(s.texture);else UnityEngine.Object.DestroyImmediate(s.texture);}
                s.texture=null;
            }
            commands.Dispose();displayed=null;
        }
    }
}
