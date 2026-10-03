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
    private long latest=-1,firstSubmitted=-1,submitted=-1,minimumSequence;
    private bool gpuActive,closing,watched;
    private HumanVisionAndroidSourceRetirement retirement;
    private readonly Slot[] slots={new Slot(),new Slot(),new Slot()};
    private HumanVisionInputAdapter nextRetirement;
    private sealed class CpuFence:ISourceCopyFence {
        internal AsyncGPUReadbackRequest Request;
        public bool IsComplete=>Request.done;
    }
    private sealed class Slot {
        internal readonly HumanVisionAndroidFrameCopyFence Gpu=new HumanVisionAndroidFrameCopyFence();
        internal readonly CpuFence Cpu=new CpuFence();
        internal RenderTexture Target;
        internal SourceCopyLease Lease;
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
    public void Bind(IHumanVisionFrameSource newSource) {
        if(newSource==null)throw new ArgumentNullException(nameof(newSource));
        Detach();source=newSource;latest=-1;firstSubmitted=submitted=-1;sourceId=generation=0;
        minimumSequence=manager!=null?manager.ResultSequence:0;
    }
    public void Detach() {
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
        if(!source.TryGetLatestFrame(latest,out var frame))return;
        PreviewTexture=frame.Texture;LatestPreviewFrameId=frame.FrameId;
        if(manager==null)manager=GetComponent<HumanVisionManager>();
        if(manager==null||!manager.IsInitialized){latest=frame.FrameId;LastError="SDK is not initialized; independent preview continues.";return;}
        if(closing)return;
        if(sourceId!=0&&(sourceId!=frame.SourceId||generation!=frame.Generation)) {
            closing=gpuActive||AnyActive();minimumSequence=manager.ResultSequence;firstSubmitted=submitted=-1;
            sourceId=generation=0;
            if(closing){RetirementPump.Watch(this);return;}
        }
        sourceId=frame.SourceId;generation=frame.Generation;
        try {
            if(manager.UsesAndroidGpuFrames)ValidateGpuGeometry(frame.Width,frame.Height);
            else if(Application.platform==RuntimePlatform.Android&&source is RtspFrameSource)
                throw new InvalidOperationException("Android hardware RTSP requires the explicitly selected GPU runtime. The existing CPU modes do not accept this source; preview continues.");
            Slot free=null;for(int i=0;i<slots.Length;i++)if(!slots[i].Active){free=slots[i];break;}
            if(free==null){latest=frame.FrameId;return;}
            if(!source.TryAcquireSourceCopyLease(in frame,out var lease)){latest=frame.FrameId;return;}
            free.Lease=lease;
            bool accepted=false;
            try {
                long unityNowUs=(long)(Time.realtimeSinceStartupAsDouble*1000000);
                long inputNowUs=InputMonotonicClock.NowUs;
                long timestamp=manager.UsesRuntimeProfile&&!manager.UsesAndroidGpuFrames
                    ? InputTimestampMapping.ToUnity(in frame,inputNowUs,unityNowUs)
                    : unityNowUs-Math.Max(0,inputNowUs-frame.PublishedTimestampUs);
                if(manager.UsesAndroidGpuFrames) {
                    if(free.Target==null){free.Target=new RenderTexture(frame.Width,frame.Height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);free.Target.Create();}
                    gpuActive=true;free.GpuPath=true;
                    if(!ReferenceEquals(provenanceOwner,manager)){provenanceOwner=manager;provenanceSink=manager.SetCaptureProvenance;}
                    ForwardPublicationProvenance(in frame,provenanceSink);
                    manager.RecordSourceArrival(false);
                    accepted=manager.SubmitUnifiedGpuFrame(frame.Texture,free.Target,in frame,timestamp,free.Gpu);
                    if(free.Gpu.Active){lease.RetireAfter(free.Gpu);free.Active=true;}
                } else {
                    if(cpuBridge==null){cpuBridge=GetComponent<VideoPlayerFrameSource>();if(cpuBridge==null){cpuBridge=gameObject.AddComponent<VideoPlayerFrameSource>();cpuBridge.Configure(manager,null,null);}else cpuBridge.BindManager(manager);cpuBridge.ConfigureLiveInput(true);}
                    free.GpuPath=false;
                    accepted=cpuBridge.TrySubmitUnifiedCpuFrame(frame.Texture,timestamp,out var request);
                    if(accepted){free.Cpu.Request=request;lease.RetireAfter(free.Cpu);free.Active=true;}
                }
                if(!free.Active)lease.Dispose();
                if(accepted){submitted=manager.UsesAndroidGpuFrames?frame.FrameId:cpuBridge.LatestQueuedFrameId;if(firstSubmitted<0)firstSubmitted=submitted;}
                LastError=string.Empty;
            } catch {
                // The bridge binds its native token before Execute. Unknown queued
                // completion retains the exact input lease and both strong textures.
                if(free.Gpu.Active&&!free.Active){lease.RetireAfter(free.Gpu);free.Active=true;}
                else if(!free.Active)lease.Dispose();
                throw;
            }
            latest=frame.FrameId;
        } catch(Exception error){LastError=error.Message;latest=frame.FrameId;}
    }
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
                if(slot.GpuPath){if(slot.Gpu.Outcome==1)CopiedFrames++;else DroppedUnsubmittedFrames++;slot.Gpu.ReleaseAcknowledgedLease();}
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
