using System;
using System.Collections;
using System.Reflection;
using UnityEngine.Video;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.TestTools;
namespace HumanVision.Input.Tests
{
    public class UnitySourceLifecycleTests
    {
        private GameObject graphicsCamera;
        private RenderTexture graphicsTarget;
        [SetUp] public void CreateGraphicsContext()
        {
            graphicsCamera = new GameObject("input fixture graphics camera", typeof(Camera));
            graphicsTarget = new RenderTexture(64, 64, 0);
            graphicsTarget.Create();
            graphicsCamera.GetComponent<Camera>().targetTexture = graphicsTarget;
            graphicsCamera.AddComponent<FixtureRenderer>();
        }
        [UnityTearDown] public IEnumerator ReleaseGraphicsContext()
        {
            yield return null;
            UnityEngine.Object.Destroy(graphicsCamera);
            UnityEngine.Object.Destroy(graphicsTarget);
            yield return null;
        }
        const string Fixture="E:/Project/Human Vision SDK/video-1.mp4";
        sealed class Fence:ISourceCopyFence
        {
            public bool Complete;
            public bool IsComplete=>Complete;
        }
        static IEnumerator WaitForFrame(VideoFrameSource s)
        {
            var end=Time.realtimeSinceStartup+25;
            while(s.CurrentTexture==null&&Time.realtimeSinceStartup<end)
            {
                Assert.AreNotEqual(InputSourceState.Error,s.State,s.LastError);
                yield return null;
            }
            Assert.IsNotNull(s.CurrentTexture,s.LastError);
        }
        [UnityTest] public IEnumerator PreviewWithoutModels()
        {
            var go=new GameObject("source");
            var ui=new GameObject("preview",typeof(RectTransform),typeof(CanvasRenderer),typeof(RawImage));
            var s=go.AddComponent<VideoFrameSource>();
            var p=ui.AddComponent<FramePreview>();
            try
            {
                p.Bind(s);
                s.Open(new HumanVisionSourceSettings
                {
                    Location=Fixture,RequestedWidth=1920,RequestedHeight=1080
                }
                );
                yield return WaitForFrame(s);
                yield return null;
                Assert.AreSame(s.CurrentTexture,ui.GetComponent<RawImage>().texture);
                Assert.IsTrue(s.TryGetLatestFrame(-1,out var first));
                Assert.AreEqual(FrameRowOrigin.UnityBottomLeft,first.RowOrigin);
                Assert.AreEqual(FrameClockDomain.InputMonotonic,first.SourceClockDomain);
                Assert.LessOrEqual(first.SourceTimestampUs,first.PublishedTimestampUs);
                Debug.Log($"INPUT_FIXTURE requested=1920x1080 actual={first.Width}x{first.Height}");
                var end=Time.realtimeSinceStartup+3;
                var next=first;
                while(next.FrameId==first.FrameId&&Time.realtimeSinceStartup<end)
                {
                    yield return null;
                    if(s.TryGetLatestFrame(first.FrameId,out var found))next=found;
                }
                Assert.Greater(next.FrameId,first.FrameId);
            }
            finally
            {
                s.Close();
                UnityEngine.Object.Destroy(go);
                UnityEngine.Object.Destroy(ui);
            }
        }
        [UnityTest] public IEnumerator SwitchAndPauseRejectLateFrames()
        {
            var go=new GameObject("source");
            var s=go.AddComponent<VideoFrameSource>();
            try
            {
                s.Open(new HumanVisionSourceSettings
                {
                    Location=Fixture
                }
                );
                yield return WaitForFrame(s);
                Assert.IsTrue(s.TryGetLatestFrame(-1,out var old));
                Assert.IsTrue(s.TryAcquireSourceCopyLease(in old,out var lease));
                var f=new Fence();
                lease.RetireAfter(f);
                var previousPlayer=(VideoPlayer)typeof(VideoFrameSource).GetField("player",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(s);
                var late=(VideoPlayer.FrameReadyEventHandler)typeof(VideoFrameSource).GetField("ready",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(s);
                s.Pause();
                late(previousPlayer,123);
                Assert.IsFalse(s.TryGetLatestFrame(-1,out _));
                Assert.IsFalse(s.TryAcquireSourceCopyLease(in old,out _));
                s.Resume();
                yield return WaitForFrame(s);
                Assert.IsTrue(s.TryGetLatestFrame(-1,out var current));
                Assert.Greater(current.Generation,old.Generation);
                Assert.Greater(current.FrameId,old.FrameId);
                s.Close();
                s.Open(new HumanVisionSourceSettings
                {
                    Location=Fixture
                }
                );
                late(previousPlayer,456);
                Assert.IsFalse(s.TryGetLatestFrame(-1,out _));
                yield return WaitForFrame(s);
                Assert.IsTrue(s.TryGetLatestFrame(-1,out var reopened));
                Assert.Greater(reopened.Generation,current.Generation);
                s.Close();
                Assert.IsNotNull(old.Texture);
                go.SetActive(false);
                yield return null;
                Assert.IsNotNull(old.Texture);
                UnityEngine.Object.Destroy(go);
                yield return null;
                Assert.IsNotNull(old.Texture);
                f.Complete=true;
                yield return null;
                yield return null;
                Assert.IsTrue(old.Texture==null);
                Assert.AreEqual(0,s.PendingRetirementCount);
                Assert.AreEqual(InputSourceState.Stopped,s.State);
            }
            finally
            {
                if(go!=null)UnityEngine.Object.Destroy(go);
            }
        }
        [UnityTest] public IEnumerator PublicationEncodingMatchesTextureFlag()
        {
            var go = new GameObject("encoding fixture");
            var source = go.AddComponent<FixtureSource>();
            var input = new Texture2D(32,18,TextureFormat.RGBA32,false,false);
            source.Open(new HumanVisionSourceSettings());
            source.Feed(input);
            Assert.IsTrue(source.TryGetLatestFrame(-1,out var frame));
            var expected = QualitySettings.activeColorSpace == ColorSpace.Gamma ? FrameColorSpace.Srgb : FrameColorSpace.Linear;
            Assert.AreEqual(expected, frame.ColorSpace);
            source.Close();
            UnityEngine.Object.Destroy(go);
            UnityEngine.Object.Destroy(input);
            yield return null;
        }
        [UnityTest] public IEnumerator GeometryChangeRetiresOutstandingCopy()
        {
            var go=new GameObject("fixture source");
            var s=go.AddComponent<FixtureSource>();
            var a=new Texture2D(32,18);
            var b=new Texture2D(18,32);
            try
            {
                s.Open(new HumanVisionSourceSettings
                {
                    RequestedWidth=1920,RequestedHeight=1080
                }
                );
                s.Feed(a);
                Assert.IsTrue(s.TryGetLatestFrame(-1,out var first));
                Assert.AreEqual(32,first.Width);
                Assert.IsTrue(s.TryAcquireSourceCopyLease(in first,out var lease));
                var f=new Fence();
                lease.RetireAfter(f);
                s.Feed(b);
                Assert.IsTrue(s.TryGetLatestFrame(-1,out var second));
                Assert.Greater(second.Generation,first.Generation);
                Assert.AreEqual(18,second.Width);
                Assert.AreEqual(32,second.Height);
                Assert.IsNotNull(first.Texture);
                s.Close();
                UnityEngine.Object.Destroy(go);
                yield return null;
                Assert.IsNotNull(first.Texture);
                f.Complete=true;
                yield return null;
                yield return null;
                Assert.IsTrue(first.Texture==null);
                Assert.AreEqual(0,s.PendingRetirementCount);
            }
            finally
            {
                UnityEngine.Object.Destroy(a);
                UnityEngine.Object.Destroy(b);
                if(go!=null)UnityEngine.Object.Destroy(go);
            }
        }
        [UnityTest] public IEnumerator LateErrorAfterPauseCannotStopSource()
        {
            var go=new GameObject("paused error fixture");
            var source=go.AddComponent<VideoFrameSource>();
            try
            {
                source.Open(new HumanVisionSourceSettings { Location=Fixture });
                yield return WaitForFrame(source);
                var player=PlayerOf(source);
                var lateError=(VideoPlayer.ErrorEventHandler)typeof(VideoFrameSource).GetField("error",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(source);
                source.Pause();
                lateError(player,"old paused error");
                Assert.AreEqual(InputSourceState.Stopped,source.State);
                Assert.IsEmpty(source.LastError);
                source.Resume();
                yield return WaitForFrame(source);
            }
            finally { source.Close();UnityEngine.Object.Destroy(go); }
        }

        [UnityTest] public IEnumerator LateCallbacksAfterRawDestroyAreIgnored()
        {
            var go=new GameObject("destroyed callback fixture");
            var source=go.AddComponent<VideoFrameSource>();
            try
            {
                source.Open(new HumanVisionSourceSettings { Location=Fixture });
                yield return WaitForFrame(source);
                var player=PlayerOf(source);
                var lateReady=(VideoPlayer.FrameReadyEventHandler)typeof(VideoFrameSource).GetField("ready",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(source);
                var lateError=(VideoPlayer.ErrorEventHandler)typeof(VideoFrameSource).GetField("error",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(source);
                source.Close();
                yield return null;
                Assert.IsTrue(player==null,"Raw decoder should now be a Unity destroyed-object wrapper.");
                Assert.DoesNotThrow(()=>lateReady(player,123));
                Assert.DoesNotThrow(()=>lateError(player,"old destroyed error"));
                Assert.AreEqual(InputSourceState.Stopped,source.State);
                Assert.IsEmpty(source.LastError);
                Assert.IsNull(source.CurrentTexture);
            }
            finally { source.Close();UnityEngine.Object.Destroy(go); }
        }

        private static VideoPlayer PlayerOf(VideoFrameSource source)
        {
            return (VideoPlayer)typeof(VideoFrameSource).GetField("player",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(source);
        }

        [UnityTest] public IEnumerator NoConsumerCloseCleansImmediateErrorAndRepeatedReopen()
        {
            var go = new GameObject("no-consumer lifecycle fixture");
            var source = go.AddComponent<VideoFrameSource>();
            try
            {
                source.Open(new HumanVisionSourceSettings { Location = Fixture });
                var immediateOwner = PlayerOf(source).gameObject;
                source.Close();
                source.Close();
                Assert.AreEqual(InputSourceState.Stopped,source.State);
                Assert.AreEqual(0,source.PendingRetirementCount);
                yield return null;
                Assert.IsTrue(immediateOwner==null,"Immediate close must release a decoder that never published.");

                source.Open(new HumanVisionSourceSettings { Location = Fixture });
                var failedPlayer = PlayerOf(source);
                var failedOwner = failedPlayer.gameObject;
                var fault = (VideoPlayer.ErrorEventHandler)typeof(VideoFrameSource).GetField("error",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(source);
                fault(failedPlayer,"fixture decoder error");
                Assert.AreEqual(InputSourceState.Error,source.State);
                Assert.AreEqual("fixture decoder error",source.LastError);
                source.Close();
                source.Close();
                Assert.AreEqual(InputSourceState.Stopped,source.State);
                Assert.AreEqual(0,source.PendingRetirementCount);
                yield return null;
                Assert.IsTrue(failedOwner==null);

                long previousId=-1;
                for(int cycle=0;cycle<5;cycle++)
                {
                    source.Open(new HumanVisionSourceSettings { Location = Fixture });
                    yield return WaitForFrame(source);
                    Assert.IsTrue(source.TryGetLatestFrame(previousId,out var frame));
                    previousId=frame.FrameId;
                    var decoderOwner=PlayerOf(source).gameObject;
                    var normalizer=typeof(UnityTextureFrameSource).GetField("normalizer",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(source);
                    var material=(Material)typeof(FrameTextureNormalizer).GetField("material",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(normalizer);
                    source.Close();
                    source.Close();
                    Assert.AreEqual(InputSourceState.Stopped,source.State);
                    Assert.AreEqual(0,source.PendingRetirementCount);
                    Assert.IsNull(source.CurrentTexture);
                    Assert.IsFalse(source.TryAcquireSourceCopyLease(in frame,out _));
                    yield return null;
                    Assert.IsTrue(frame.Texture==null);
                    Assert.IsTrue(decoderOwner==null);
                    Assert.IsTrue(material==null,"No-consumer close must also release its normalization material.");
                    go.SetActive(false);
                    go.SetActive(true);
                }
            }
            finally
            {
                source.Close();
                UnityEngine.Object.Destroy(go);
            }
        }

        private static void BlitDiagnostic(Texture input, RenderTexture output)
        {
            var previous=RenderTexture.active;
            try { Graphics.Blit(input,output); }
            finally { RenderTexture.active=previous; }
        }

        private static Texture2D ReadDiagnostic(RenderTexture texture)
        {
            var read=new Texture2D(texture.width,texture.height,TextureFormat.RGBA32,false,true);
            var previous=RenderTexture.active;
            try { RenderTexture.active=texture; read.ReadPixels(new Rect(0,0,texture.width,texture.height),0,0); read.Apply(); }
            finally { RenderTexture.active=previous; }
            return read;
        }

        [UnityTest] public IEnumerator SameTurnUnityCopyAfterClosePreservesPixels()
        {
            var go=new GameObject("same-turn marker fixture");
            var source=go.AddComponent<FixtureSource>();
            var input=new Texture2D(6,4,TextureFormat.RGBA32,false,true);
            var target=new RenderTexture(6,4,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
            target.Create();
            var pixels=new Color[24];
            for(int y=0;y<4;y++) for(int x=0;x<6;x++) pixels[y*6+x]=new Color((x+1)/8f,(y+1)/6f,(x+y+1)/12f,1);
            input.SetPixels(pixels);input.Apply();
            source.Open(new HumanVisionSourceSettings());
            source.Feed(input);
            Assert.IsTrue(source.TryGetLatestFrame(-1,out var frame));
            BlitDiagnostic(frame.Texture,target);
            source.Close();
            UnityEngine.Object.Destroy(go);
            UnityEngine.Object.Destroy(input);
            // First diagnostic GPU readback occurs only after close/destroy, never before shutdown.
            var read=ReadDiagnostic(target);
            for(int y=0;y<4;y++) for(int x=0;x<6;x++)
            {
                var actual=read.GetPixel(x,y);var expected=pixels[y*6+x];
                Assert.That(actual.r,Is.EqualTo(expected.r).Within(.015));
                Assert.That(actual.g,Is.EqualTo(expected.g).Within(.015));
                Assert.That(actual.b,Is.EqualTo(expected.b).Within(.015));
            }
            Assert.AreEqual(0,source.PendingRetirementCount);
            UnityEngine.Object.Destroy(read);UnityEngine.Object.Destroy(target);
            yield return null;
        }

        [UnityTest] public IEnumerator SameTurnVideoPublicationCopySurvivesClose()
        {
            var go=new GameObject("same-turn MP4 fixture");
            var source=go.AddComponent<VideoFrameSource>();
            RenderTexture snapshot=null,reference=null;
            Texture2D read=null,expectedRead=null;
            bool captured=false;
            try
            {
                source.Open(new HumanVisionSourceSettings { Location=Fixture });
                var player=PlayerOf(source);
                // Registered after the production handler: normalization has just been queued,
                // then this fixture queues independent copies and closes in the same callback turn.
                player.frameReady+=(sender,index)=>
                {
                    // Exercise an advanced decoded frame, beyond the initial decoder callback.
                    if(captured||index<3||!source.TryGetLatestFrame(-1,out var frame))return;
                    snapshot=new RenderTexture(frame.Width,frame.Height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
                    reference=new RenderTexture(frame.Width,frame.Height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
                    snapshot.Create();reference.Create();
                    BlitDiagnostic(frame.Texture,snapshot);
                    BlitDiagnostic(sender.texture,reference);
                    captured=true;
                    source.Close();
                    UnityEngine.Object.Destroy(go);
                };
                var end=Time.realtimeSinceStartup+25;
                while(!captured&&Time.realtimeSinceStartup<end)yield return null;
                Assert.IsTrue(captured,"Actual VideoPlayer frameReady publication never arrived.");
                read=ReadDiagnostic(snapshot);
                expectedRead=ReadDiagnostic(reference);
                int minimum=255,maximum=0;
                for(int y=0;y<snapshot.height;y+=Math.Max(1,snapshot.height/16))
                    for(int x=0;x<snapshot.width;x+=Math.Max(1,snapshot.width/16))
                    {
                        var actual=read.GetPixel(x,y);var expected=expectedRead.GetPixel(x,y);
                        Assert.That(actual.r,Is.EqualTo(expected.r).Within(.02));
                        Assert.That(actual.g,Is.EqualTo(expected.g).Within(.02));
                        Assert.That(actual.b,Is.EqualTo(expected.b).Within(.02));
                        minimum=Math.Min(minimum,(int)(expected.r*255));maximum=Math.Max(maximum,(int)(expected.r*255));
                    }
                Assert.Greater(maximum-minimum,20,"Actual decoded fixture should contain nonuniform image content.");
                Assert.AreEqual(0,source.PendingRetirementCount);
            }
            finally
            {
                if(go!=null){source.Close();UnityEngine.Object.Destroy(go);}
                if(snapshot!=null)UnityEngine.Object.Destroy(snapshot);
                if(reference!=null)UnityEngine.Object.Destroy(reference);
                if(read!=null)UnityEngine.Object.Destroy(read);
                if(expectedRead!=null)UnityEngine.Object.Destroy(expectedRead);
            }
        }

        [UnityTest] public IEnumerator UnregisteredOutputIsDestroyedOnClose()
        {
            var go=new GameObject("failed output fixture");
            var s=go.AddComponent<FixtureSource>();
            var input=new Texture2D(32,18);
            s.Open(new HumanVisionSourceSettings());
            var normalizer=new FrameTextureNormalizer();
            var output=normalizer.Update(input,0,false,false);
            typeof(UnityTextureFrameSource).GetField("normalizer",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(s,normalizer);
            s.Close();
            yield return null;
            Assert.IsTrue(output==null,"An output whose blit/registration failed still needs an owner that destroys it.");
            UnityEngine.Object.Destroy(go);
            UnityEngine.Object.Destroy(input);
        }
        [UnityTest] public IEnumerator PublicationHasNoPerFrameManagedAllocationsAfterWarmup()
        {
            var go=new GameObject("fixture source");
            var s=go.AddComponent<FixtureSource>();
            var input=new Texture2D(32,18);
            try
            {
                s.Open(new HumanVisionSourceSettings());
                for(int i=0;i<8;i++)s.Feed(input);
                var before=GC.GetAllocatedBytesForCurrentThread();
                for(int i=0;i<512;i++)s.Feed(input);
                Assert.AreEqual(0,GC.GetAllocatedBytesForCurrentThread()-before);
            }
            finally
            {
                s.Close();
                UnityEngine.Object.Destroy(go);
                UnityEngine.Object.Destroy(input);
            }
            yield return null;
        }
        [UnityTest] public IEnumerator PreviewHasNoPerFrameManagedAllocationsAfterWarmup()
        {
            var go=new GameObject("source");
            var ui=new GameObject("preview",typeof(RectTransform),typeof(CanvasRenderer),typeof(RawImage));
            var s=go.AddComponent<VideoFrameSource>();
            var p=ui.AddComponent<FramePreview>();
            try
            {
                s.Open(new HumanVisionSourceSettings
                {
                    Location=Fixture
                }
                );
                p.Bind(s);
                yield return WaitForFrame(s);
                for(int i=0;i<8;i++)p.Refresh();
                var start=GC.GetAllocatedBytesForCurrentThread();
                for(int i=0;i<1024;i++)
                {
                    p.Refresh();
                    s.TryGetLatestFrame(-1,out _);
                }
                Assert.AreEqual(0,GC.GetAllocatedBytesForCurrentThread()-start);
            }
            finally
            {
                s.Close();
                UnityEngine.Object.Destroy(go);
                UnityEngine.Object.Destroy(ui);
            }
        }
    }
    public sealed class FixtureRenderer : MonoBehaviour
    {
        private void Update() { GetComponent<Camera>().Render(); }
    }
    public sealed class FixtureSource:UnityTextureFrameSource
    {
        public override void Open(HumanVisionSourceSettings settings)
        {
            Close();
            BeginOutput();
            State=InputSourceState.Opening;
        }
        public void Feed(Texture texture)
        {
            Publish(texture,0,false,-1,ActiveGeneration);
        }
        public override void Close()
        {
            RetireOutput();
            State=InputSourceState.Stopped;
        }
    }
}
