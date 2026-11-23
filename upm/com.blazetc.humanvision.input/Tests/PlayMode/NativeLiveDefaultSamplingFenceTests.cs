using System;
using System.Collections;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.TestTools;
namespace HumanVision.Input.Tests
{
    // Actual GPU fences need rendered PlayerLoop frames. EditMode UnityTest
    // yields only EditorApplication.update and is not this execution contract.
    public sealed class NativeLiveDefaultSamplingFenceTests
    {
        private sealed class Backend : INativeLiveConsumerApi
        {
            internal ulong Target, Sequence; internal bool Retiring, Released;
            private readonly bool[] held = new bool[3];
            internal int HeldCount { get { int count=0;foreach(bool value in held)if(value)++count;return count; } }
            public InputSourceState State => InputSourceState.Streaming;
            public string LastError => "";
            public bool TryGetGeometry(out NativeLiveGeometry value) { value=new NativeLiveGeometry {Generation=1,Width=2,Height=2,SourceWidth=2,SourceHeight=2};return true; }
            public int BindTargets(IntPtr[] targets,uint width,uint height,ulong generation) {Target=generation;return 0;}
            public bool TryPoll(ulong after,out NativeLiveTextureFrame value) {
                value=default;if(Retiring)return false;
                for(uint i=0;i<3;i++)if(!held[i]){held[i]=true;value=new NativeLiveTextureFrame {Sequence=++Sequence,Generation=1,TargetGeneration=Target,Slot=i,Width=2,Height=2,SourceWidth=2,SourceHeight=2,ClockId=42,DecodedTimestampUs=(long)Sequence*100};return true;}
                return false;
            }
            public int ReleaseSlot(uint slot,ulong generation,ulong sequence){if(!held[slot]||generation!=Target)return -1;held[slot]=false;return 0;}
            public int RetireTargets(){Retiring=true;return 0;}
            public bool TargetsRetired => Retiring && HeldCount==0;
            public int Close(){return 0;}
            public bool TryRelease(){Released=TargetsRetired;return Released;}
            public void PumpRenderEvent(){}
        }
        private Backend backend;
        private NativeLiveFrameConsumer consumer;
        private GameObject graphicsCamera;
        private RenderTexture graphicsTarget;
        [SetUp] public void CreateRenderedContext()
        {
            Assert.That(Application.isPlaying,Is.True,"This GPU fence test requires the PlayMode runner.");
            Assert.That(SystemInfo.graphicsDeviceType,Is.Not.EqualTo(GraphicsDeviceType.Null));
            Assert.That(SystemInfo.supportsGraphicsFence,Is.True);
            graphicsTarget=new RenderTexture(16,16,0);Assert.That(graphicsTarget.Create(),Is.True);
            graphicsCamera=new GameObject("live fence test camera",typeof(Camera));
            graphicsCamera.GetComponent<Camera>().targetTexture=graphicsTarget;
            backend=new Backend();consumer=new NativeLiveFrameConsumer(backend,77,true);
        }
        [UnityTearDown] public IEnumerator RetireRenderedContext()
        {
            bool pending=false;
            if(consumer!=null){consumer.Close();double deadline=Time.realtimeSinceStartupAsDouble+5;
                while(consumer.Pending&&Time.realtimeSinceStartupAsDouble<deadline){consumer.Tick();yield return null;}
                pending=consumer.Pending;
            }
            // The production retirement pump retains an unproved consumer.
            // Never bypass its GPU fence merely to clean up a failed assertion.
            consumer=null;backend=null;
            UnityEngine.Object.Destroy(graphicsCamera);UnityEngine.Object.Destroy(graphicsTarget);
            yield return null;
            Assert.That(pending,Is.False,"Actual GPU completion must allow the default sampling fences to retire.");
        }
        [UnityTest] public IEnumerator DefaultSamplingFencesAdvanceAndRetireActualGpuWork()
        {
            double deadline=Time.realtimeSinceStartupAsDouble+5;long first=-1,last=-1;
            while(Time.realtimeSinceStartupAsDouble<deadline&&last<first+8){
                consumer.Tick();Assert.That(consumer.LastError,Is.Empty);
                if(consumer.TryGetLatestFrame(-1,out var frame)){last=frame.FrameId;if(first<0)first=last;}
                yield return null;
            }
            Assert.That(last,Is.GreaterThanOrEqualTo(first+8),"Real GPU proof must release slots repeatedly, beyond the three-slot capacity.");
            Assert.That(backend.Released,Is.False);
        }
    }
}
