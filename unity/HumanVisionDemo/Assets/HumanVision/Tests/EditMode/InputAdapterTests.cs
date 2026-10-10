using System;
using System.Reflection;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
namespace HumanVision.Tests {
public sealed class InputAdapterTests {
    private sealed class Source : IHumanVisionFrameSource {
        public readonly Texture2D Texture;
        public readonly SourceRetirement Retirement = new SourceRetirement();
        private readonly ulong token;
        private readonly SourceGeneration owner;
        public Source(int width=16,int height=9) { Texture=new Texture2D(width,height,TextureFormat.RGBA32,false,true);owner=new SourceGeneration(Retirement);owner.BeginGeneration();token=Retirement.Register(owner.Generation,Texture,()=>{}); }
        public long FrameId = 1; public int CloseCount;
        public InputSourceState State => InputSourceState.Streaming;
        public string LastError => string.Empty;
        public Texture CurrentTexture => Texture;
        public void Open(HumanVisionSourceSettings settings) { }
        public void Close() { ++CloseCount; }
        public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) => owner.TryAcquireSourceCopyLease(in frame,out lease);
        public bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) {
            frame = new HumanVisionTextureFrame(owner.SourceId,owner.Generation,FrameId,Texture,FrameId, -1,180,true,
                FrameRowOrigin.UnityBottomLeft,FrameColorSpace.Linear,FrameTimestampKind.UnityObserved,token);
            owner.TryPublish(in frame);
            return FrameId>after;
        }
    }
    private static Type AdapterType() {
        var type=typeof(VideoPlayerFrameSource).Assembly.GetType("HumanVision.Demo.HumanVisionInputAdapter");
        Assert.That(type,Is.Not.Null,"Unified recognition consumer is missing."); return type;
    }
    [Test] public void PublicationForwardsUnityObservedProvenanceAndPreservesDecodeMetadata() {
        var method=AdapterType().GetMethod("ForwardPublicationProvenance",BindingFlags.Static|BindingFlags.NonPublic);
        Assert.That(method,Is.Not.Null);
        var texture=new Texture2D(16,9,TextureFormat.RGBA32,false,true);
        try {
            var frame=new HumanVisionTextureFrame(7,9,11,texture,44,123,0,false,FrameRowOrigin.UnityBottomLeft,
                FrameColorSpace.Linear,FrameTimestampKind.LocalDecode,1,999,FrameClockDomain.SourceLocalMonotonic,7);
            uint forwarded=99;Action<uint> sink=value=>forwarded=value;
            method.Invoke(null,new object[]{frame,sink});
            Assert.That(forwarded,Is.EqualTo(1u),"Actual ABI declares UnityObserved=1; publication is never SensorVerified=2.");
            Assert.That(frame.SourceTimestampUs,Is.EqualTo(999));Assert.That(frame.PresentationTimestampUs,Is.EqualTo(123));
            frame.PublishedTimestampUs=-1;forwarded=99;
            Assert.Throws<TargetInvocationException>(()=>method.Invoke(null,new object[]{frame,sink}));
            Assert.That(forwarded,Is.EqualTo(99u),"Invalid publication must not be forwarded.");
        } finally {UnityEngine.Object.DestroyImmediate(texture);}
    }
    [Test] public void UprightPreviewAndInferenceShareCoordinateContract() {
        var type=AdapterType();
        var preview=new Texture2D(4,2,TextureFormat.RGBA32,false,true);
        var native=new RenderTexture(4,2,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear); native.Create();
        var read=new Texture2D(4,2,TextureFormat.RGBA32,false,true);
        var before=RenderTexture.active;
        try {
            preview.SetPixels(new[]{Color.red,Color.green,Color.blue,Color.white,Color.cyan,Color.magenta,Color.yellow,Color.black}); preview.Apply();
            type.GetMethod("NormalizeNativeRows",BindingFlags.Static|BindingFlags.NonPublic).Invoke(null,new object[]{preview,native,FrameRowOrigin.UnityBottomLeft});
            RenderTexture.active=native;read.ReadPixels(new Rect(0,0,4,2),0,0);read.Apply();
            var pixels=read.GetPixels();var original=preview.GetPixels();
            for(int y=0;y<2;y++)for(int x=0;x<4;x++) Assert.That((Vector4)pixels[y*4+x],Is.EqualTo((Vector4)original[(1-y)*4+x]).Using(Vector4Comparer));
        } finally { RenderTexture.active=before;native.Release();UnityEngine.Object.DestroyImmediate(native);UnityEngine.Object.DestroyImmediate(preview);UnityEngine.Object.DestroyImmediate(read); }
    }
    private static readonly System.Collections.Generic.IEqualityComparer<Vector4> Vector4Comparer=new PixelComparer();
    private sealed class PixelComparer:System.Collections.Generic.IEqualityComparer<Vector4> {
        public bool Equals(Vector4 a,Vector4 b)=>(a-b).sqrMagnitude<0.0001f; public int GetHashCode(Vector4 value)=>value.GetHashCode();
    }
    [Test] public void UnsupportedPortraitPreviewContinuesButInferenceFails() {
        var type=AdapterType();
        var method=type.GetMethod("ValidateGpuGeometry",BindingFlags.Static|BindingFlags.NonPublic);
        var error=Assert.Throws<TargetInvocationException>(()=>method.Invoke(null,new object[]{720,1280}));
        Assert.That(error.InnerException.Message,Does.Contain("16:9"));
        Assert.DoesNotThrow(()=>method.Invoke(null,new object[]{1280,720}));
    }
    [Test] public void AdapterDetachKeepsPreviewRunning() {
        var source=new Source();var go=new GameObject("adapter test");
        try {
            var adapter=go.AddComponent(AdapterType());
            adapter.GetType().GetMethod("Bind").Invoke(adapter,new object[]{source});
            adapter.GetType().GetMethod("Tick").Invoke(adapter,null);
            Assert.That(adapter.GetType().GetProperty("PreviewTexture").GetValue(adapter),Is.SameAs(source.Texture));
            adapter.GetType().GetMethod("Detach").Invoke(adapter,null);
            Assert.That(source.CloseCount,Is.Zero);source.FrameId++;
            Assert.That(source.TryGetLatestFrame(1,out _),Is.True);
            Assert.That(source.State,Is.EqualTo(InputSourceState.Streaming));
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void SlowInferenceNeverThrottlesPreview() {
        var source=new Source();var go=new GameObject("uninitialized recognition");
        try {
            var adapter=go.AddComponent(AdapterType());adapter.GetType().GetMethod("Bind").Invoke(adapter,new object[]{source});
            for(int i=1;i<=60;i++){source.FrameId=i;adapter.GetType().GetMethod("Tick").Invoke(adapter,null);
                Assert.That(adapter.GetType().GetProperty("LatestPreviewFrameId").GetValue(adapter),Is.EqualTo((long)i));}
            Assert.That(source.CloseCount,Is.Zero);
            Assert.That(adapter.GetType().GetProperty("LastError").GetValue(adapter),Does.Contain("initialized"));
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void LegacyPublicSignaturesRemainAvailable() {
        Assert.That(typeof(HumanVisionCameraManager).GetMethod("StartCamera",Type.EmptyTypes),Is.Not.Null);
        Assert.That(typeof(HumanVisionCameraManager).GetMethod("GetColorImageTex",Type.EmptyTypes).ReturnType,Is.EqualTo(typeof(Texture)));
        Assert.That(typeof(HumanVisionLiveSource).GetMethod("Open",new[]{typeof(HumanVisionCameraSettings)}),Is.Not.Null);
        Assert.That(typeof(VideoPlayerFrameSource).GetMethod("PlayUrl",new[]{typeof(string)}),Is.Not.Null);
        Assert.That(typeof(VideoPlayerFrameSource).GetMethod("SubmitExternalTexture",new[]{typeof(Texture),typeof(long)}),Is.Not.Null,"UPM baseline CLR method identity must remain.");
        Assert.That(typeof(VideoPlayerFrameSource).GetMethod("SubmitExternalTexture",new[]{typeof(Texture),typeof(long),typeof(int),typeof(bool)}),Is.Not.Null);
    }
    // CPU integration records the real managed submission boundary, not model results.
    private sealed class RecordingSession : IHumanVisionSession {
        public int MaxBodies=>8; public HumanVisionBody[] Bodies=>null; public int BodyCount=>0;
        public long Sequence; public long ResultSequence=>Sequence; public long SourceFrameId=>-1; public long SourceTimestampUs=>0;
        public HumanVisionStats Stats=>default;
        public int Count,Width,Height,Stride,Bytes;
        public bool SubmitFrame(IntPtr data,int width,int height,int stride,HumanVisionPixelFormat format,long frame,long time,int bytes) {
            Count++;Width=width;Height=height;Stride=stride;Bytes=bytes;return true;
        }
        public bool PollLatestResult()=>false; public void SetRegions(Rect[] regions,long revision){}
        public bool CopyRegions(long sequence,int[] indices,out long revision){revision=0;return false;}
        public void RefreshStats(){} public void ReconfigureMaxBodies(int count){} public void Dispose(){}
    }
    private static void Set(object owner,string field,object value)=>owner.GetType().GetField(field,BindingFlags.Instance|BindingFlags.NonPublic).SetValue(owner,value);
    private static object Get(object owner,string field)=>owner.GetType().GetField(field,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(owner);
    private static void Late(VideoPlayerFrameSource bridge)=>typeof(VideoPlayerFrameSource).GetMethod("LateUpdate",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(bridge,null);
    private static VideoPlayerFrameSource CpuBridge(GameObject go) {
        var bridge=go.AddComponent<VideoPlayerFrameSource>();
        // EditMode has no MonoBehaviour Awake lifecycle; initialize the actual bridge pool.
        if(((Array)Get(bridge,"_slots")).GetValue(0)==null)
            typeof(VideoPlayerFrameSource).GetMethod("Awake",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(bridge,null);
        return bridge;
    }
    private static void DestroyCpuTestObject(GameObject go,Source source) {
        // Manual Awake in EditMode has no matching Unity OnDisable lifecycle.
        // Drain actual GPU fences and release the persistent pool explicitly.
        var bridge=go.GetComponent<VideoPlayerFrameSource>();
        if(bridge!=null) {
            UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();
            var adapter=go.GetComponent<HumanVisionInputAdapter>();
            if(adapter!=null)typeof(HumanVisionInputAdapter).GetMethod("PollRetirement",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(adapter,null);
            bridge.StopFrames();
        }
        UnityEngine.Object.DestroyImmediate(go);
    }
    [Test] public void CpuAdapterPreservesConfiguredPreviewBindings() {
        var source=new Source();var go=new GameObject("CPU adapter bindings");var displayGo=new GameObject("preview",typeof(RectTransform),typeof(UnityEngine.UI.RawImage),typeof(UnityEngine.UI.AspectRatioFitter));
        try {
            var manager=go.AddComponent<HumanVisionManager>();var session=new RecordingSession();Set(manager,"_session",session);
            var bridge=CpuBridge(go);var display=displayGo.GetComponent<UnityEngine.UI.RawImage>();var fitter=displayGo.GetComponent<UnityEngine.UI.AspectRatioFitter>();
            bridge.Configure(manager,display,fitter);Set(bridge,"_rowOrderReady",true);bridge.BindUnifiedSource(source);
            go.GetComponent<HumanVisionInputAdapter>().Tick();UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();Late(bridge);
            Assert.That(session.Count,Is.EqualTo(1),"Actual CPU readback callback must reach manager submission.");
            Assert.That(display.texture,Is.SameAs(source.Texture));Assert.That(fitter.aspectRatio,Is.EqualTo(16f/9).Within(.001));
            source.FrameId++;Late(bridge);Assert.That(display.texture,Is.SameAs(source.CurrentTexture));
            Assert.That(Get(bridge,"targetDisplay"),Is.SameAs(display));Assert.That(Get(bridge,"aspectRatioFitter"),Is.SameAs(fitter));
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(displayGo);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void HdCpuReadbackKeepsAcceptedGeometryAcrossDelayedCallbackAndReusesAllocation() {
        var source=new Source(1920,1080);var go=new GameObject("HD CPU adapter");
        try {
            var manager=go.AddComponent<HumanVisionManager>();var session=new RecordingSession();Set(manager,"_session",session);
            var bridge=CpuBridge(go);Set(bridge,"_rowOrderReady",true);bridge.BindUnifiedSource(source);
            var adapter=go.GetComponent<HumanVisionInputAdapter>();adapter.Tick();var target=Get(bridge,"_renderTexture");
            Assert.That(session.Count,Is.Zero,"Callback has not run: preview changes before GPU readback completes.");
            Late(bridge);Assert.That(bridge.SourceWidth,Is.EqualTo(1920));Assert.That(bridge.SourceHeight,Is.EqualTo(1080));
            UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();adapter.Tick();
            Assert.That(session.Count,Is.EqualTo(1));Assert.That(session.Width,Is.EqualTo(1280));Assert.That(session.Height,Is.EqualTo(720));
            Assert.That(session.Stride,Is.EqualTo(5120));Assert.That(session.Bytes,Is.EqualTo(1280*720*4));
            source.FrameId++;Set(bridge,"_cpuAdmission",default(GpuFrameAdmissionPolicy));adapter.Tick();
            Assert.That(Get(bridge,"_renderTexture"),Is.SameAs(target),"Preview geometry must not trigger CPU allocation replacement.");
            Late(bridge);UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();adapter.Tick();Assert.That(session.Count,Is.EqualTo(2));
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    // Actual GPU readback geometry differs from the preview at HD/4K. Check all
    // corners as well as the center; checking only a visible 720p scene missed this.
    [TestCase(640,360)] [TestCase(1280,720)]
    [TestCase(1920,1080)] [TestCase(3840,2160)]
    public void OverlayMapsAcceptedCpuPixelsToFullPreviewAtEveryResolution(int width,int height) {
        var source=new Source(width,height);var go=new GameObject("resolution readback");
        var overlayGo=new GameObject("resolution overlay",typeof(RectTransform));
        try {
            var manager=go.AddComponent<HumanVisionManager>();var session=new RecordingSession();Set(manager,"_session",session);
            var bridge=CpuBridge(go);Set(bridge,"_rowOrderReady",true);bridge.BindUnifiedSource(source);
            go.GetComponent<HumanVisionInputAdapter>().Tick();Late(bridge);
            UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();
            Assert.That(session.Count,Is.EqualTo(1));Assert.That(bridge.SourceWidth,Is.EqualTo(width));
            var overlay=overlayGo.AddComponent<HumanVisionOverlay>();overlay.Configure(manager,bridge);
            var convert=typeof(HumanVisionOverlay).GetMethod("ToOverlay",BindingFlags.Instance|BindingFlags.NonPublic);
            var rect=new Rect(-960,-540,1920,1080);
            foreach(var uv in new[]{Vector2.zero,new Vector2(.5f,.5f),Vector2.one,new Vector2(.25f,.75f)}) {
                var pixel=new Vector2(session.Width*uv.x,session.Height*uv.y);
                var actual=(Vector2)convert.Invoke(overlay,new object[]{pixel,rect});
                Assert.That(actual.x,Is.EqualTo(rect.xMin+rect.width*uv.x).Within(.01f));
                Assert.That(actual.y,Is.EqualTo(rect.yMax-rect.height*uv.y).Within(.01f));
            }
        } finally {UnityEngine.Object.DestroyImmediate(overlayGo);DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void CpuReadbacksOverlapWithinTwoSlotsAndRejectQueueGrowth() {
        var source=new Source(1280,720);var go=new GameObject("bounded CPU overlap");
        try {
            var manager=go.AddComponent<HumanVisionManager>();var session=new RecordingSession();Set(manager,"_session",session);
            var bridge=CpuBridge(go);bridge.Configure(manager,null,null);Set(bridge,"_rowOrderReady",true);bridge.ConfigureLiveInput(true);
            Assert.That(bridge.SubmitExternalTexture(source.Texture,1),Is.True);
            Set(bridge,"_cpuAdmission",default(GpuFrameAdmissionPolicy));Assert.That(bridge.SubmitExternalTexture(source.Texture,2),Is.True,"One unfinished readback must not idle the feed.");
            Set(bridge,"_cpuAdmission",default(GpuFrameAdmissionPolicy));Assert.That(bridge.SubmitExternalTexture(source.Texture,3),Is.False,"Never accumulate more than two GPU reads.");
            UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();Assert.That(session.Count,Is.EqualTo(2));
            var timing=bridge.CpuReadbackStats;Assert.That(timing.completed,Is.EqualTo(2));Assert.That(timing.submitted,Is.EqualTo(2));
            Assert.That(timing.pending,Is.Zero);Assert.That(timing.pixelWidth,Is.EqualTo(1280));Assert.That(timing.pixelHeight,Is.EqualTo(720));
            Assert.That(timing.queueRejected,Is.EqualTo(1));Assert.That(timing.readbackMeanMs,Is.GreaterThan(0));
            Assert.That(timing.readbackMaximumMs,Is.GreaterThanOrEqualTo(timing.readbackMeanMs));
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void ResolutionChangeHidesPreviousGeometryUntilANewAcceptedResult() {
        var source=new Source(640,360);var larger=new Texture2D(1920,1080,TextureFormat.RGBA32,false,true);var go=new GameObject("geometry fence");
        try {
            var manager=go.AddComponent<HumanVisionManager>();var session=new RecordingSession();Set(manager,"_session",session);
            var bridge=CpuBridge(go);Set(bridge,"_rowOrderReady",true);bridge.BindUnifiedSource(source);
            go.GetComponent<HumanVisionInputAdapter>().Tick();UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();session.Sequence=1;
            long first=bridge.LatestSubmittedFrameId;Assert.That(bridge.CanPresentResult(first),Is.True);
            Set(bridge,"_cpuAdmission",default(GpuFrameAdmissionPolicy));Assert.That(bridge.SubmitExternalTexture(larger,2),Is.True);
            Assert.That(bridge.CanPresentResult(first),Is.False,"Old pixels must not be drawn against a new readback geometry.");
            UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(larger);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void UnifiedGpuPresentationDoesNotRequireCpuReadbackReadiness() {
        var source=new Source();var go=new GameObject("independent GPU acceptance");
        try {
            var manager=go.AddComponent<HumanVisionManager>();var session=new RecordingSession();Set(manager,"_session",session);
            var bridge=CpuBridge(go);Set(bridge,"_rowOrderReady",true);bridge.BindUnifiedSource(source);
            go.GetComponent<HumanVisionInputAdapter>().Tick();UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();source.Retirement.Poll();session.Sequence=1;
            long accepted=bridge.LatestSubmittedFrameId;Set(bridge,"_acceptReadbacks",false);
            var gate=typeof(VideoPlayerFrameSource).GetMethod("CanPresentUnifiedResult",BindingFlags.Instance|BindingFlags.NonPublic);
            Assert.That(gate,Is.Not.Null,"Unified Vulkan submissions bypass the CPU bridge, so CPU readiness cannot gate GPU results.");
            Assert.That((bool)gate.Invoke(bridge,new object[]{accepted,true}),Is.True);
            Assert.That((bool)gate.Invoke(bridge,new object[]{accepted,false}),Is.False,"CPU geometry retirement still hides old results.");
            Assert.That((bool)gate.Invoke(bridge,new object[]{accepted+1,true}),Is.False,"GPU must still pass the actual adapter frame range.");
        } finally {DestroyCpuTestObject(go,source);UnityEngine.Object.DestroyImmediate(source.Texture);}
    }
    [Test] public void CpuAdmissionKeepsThirtyFpsAcrossAlternatingUnityIntervals() {
        var field=typeof(VideoPlayerFrameSource).GetField("_cpuAdmission",BindingFlags.Instance|BindingFlags.NonPublic);
        Assert.That(field,Is.Not.Null,"CPU/NPU deadline gating drops alternating 30 FPS source arrivals.");
        Assert.That(field.FieldType,Is.EqualTo(typeof(GpuFrameAdmissionPolicy)));
        var policy=default(GpuFrameAdmissionPolicy);int accepted=0;double now=0;
        for(int i=0;i<300;i++){now+=i%2==0?.030:.0366666666667;if(policy.CanSubmit(now)){policy.RecordAccepted();accepted++;}}
        Assert.That(accepted,Is.EqualTo(300));Assert.That(now,Is.EqualTo(10).Within(.0001));
        Assert.That(policy.CanSubmit(now),Is.False,"No unlimited burst at the same clock value.");
    }
}}
