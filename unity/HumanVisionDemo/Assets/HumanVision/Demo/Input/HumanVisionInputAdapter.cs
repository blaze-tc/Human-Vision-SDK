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
    private long nextSplitFeedLogUs;
    internal readonly struct AnalysisCopyTrace {
        private readonly RtspFrameSource source;
        internal bool IsSplitAnalysis => source != null;
        internal readonly ulong SourceId, Generation;
        private readonly long frameId;
        internal AnalysisCopyTrace(RtspFrameSource source,in HumanVisionTextureFrame frame) {
            this.source=source;
            SourceId=frame.SourceId;Generation=frame.Generation;frameId=frame.FrameId;
        }
        internal void RecordSnapshot(RtspAnalysisSnapshotStatus status) {
            if(source!=null)source.RecordAnalysisSnapshotCompletion(SourceId,Generation,frameId,InputMonotonicClock.NowUs,status);
        }
        internal void RecordReadback(bool succeeded) {
            if(source!=null)source.RecordAnalysisReadbackCompletion(SourceId,Generation,frameId,InputMonotonicClock.NowUs,succeeded);
        }
    }
    internal sealed class CpuFence:ISourceCopyFence {
        private GraphicsFence copyFence;
        private bool hasFence, fenceQueryFailed, unavailableReported, readbackSucceeded, complete;
        internal ulong Attempt {get;private set;}
        internal AnalysisCopyTrace Trace {get;private set;}
        internal bool HasQueuedCopy {get;private set;}
        internal bool HasRequest {get;private set;}
        internal bool ReadbackError {get;private set;}
        internal void Begin(in AnalysisCopyTrace trace) {
            ++Attempt;Trace=trace;
            HasQueuedCopy=HasRequest=ReadbackError=false;
            hasFence=fenceQueryFailed=unavailableReported=readbackSucceeded=complete=false;
        }
        internal void Arm(GraphicsFence fence,bool canPoll) {
            copyFence=fence;hasFence=canPoll;HasQueuedCopy=true;
        }
        internal void MarkRequestQueued() => HasRequest=true;
        internal void ObserveReadback(ulong attempt,bool succeeded) {
            // The bridge keeps its owned texture after the source is retired. A
            // callback for a recycled adapter slot cannot complete its next copy.
            if(attempt!=Attempt || !HasQueuedCopy || complete)return;
            readbackSucceeded=succeeded;ReadbackError=!succeeded;
        }
        internal void Reset() {Trace=default;HasQueuedCopy=false;}
        public bool IsComplete {
            get {
                if(complete)return true;
                // The source is no longer needed after the actual GPU copy. The
                // bridge separately retains the owned snapshot until readback completes.
                if(hasFence&&!fenceQueryFailed) {
                    try {
                        if(copyFence.passed) {
                            complete=true;
                            Trace.RecordSnapshot(RtspAnalysisSnapshotStatus.FencePassed);
                            return true;
                        }
                    }
                    catch(NotSupportedException) {fenceQueryFailed=true;}
                    catch(InvalidOperationException) {fenceQueryFailed=true;}
                }
                // A successful read proves the earlier GPU copy has finished. An
                // errored read, or a missing callback, cannot establish that proof.
                if(readbackSucceeded) {
                    complete=true;
                    Trace.RecordSnapshot(!hasFence?RtspAnalysisSnapshotStatus.ReadbackCompletedFenceUnavailable:
                        fenceQueryFailed?RtspAnalysisSnapshotStatus.ReadbackCompletedFenceQueryFailed:
                        RtspAnalysisSnapshotStatus.ReadbackCompletedBeforeFenceObserved);
                    return true;
                }
                if(!unavailableReported&&(!hasFence||fenceQueryFailed)) {
                    unavailableReported=true;
                    Trace.RecordSnapshot(hasFence?RtspAnalysisSnapshotStatus.FenceQueryFailed:RtspAnalysisSnapshotStatus.FenceUnavailable);
                }
                return false;
            }
        }
    }
    private sealed class Slot:ISourceCopyFence {
        internal readonly HumanVisionAndroidFrameCopyFence Gpu=new HumanVisionAndroidFrameCopyFence();
        internal readonly CpuFence Cpu=new CpuFence();
        internal RenderTexture Target;
        internal SourceCopyLease Lease;
        internal AnalysisCopyTrace Trace;
        internal bool Active,GpuPath,GpuSnapshotObserved,GpuQueryFailureReported;
        public bool IsComplete {
            get {
                if(!GpuPath)return Cpu.IsComplete;
                if(GpuSnapshotObserved)return true;
                try {if(!Gpu.IsComplete)return false;}
                catch {
                    if(!GpuQueryFailureReported) {
                        GpuQueryFailureReported=true;
                        Trace.RecordSnapshot(RtspAnalysisSnapshotStatus.FenceQueryFailed);
                    }
                    throw;
                }
                GpuSnapshotObserved=true;
                Trace.RecordSnapshot(Gpu.Outcome==1?RtspAnalysisSnapshotStatus.NativeFencePassed:RtspAnalysisSnapshotStatus.NativeCopyFailed);
                return true;
            }
        }
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
        if(manager.UsesIndependentInput){LastError="The native runtime already owns input; Unity texture ingestion is disabled.";return;}
        if(closing)return;
        if(sourceId!=0&&(sourceId!=frame.SourceId||generation!=frame.Generation)) {
            // Retire CPU result identity immediately on reconnect/mirror changes.
            if(cpuBridge!=null)cpuBridge.InvalidateCpuInputGeneration();
            SubmittedGpuAnalysisSize=Vector2Int.zero;
            ResetSplitMatching();
            presentationBridge?.InvalidateRuntimeViewHistory();
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
            else if(Application.platform==RuntimePlatform.Android&&source is RtspFrameSource&&!HumanVisionAndroidAccelerationSelection.IsNeuralProfile(manager.ActiveRuntimeProfile)&&
                !HumanVisionAndroidAccelerationSelection.IsCpuProfile(manager.ActiveRuntimeProfile))
                throw new InvalidOperationException("Android hardware RTSP requires the explicitly selected GPU runtime. The existing CPU modes do not accept this source; preview continues.");
            Slot free=null;for(int i=0;i<slots.Length;i++)if(!slots[i].Active){free=slots[i];break;}
            if(free==null)return;
            SourceCopyLease lease;
            bool acquired=splitSource!=null ? splitSource.TryAcquireAnalysisCopyLease(in frame,out lease) :
                source.TryAcquireSourceCopyLease(in frame,out lease);
            if(!acquired)return;
            free.Lease=lease;
            free.Trace=new AnalysisCopyTrace(splitSource,in frame);
            free.GpuPath=manager.UsesAndroidGpuFrames;
            free.GpuSnapshotObserved=free.GpuQueryFailureReported=false;
            if(!free.GpuPath)free.Cpu.Begin(in free.Trace);
            bool accepted=false;
            try {
                long unityNowUs=(long)(Time.realtimeSinceStartupAsDouble*1000000);
                long inputNowUs=InputMonotonicClock.NowUs;
                long timestamp=manager.UsesRuntimeProfile&&!manager.UsesAndroidGpuFrames
                    ? InputTimestampMapping.ToUnity(in frame,inputNowUs,unityNowUs)
                    : unityNowUs-Math.Max(0,inputNowUs-frame.PublishedTimestampUs);
                if(free.GpuPath) {
                    if(free.Target==null) {
                        var size=analysisLimit==Vector2Int.zero ? new Vector2Int(frame.Width,frame.Height) :
                            AnalysisRenderTextureGeometry.CalculateTargetSize(frame.Width,frame.Height,analysisLimit.x,analysisLimit.y);
                        ValidateGpuGeometry(size.x,size.y);
                        free.Target=new RenderTexture(size.x,size.y,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
                        free.Target.Create();
                    }
                    gpuActive=true;
                    if(!ReferenceEquals(provenanceOwner,manager)){provenanceOwner=manager;provenanceSink=manager.SetCaptureProvenance;}
                    ForwardPublicationProvenance(in frame,provenanceSink);
                    accepted=manager.SubmitUnifiedGpuFrame(frame.Texture,free.Target,in frame,timestamp,free.Gpu);
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
                    accepted=cpuBridge.TrySubmitUnifiedCpuFrame(frame.Texture,timestamp,free.Cpu);
                }
            } finally {
                // Queueing can fail after the source is referenced by GPU work.
                if(free.GpuPath?free.Gpu.Active:free.Cpu.HasQueuedCopy) {
                    lease.RetireAfter(free);free.Active=true;
                } else {
                    lease.Dispose();free.Cpu.Reset();free.Trace=default;
                }
            }
            if(accepted){
                submitted=free.GpuPath?frame.FrameId:cpuBridge.LatestQueuedFrameId;
                if(firstSubmitted<0)firstSubmitted=submitted;
                latest=frame.FrameId;
                // CPU allocates its own submission IDs. Retain the original
                // publication under the actual ID native results will return.
                if(splitSource!=null) {
                    long acceptedUs=InputMonotonicClock.NowUs;
                    bool acknowledged=splitSource.AcknowledgeAnalysisFrame(in frame);
                    RememberAcceptedAnalysis(in frame,acceptedUs,acknowledged);
                    if(presentationBridge!=null && presentationBridge.CaptureAnalysisRuntimeViewFrames &&
                        presentationBridge.CaptureRuntimeViewFrame(in frame,submitted,analysisFrame:true))
                        RecordMatchedFrame(in frame,frame.PublishedTimestampUs,acceptedUs);
                    UpdateSplitPreview();
                } else presentationBridge?.CaptureRuntimeViewFrame(in frame,submitted);
            }
            LastError=string.Empty;
            // A temporary rejection must not retire this source frame. On the
            // next Unity tick TryGetLatestFrame supplies it or a newer one;
            // acceptance retires it exactly once without creating a frame queue.
        } catch(Exception error){LastError=error.Message;latest=frame.FrameId;}
    }
    internal void ResetSplitMatching() {
        acceptedAnalysisFrames=null;
        ResetSplitPreviewCapture();
        nextSplitFeedLogUs=0;
    }
    internal void ResetSplitPreviewCapture() => lastMatchedInputId=-1;
    private void RememberAcceptedAnalysis(in HumanVisionTextureFrame frame,long acceptedUs,bool acknowledged) {
        if(acceptedAnalysisFrames==null)acceptedAnalysisFrames=new AcceptedAnalysisFrame[32];
        acceptedAnalysisFrames[(int)(frame.FrameId%acceptedAnalysisFrames.Length)]=new AcceptedAnalysisFrame {
            Source=frame.SourceId,Generation=frame.Generation,InputId=frame.FrameId,ResultId=submitted,
            DecodeUs=frame.SourceTimestampUs,Clock=frame.SourceClockId,
            PublishedUs=frame.PublishedTimestampUs,AcceptedUs=acceptedUs
        };
        if(acceptedUs<nextSplitFeedLogUs)return;
        nextSplitFeedLogUs=acceptedUs+1000000;
        long previousPreview=-1;
        if(source.TryGetLatestFrame(-1,out var preview)&&preview.SourceId==frame.SourceId&&preview.Generation==frame.Generation)
            previousPreview=preview.FrameId;
        Debug.Log($"HumanVision RTSP split feed: source={frame.SourceId} generation={frame.Generation} frame={frame.FrameId} analysis_published_us={frame.PublishedTimestampUs} accepted_us={acceptedUs} previous_preview_frame={previousPreview} acknowledged={(acknowledged?1:0)}");
    }
    private void UpdateSplitPreview() {
        if(!source.TryGetLatestFrame(-1,out var frame)) {
            PreviewTexture=null;LatestPreviewFrameId=-1;
            if(source.State!=InputSourceState.Streaming&&acceptedAnalysisFrames!=null) {
                ResetSplitMatching();
                presentationBridge?.InvalidateRuntimeViewHistory();
            }
            return;
        }
        PreviewTexture=frame.Texture;LatestPreviewFrameId=frame.FrameId;
        if(closing||presentationBridge==null||presentationBridge.CaptureAnalysisRuntimeViewFrames||
           acceptedAnalysisFrames==null||frame.SourceId!=sourceId||
           frame.Generation!=generation||frame.FrameId==lastMatchedInputId)return;
        var accepted=acceptedAnalysisFrames[(int)(frame.FrameId%acceptedAnalysisFrames.Length)];
        if(accepted.Source!=frame.SourceId||accepted.Generation!=frame.Generation||accepted.InputId!=frame.FrameId||
           accepted.Clock!=frame.SourceClockId||accepted.DecodeUs!=frame.SourceTimestampUs)return;
        if(!presentationBridge.CaptureRuntimeViewFrame(in frame,accepted.ResultId))return;
        RecordMatchedFrame(in frame,accepted.PublishedUs,accepted.AcceptedUs);
    }
    private void RecordMatchedFrame(in HumanVisionTextureFrame frame,long analysisPublishedUs,long acceptedUs) {
        lastMatchedInputId=frame.FrameId;
        lastMatchedAnalysisPublishedUs=analysisPublishedUs;
        lastMatchedAnalysisAcceptedUs=acceptedUs;
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
    internal bool TryGetResultDecodeAgeMs(long resultFrameId, out long inputFrameId, out double ageMs) {
        inputFrameId = -1; ageMs = -1;
        if (resultFrameId < 0 || acceptedAnalysisFrames == null || sourceId == 0) return false;
        foreach (var accepted in acceptedAnalysisFrames) {
            if (accepted.Source != sourceId || accepted.Generation != generation || accepted.ResultId != resultFrameId) continue;
            if (!InputMonotonicClock.TryGetNativeFrameAgeUs(accepted.Clock, accepted.DecodeUs, out long ageUs)) return false;
            inputFrameId = accepted.InputId;
            ageMs = ageUs / 1000d;
            return true;
        }
        return false;
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
                if(!slot.IsComplete||!slot.Lease.IsRetired)continue;
                if(slot.GpuPath){
                    if(slot.Gpu.Outcome==1){
                        CopiedFrames++;
                        if(source!=null&&!closing&&slot.Trace.SourceId==sourceId&&slot.Trace.Generation==generation)
                            SubmittedGpuAnalysisSize=new Vector2Int(slot.Target.width,slot.Target.height);
                    }else DroppedUnsubmittedFrames++;
                    slot.Gpu.ReleaseAcknowledgedLease();
                }
                else {if(!slot.Cpu.HasRequest||slot.Cpu.ReadbackError)DroppedUnsubmittedFrames++;else CopiedFrames++;}
                slot.Cpu.Reset();slot.Trace=default;slot.Active=false;
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
