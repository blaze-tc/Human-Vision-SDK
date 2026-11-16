using System;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Rendering;
namespace HumanVision.Demo {
// Optional SDK consumer. The input source and independent FramePreview own playback.
[DefaultExecutionOrder(100)]
public sealed class HumanVisionInputAdapter:MonoBehaviour {
    [SerializeField] public HumanVisionManager manager;
    private IHumanVisionFrameSource source;
    private VideoPlayerFrameSource cpuBridge;
    private HumanVisionManager provenanceOwner;
    private Action<uint> provenanceSink;
    private ulong sourceId,generation;
    private ulong observedSource,observedGeneration;
    private long observedFrame=-1;
    private long latest=-1,firstSubmitted=-1,submitted=-1,minimumSequence;
    private bool gpuActive,closing,watched;
    private Vector2Int analysisLimit;
    internal Vector2Int AnalysisLimit => analysisLimit;
    private bool cpuBridgeConfigured;
    internal Vector2Int SubmittedGpuAnalysisSize { get; private set; }
    private HumanVisionAndroidSourceRetirement retirement;
    private readonly Slot[] slots={new Slot(),new Slot(),new Slot()};
    private HumanVisionInputAdapter nextRetirement;
    private struct AcceptedAnalysisFrame {
        internal ulong Source, Generation, Clock;
        internal long InputId, ResultId, DecodeUs, PublishedUs, AcceptedUs;
    }
    private AcceptedAnalysisFrame[] acceptedAnalysisFrames;
    private long lastMatchedInputId=-1, lastMatchedAnalysisPublishedUs, lastMatchedAnalysisAcceptedUs, lastMatchedPreviewPublishedUs;
    private sealed class CpuFence:ISourceCopyFence {
        internal AsyncGPUReadbackRequest Request;
        internal GraphicsFence CopyFence;
        internal bool HasCopyFence, HasRequest;
        public bool IsComplete {
            get {
                if(HasRequest && Request.done)return true;
                // The source is no longer needed after the actual GPU copy. The
                // bridge separately retains the owned snapshot until readback completes.
                if(!HasCopyFence)return false;
                try {return CopyFence.passed;}
                catch(NotSupportedException){return false;}
                catch(InvalidOperationException){return false;}
            }
        }
    }
    private sealed class Slot {
        internal readonly HumanVisionAndroidFrameCopyFence Gpu=new HumanVisionAndroidFrameCopyFence();
        internal readonly CpuFence Cpu=new CpuFence();
        internal RenderTexture Target;
        internal SourceCopyLease Lease;
        internal ulong SourceId, Generation;
        internal bool Active,GpuPath;
    }
    public Texture PreviewTexture {get;private set;}
    public long LatestPreviewFrameId {get;private set;}=-1;
    public long LatestSubmittedFrameId=>submitted;
    public ulong SourceId=>sourceId;
    public ulong SourceGeneration=>generation;
    public string LastError {get;private set;}=string.Empty;
    public long CopiedFrames {get;private set;}
    public long DroppedUnsubmittedFrames {get;private set;}
    public bool RetirementPending=>closing;
    public int PendingSourceCopies {get {int count=0;for(int i=0;i<slots.Length;i++)if(slots[i].Active)count++;return count;}}
    public void Bind(IHumanVisionFrameSource newSource) => Bind(newSource, Vector2Int.zero);
    internal void Bind(IHumanVisionFrameSource newSource, Vector2Int limit) {
        if(newSource==null)throw new ArgumentNullException(nameof(newSource));
        Detach();source=newSource;latest=-1;firstSubmitted=submitted=-1;sourceId=generation=0;
        if(analysisLimit!=limit)cpuBridgeConfigured=false;
        analysisLimit=limit;
        observedSource=observedGeneration=0;observedFrame=-1;
        minimumSequence=manager!=null?manager.ResultSequence:0;
    }
    public void Detach() {
        // Readback callbacks may outlive a binding. Retire their frame identities
        // before accepting a different source, even if its local frame IDs restart.
        if(cpuBridge!=null)cpuBridge.InvalidateCpuInputGeneration();
        SubmittedGpuAnalysisSize=Vector2Int.zero;
        ResetSplitMatching();
        source=null;PreviewTexture=null;LatestPreviewFrameId=-1;firstSubmitted=submitted=-1;
        closing=gpuActive||AnyActive();
        if(closing)RetirementPump.Watch(this);
    }
    public bool CanPresentResult(long frameId)=>source!=null&&source.State==InputSourceState.Streaming&&
        firstSubmitted>=0&&frameId>=firstSubmitted&&frameId<=submitted&&manager!=null&&manager.ResultSequence>minimumSequence;
    private void Update(){Tick();}
    public void Tick() {
        PollRetirement();
        if(source==null)return;
        var splitSource=source is RtspFrameSource rtsp && rtsp.HasSeparateAnalysis ? rtsp : null;
        HumanVisionTextureFrame frame;
        if(splitSource!=null) {
            UpdateSplitPreview();
            if(!splitSource.TryGetLatestAnalysisFrame(latest,out frame))return;
        } else {
            if(!source.TryGetLatestFrame(latest,out frame))return;
            PreviewTexture=frame.Texture;LatestPreviewFrameId=frame.FrameId;
        }
        if(manager==null)manager=GetComponent<HumanVisionManager>();
        if(manager==null||!manager.IsInitialized){latest=frame.FrameId;LastError="SDK is not initialized; independent preview continues.";return;}
        if(closing)return;
        if(sourceId!=0&&(sourceId!=frame.SourceId||generation!=frame.Generation)) {
            // Retire CPU result identity immediately on reconnect/mirror changes.
            if(cpuBridge!=null)cpuBridge.InvalidateCpuInputGeneration();
            SubmittedGpuAnalysisSize=Vector2Int.zero;
            ResetSplitMatching();
            closing=gpuActive||AnyActive();minimumSequence=manager.ResultSequence;firstSubmitted=submitted=-1;
            sourceId=generation=0;
            if(closing){RetirementPump.Watch(this);return;}
        }
        sourceId=frame.SourceId;generation=frame.Generation;
        // Rendering can retry the same fresh source while a readback slot or
        // admission credit is unavailable. Count the source publication once.
        bool newlyObserved=observedSource!=frame.SourceId||observedGeneration!=frame.Generation||observedFrame!=frame.FrameId;
        if(newlyObserved){manager.RecordSourceArrival(false);observedSource=frame.SourceId;observedGeneration=frame.Generation;observedFrame=frame.FrameId;}
        try {
            if(manager.UsesAndroidGpuFrames)ValidateGpuGeometry(frame.Width,frame.Height);
            else if(Application.platform==RuntimePlatform.Android&&source is RtspFrameSource&&!HumanVisionAndroidAccelerationSelection.IsNeuralProfile(manager.ActiveRuntimeProfile)&&manager.ActiveRuntimeProfile!=HumanVisionAndroidAccelerationSelection.CpuProfile)
                throw new InvalidOperationException("Android hardware RTSP requires the explicitly selected GPU runtime. The existing CPU modes do not accept this source; preview continues.");
            Slot free=null;for(int i=0;i<slots.Length;i++)if(!slots[i].Active){free=slots[i];break;}
            if(free==null)return;
            SourceCopyLease lease;
            bool acquired=splitSource!=null ? splitSource.TryAcquireAnalysisCopyLease(in frame,out lease) :
                source.TryAcquireSourceCopyLease(in frame,out lease);
            if(!acquired)return;
            free.Lease=lease;
            bool accepted=false;
            try {
                long unityNowUs=(long)(Time.realtimeSinceStartupAsDouble*1000000);
                long inputNowUs=InputMonotonicClock.NowUs;
                long timestamp=manager.UsesRuntimeProfile&&!manager.UsesAndroidGpuFrames
                    ? InputTimestampMapping.ToUnity(in frame,inputNowUs,unityNowUs)
                    : unityNowUs-Math.Max(0,inputNowUs-frame.PublishedTimestampUs);
                if(manager.UsesAndroidGpuFrames) {
                    if(free.Target==null) {
                        var size=analysisLimit==Vector2Int.zero ? new Vector2Int(frame.Width,frame.Height) :
                            AnalysisRenderTextureGeometry.CalculateTargetSize(frame.Width,frame.Height,analysisLimit.x,analysisLimit.y);
                        ValidateGpuGeometry(size.x,size.y);
                        free.Target=new RenderTexture(size.x,size.y,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
                        free.Target.Create();
                    }
                    gpuActive=true;free.GpuPath=true;
                    free.SourceId=frame.SourceId;free.Generation=frame.Generation;
                    if(!ReferenceEquals(provenanceOwner,manager)){provenanceOwner=manager;provenanceSink=manager.SetCaptureProvenance;}
                    ForwardPublicationProvenance(in frame,provenanceSink);
                    accepted=manager.SubmitUnifiedGpuFrame(frame.Texture,free.Target,in frame,timestamp,free.Gpu);
                    if(free.Gpu.Active){lease.RetireAfter(free.Gpu);free.Active=true;}
                } else {
                    if(!ReferenceEquals(provenanceOwner,manager)){provenanceOwner=manager;provenanceSink=manager.SetCaptureProvenance;}
                    ForwardPublicationProvenance(in frame,provenanceSink);
                    if(cpuBridge==null) {
                        cpuBridge=GetComponent<VideoPlayerFrameSource>();
                        if(cpuBridge==null){cpuBridge=gameObject.AddComponent<VideoPlayerFrameSource>();cpuBridge.Configure(manager,null,null);}
                        else cpuBridge.BindManager(manager);
                        cpuBridgeConfigured=false;
                    }
                    if(!cpuBridgeConfigured) {
                        if(analysisLimit==Vector2Int.zero)cpuBridge.ConfigureLiveInput(true);
                        else cpuBridge.ConfigureLiveInput(true,analysisLimit.x,analysisLimit.y);
                        cpuBridgeConfigured=true;
                    }
                    free.GpuPath=false;
                    accepted=cpuBridge.TrySubmitUnifiedCpuFrame(frame.Texture,timestamp,out var request,out var copyFence,out var hasCopyFence);
                    if(accepted){free.Cpu.Request=request;free.Cpu.HasRequest=true;free.Cpu.CopyFence=copyFence;free.Cpu.HasCopyFence=hasCopyFence;lease.RetireAfter(free.Cpu);free.Active=true;}
                }
                if(!free.Active)lease.Dispose();
                if(accepted){
                    submitted=manager.UsesAndroidGpuFrames?frame.FrameId:cpuBridge.LatestQueuedFrameId;
                    if(firstSubmitted<0)firstSubmitted=submitted;
                    latest=frame.FrameId;
                    // CPU allocates its own submission IDs. Retain the original
                    // publication under the actual ID native results will return.
                    if(splitSource!=null) {
                        long acceptedUs=InputMonotonicClock.NowUs;
                        if(splitSource.AcknowledgeAnalysisFrame(in frame)) RememberAcceptedAnalysis(in frame,acceptedUs);
                        UpdateSplitPreview();
                    } else presentationBridge?.CaptureRuntimeViewFrame(in frame,submitted);
                }
                LastError=string.Empty;
            } catch {
                // The bridge binds its native token before Execute. Unknown queued
                // completion retains the exact input lease and both strong textures.
                if(free.Gpu.Active&&!free.Active){lease.RetireAfter(free.Gpu);free.Active=true;}
                else if(!free.GpuPath && !free.Active && cpuBridge!=null && cpuBridge.TryGetQueuedSourceCopy(out var pendingCopy)) {
                    free.Cpu.HasRequest=false;free.Cpu.HasCopyFence=true;free.Cpu.CopyFence=pendingCopy;
                    lease.RetireAfter(free.Cpu);free.Active=true;
                }
                else if(!free.Active)lease.Dispose();
                throw;
            }
            // A temporary rejection must not retire this source frame. On the
            // next Unity tick TryGetLatestFrame supplies it or a newer one;
            // acceptance retires it exactly once without creating a frame queue.
        } catch(Exception error){LastError=error.Message;latest=frame.FrameId;}
    }
    private void ResetSplitMatching() {
        if(acceptedAnalysisFrames!=null)Array.Clear(acceptedAnalysisFrames,0,acceptedAnalysisFrames.Length);
        lastMatchedInputId=-1;
    }
    private void RememberAcceptedAnalysis(in HumanVisionTextureFrame frame,long acceptedUs) {
        if(acceptedAnalysisFrames==null)acceptedAnalysisFrames=new AcceptedAnalysisFrame[32];
        acceptedAnalysisFrames[(int)(frame.FrameId%acceptedAnalysisFrames.Length)]=new AcceptedAnalysisFrame {
            Source=frame.SourceId,Generation=frame.Generation,InputId=frame.FrameId,ResultId=submitted,
            DecodeUs=frame.SourceTimestampUs,Clock=frame.SourceClockId,
            PublishedUs=frame.PublishedTimestampUs,AcceptedUs=acceptedUs
        };
    }
    private void UpdateSplitPreview() {
        if(!source.TryGetLatestFrame(-1,out var frame)) {
            PreviewTexture=null;LatestPreviewFrameId=-1;
            return;
        }
        PreviewTexture=frame.Texture;LatestPreviewFrameId=frame.FrameId;
        if(closing||acceptedAnalysisFrames==null||frame.SourceId!=sourceId||
           frame.Generation!=generation||frame.FrameId==lastMatchedInputId)return;
        var accepted=acceptedAnalysisFrames[(int)(frame.FrameId%acceptedAnalysisFrames.Length)];
        if(accepted.Source!=frame.SourceId||accepted.Generation!=frame.Generation||accepted.InputId!=frame.FrameId||
           accepted.Clock!=frame.SourceClockId||accepted.DecodeUs!=frame.SourceTimestampUs)return;
        presentationBridge?.CaptureRuntimeViewFrame(in frame,accepted.ResultId);
        lastMatchedInputId=frame.FrameId;
        lastMatchedAnalysisPublishedUs=accepted.PublishedUs;
        lastMatchedAnalysisAcceptedUs=accepted.AcceptedUs;
        lastMatchedPreviewPublishedUs=frame.PublishedTimestampUs;
    }
    internal bool TryGetMatchedAnalysisInputTiming(out long inputFrameId,out long analysisPublishedUs,
        out long analysisAcceptedUs,out long previewPublishedUs) {
        inputFrameId=analysisPublishedUs=analysisAcceptedUs=previewPublishedUs=-1;
        if(lastMatchedInputId<0)return false;
        inputFrameId=lastMatchedInputId;
        analysisPublishedUs=lastMatchedAnalysisPublishedUs;
        analysisAcceptedUs=lastMatchedAnalysisAcceptedUs;
        previewPublishedUs=lastMatchedPreviewPublishedUs;
        return true;
    }
    internal VideoPlayerFrameSource presentationBridge;
    internal static void ForwardPublicationProvenance(in HumanVisionTextureFrame frame,Action<uint> forward) {
        if(frame.PublishedTimestampUs<0 || frame.PublishedClockDomain!=FrameClockDomain.InputMonotonic)
            throw new ArgumentException("Source publication must use the declared InputMonotonic clock.");
        forward((uint)AndroidCaptureProvenanceNative.UnityObserved);
    }
    internal static void ValidateGpuGeometry(int width,int height) {
        if(width<=0||height<=0||width<=height||(long)width*9!=(long)height*16)
            throw new InvalidOperationException("GPU inference requires the validated upright 16:9 landscape contract; preview continues for other geometry.");
    }
    internal static void NormalizeNativeRows(Texture preview,RenderTexture target,FrameRowOrigin origin)
        =>HumanVisionAndroidGpuFrameBridge.NormalizeNativeRows(preview,target,origin);
    private bool AnyActive(){for(int i=0;i<slots.Length;i++)if(slots[i].Active)return true;return false;}
    private void PollRetirement() {
        try {
            for(int i=0;i<slots.Length;i++){
                var slot=slots[i];if(!slot.Active)continue;
                bool complete=slot.GpuPath?slot.Gpu.IsComplete:slot.Cpu.IsComplete;
                if(!complete||!slot.Lease.IsRetired)continue;
                if(slot.GpuPath){
                    if(slot.Gpu.Outcome==1){
                        CopiedFrames++;
                        if(source!=null&&!closing&&slot.SourceId==sourceId&&slot.Generation==generation)
                            SubmittedGpuAnalysisSize=new Vector2Int(slot.Target.width,slot.Target.height);
                    }else DroppedUnsubmittedFrames++;
                    slot.Gpu.ReleaseAcknowledgedLease();
                }
                else {if(!slot.Cpu.HasRequest || slot.Cpu.Request.done&&slot.Cpu.Request.hasError)DroppedUnsubmittedFrames++;else CopiedFrames++;}
                slot.Active=false;
            }
            if(!closing||AnyActive())return;
            if(gpuActive){
                if(retirement==null&&manager!=null&&!manager.TryRetireAndroidGpuSourceCopies(out retirement))return;
                if(retirement!=null&&!retirement.IsComplete)return;
            }
            for(int i=0;i<slots.Length;i++)if(slots[i].Target!=null){slots[i].Target.Release();Destroy(slots[i].Target);slots[i].Target=null;}
            retirement=null;gpuActive=false;closing=false;
        } catch(Exception error){LastError=error.Message;} // Completion errors retain owners.
    }
    private void OnDisable(){Detach();}
    private void OnDestroy(){Detach();}
    private sealed class RetirementPump:MonoBehaviour {
        private static RetirementPump instance;
        private HumanVisionInputAdapter head;
        internal static void Watch(HumanVisionInputAdapter adapter){
            if(adapter.watched)return;
            if(instance==null){var go=new GameObject("HumanVision adapter copy retirement"){hideFlags=HideFlags.HideAndDontSave};DontDestroyOnLoad(go);instance=go.AddComponent<RetirementPump>();}
            adapter.nextRetirement=instance.head;adapter.watched=true;instance.head=adapter;
        }
        private void Update(){
            HumanVisionInputAdapter previous=null,current=head;
            while(!ReferenceEquals(current,null)){
                var next=current.nextRetirement;current.PollRetirement();
                if(!current.closing){if(ReferenceEquals(previous,null))head=next;else previous.nextRetirement=next;current.nextRetirement=null;current.watched=false;}else previous=current;
                current=next;
            }
        }
    }
}
}
