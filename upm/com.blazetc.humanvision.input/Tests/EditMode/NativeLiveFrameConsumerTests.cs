using System;
using System.Collections.Generic;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Experimental.Rendering;
namespace HumanVision.Input.Tests
{
public sealed class NativeLiveFrameConsumerTests
{
private sealed class Fence:ILiveSamplingFence,ISourceCopyFence {internal bool Complete=true;public void Arm(){}public bool IsComplete=>Complete;}
private sealed class Backend:INativeLiveConsumerApi {
 internal ulong SourceSequence,Generation=1,Target,Clock=42;internal uint Color=1;internal int BindCount,CloseCount,Pumps,Callbacks;internal bool DeferCallbacks;internal bool Retiring,Closed,Released,FailBind;
 internal readonly bool[] Held=new bool[3],Ready=new bool[3];internal readonly NativeLiveTextureFrame[] Frames=new NativeLiveTextureFrame[3];internal int next;
 public InputSourceState State=>InputSourceState.Streaming;public string LastError=>"";
 public bool TryGetGeometry(out NativeLiveGeometry g){g=new NativeLiveGeometry{Generation=Generation,SourceWidth=1920,SourceHeight=1080,Width=2,Height=2};return true;}
 public int BindTargets(IntPtr[] textures,uint width,uint height,ulong token){BindCount++;Target=token;Retiring=false;for(int i=0;i<3;i++)Ready[i]=false;return FailBind?-3:0;}
 internal void Produce(int count=1){for(int n=0;n<count;n++){SourceSequence++;if(Target==0||Retiring||Closed)continue;for(int k=0;k<3;k++){int i=(next+k)%3;if(Held[i])continue;Frames[i]=new NativeLiveTextureFrame{Sequence=SourceSequence,Generation=Generation,TargetGeneration=Target,Slot=(uint)i,Width=2,Height=2,SourceWidth=1920,SourceHeight=1080,ClockId=Clock,DecodedTimestampUs=(long)SourceSequence*100,PresentationTimestampUs=-1,RowOrigin=0,ColorSpace=Color};Ready[i]=true;next=(i+1)%3;break;}}}
 public bool TryPoll(ulong after,out NativeLiveTextureFrame frame){frame=default;int pick=-1;for(int i=0;i<3;i++)if(Ready[i]&&!Held[i]&&Frames[i].Sequence>after&&(pick<0||Frames[i].Sequence>Frames[pick].Sequence))pick=i;if(pick<0)return false;frame=Frames[pick];Held[pick]=true;Ready[pick]=false;return true;}
 public int ReleaseSlot(uint slot,ulong token,ulong sequence){if(!Held[slot]||Frames[slot].TargetGeneration!=token||Frames[slot].Sequence!=sequence)return -1;Held[slot]=false;return 0;}
 public int RetireTargets(){Retiring=true;for(int i=0;i<3;i++)Ready[i]=false;return 0;}
 public bool TargetsRetired=>Retiring&&!Held[0]&&!Held[1]&&!Held[2];
 public int Close(){CloseCount++;Closed=true;return 0;}public bool TryRelease(){if(!TargetsRetired||Callbacks>0)return false;Released=true;return true;}
 public void PumpRenderEvent(){Pumps++;if(DeferCallbacks)Callbacks++;Produce();}internal void CompleteCallbacks(){Callbacks=0;}
}

 private readonly List<NativeLiveFrameConsumer> active = new List<NativeLiveFrameConsumer>();
 private readonly List<Fence> fences = new List<Fence>();
 [SetUp] public void RequireGraphics(){if(SystemInfo.graphicsDeviceType==GraphicsDeviceType.Null)Assert.Ignore("Realtime target ownership requires a graphics-enabled Editor.");}
 [TearDown] public void Retire(){foreach(var fence in fences)fence.Complete=true;foreach(var consumer in active){consumer.Close();for(int i=0;i<4;i++)consumer.Tick();Assert.That(consumer.Pending,Is.False);}active.Clear();fences.Clear();}
 private Fence NewFence(bool complete=true){var fence=new Fence{Complete=complete};fences.Add(fence);return fence;}
 private NativeLiveFrameConsumer Consumer(Backend backend,Fence[] supplied=null,bool enabled=true){int index=0;var consumer=new NativeLiveFrameConsumer(backend,77,enabled,()=>{var fence=supplied==null?NewFence():supplied[index++];if(supplied!=null)fences.Add(fence);return fence;});active.Add(consumer);return consumer;}
 private static void Check(bool value,string reason)=>Assert.That(value,Is.True,reason);
 [Test] public void TargetEncodingWorksWithDefaultUnitySampling()
 {
  var consumer=Consumer(new Backend());consumer.Tick();consumer.Tick();
  Assert.That(consumer.TryGetLatestFrame(-1,out var frame),Is.True);
  var texture=(RenderTexture)frame.Texture;
  Assert.That(texture.graphicsFormat,Is.EqualTo(QualitySettings.activeColorSpace==ColorSpace.Linear?GraphicsFormat.R8G8B8A8_SRGB:GraphicsFormat.R8G8B8A8_UNorm));
  Assert.That(frame.RowOrigin,Is.EqualTo(FrameRowOrigin.UnityBottomLeft));
  Assert.That(frame.ColorSpace,Is.EqualTo(FrameColorSpace.Srgb));
 }

 [Test] public void LiveAdvancesWithoutAnAnalysisClaimOrResult()
 {
  var b=new Backend();var c=Consumer(b);c.Tick();c.Tick();Check(c.TryGetLatestFrame(-1,out var first),"first live missing");for(int i=0;i<12;i++)c.Tick();Check(c.TryGetLatestFrame(first.FrameId,out var next),"independent live stopped");Check(next.SourceId==77&&next.Generation==1&&next.FrameId>first.FrameId,"source identity changed");c.Close();c.Tick();
 }

 [Test] public void SlowConsumerSelectsNewestSourceFrameWithoutReplay()
 {
  var b=new Backend();var c=Consumer(b);c.Tick();c.Tick();c.TryGetLatestFrame(-1,out var first);b.Produce(60);ulong newest=b.SourceSequence;c.Tick();Check(c.TryGetLatestFrame(-1,out var frame)&&frame.FrameId==(long)newest,"did not pick latest");Check(frame.FrameId-first.FrameId>=60,"replayed old history");c.Close();c.Tick();
 }

 [Test] public void HeldMatchedTargetRemainsImmutableWhileLiveAdvances()
 {
  var b=new Backend();var c=Consumer(b);c.Tick();c.Tick();c.TryGetLatestFrame(-1,out var first);Check(c.ProtectMatchedFrame(in first),"cannot pin matched");int held=Array.FindIndex(b.Frames,x=>x.Sequence==(ulong)first.FrameId);for(int i=0;i<15;i++)c.Tick();Check(b.Held[held]&&b.Frames[held].Sequence==(ulong)first.FrameId,"held texture overwritten");Check(c.TryGetLatestFrame(first.FrameId,out var latest)&&latest.Texture!=first.Texture,"live stopped behind matched reader");c.Close();c.Tick();
 }

 [Test] public void CopyAndSamplingFencesDelayTargetReleaseAndClose()
 {
  var b=new Backend();var f=new[]{new Fence(),new Fence(),new Fence()};var c=Consumer(b,f);c.Tick();c.Tick();c.TryGetLatestFrame(-1,out var first);Check(c.TryAcquireSourceCopyLease(in first,out var lease),"no copy lease");var copy=NewFence(false);lease.RetireAfter(copy);for(int i=0;i<6;i++)c.Tick();for(int i=0;i<3;i++)f[i].Complete=false;c.Close();c.Tick();Check(c.Pending&&!b.Released,"unproved close released");copy.Complete=true;for(int i=0;i<3;i++)f[i].Complete=true;c.Tick();Check(!c.Pending&&b.Released,"proved close did not retire");
 }

 [Test] public void PreviewDisabledDoesNotStopSourceOrAllocateTargets()
 {
  var b=new Backend();var c=Consumer(b,null,false);for(int i=0;i<10;i++){b.Produce();c.Tick();}Check(c.State==InputSourceState.Streaming&&c.CurrentTexture==null&&b.BindCount==0&&b.CloseCount==0,"preview disabled source");c.Close();c.Tick();
 }

 [Test] public void ReconnectIdentityAdvancesBeforeInference()
 {
  var b=new Backend();var c=Consumer(b);c.Tick();c.Tick();c.TryGetLatestFrame(-1,out var old);b.Generation=3;for(int i=0;i<5;i++)c.Tick();Check(c.TryGetLatestFrame(-1,out var frame)&&frame.Generation==3&&frame.SourceId==old.SourceId,"decoder generation not followed");Check(!c.TryGetMatchedFrame(old.FrameId,old.SourceId,old.Generation,out _),"old binding revived");c.Close();c.Tick();
 }

 [Test] public void PreviewFailureDoesNotStopHealthyAnalysisSource()
 {
  var b=new Backend{FailBind=true};var c=Consumer(b);c.Tick();c.Tick();Check(c.State==InputSourceState.Streaming&&c.CurrentTexture==null&&c.LastError.Length>0,"preview failure stopped source or hid reason");c.Close();c.Tick();
 }
 [Test] public void RejectedCompletedMetadataStillRetiresItsActualLease()
 {
  var backend=new Backend{Clock=0};var consumer=Consumer(backend);consumer.Tick();consumer.Tick();
  for(int i=0;i<4;i++)consumer.Tick();
  Assert.That(consumer.State,Is.EqualTo(InputSourceState.Streaming));Assert.That(consumer.LastError,Is.Not.Empty);
  Assert.That(consumer.Pending,Is.False,"Invalid completed metadata must not leak its proven target lease.");
 }
 [Test] public void UnknownSourceEncodingRemainsUnknownMetadata()
 {
  var consumer=Consumer(new Backend{Color=0});consumer.Tick();consumer.Tick();Assert.That(consumer.TryGetLatestFrame(-1,out var frame),Is.True);
  Assert.That(frame.ColorSpace,Is.EqualTo(FrameColorSpace.Unknown));
 }
 [Test] public void RetiredTargetsDoNotEnqueueCallbacksThatStarveFinalRelease()
 {
  var backend=new Backend{DeferCallbacks=true};var consumer=Consumer(backend);consumer.Tick();backend.CompleteCallbacks();consumer.Tick();backend.CompleteCallbacks();
  consumer.Close();for(int i=0;i<5;i++){consumer.Tick();backend.CompleteCallbacks();}
  Assert.That(consumer.Pending,Is.False);Assert.That(backend.Released,Is.True);
 }
}
}
