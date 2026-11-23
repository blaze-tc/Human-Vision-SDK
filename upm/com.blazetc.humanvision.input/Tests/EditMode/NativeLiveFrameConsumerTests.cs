using System;
using System.Collections.Generic;
using System.Reflection;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Experimental.Rendering;
namespace HumanVision.Input.Tests
{
public sealed class NativeLiveFrameConsumerTests
{
private sealed class Fence:ILiveSamplingFence,ISourceCopyFence {internal bool Complete=true;internal int Arms;public void Arm(){++Arms;}public bool IsComplete=>Complete;}
private sealed class Backend:INativeLiveConsumerApi {
 internal ulong SourceSequence,Generation=1,Target,Clock=42;internal uint Color=1;internal int BindCount,CloseCount,Pumps,Callbacks;internal bool DeferCallbacks;internal bool Retiring,Closed,Released,FailBind;
 internal readonly bool[] Held=new bool[3],Ready=new bool[3];internal readonly NativeLiveTextureFrame[] Frames=new NativeLiveTextureFrame[3];internal int next;
 internal readonly List<ulong> ReleasedSequences=new List<ulong>();
 public InputSourceState State=>InputSourceState.Streaming;public string LastError=>"";
 public bool TryGetGeometry(out NativeLiveGeometry g){g=new NativeLiveGeometry{Generation=Generation,SourceWidth=1920,SourceHeight=1080,Width=2,Height=2};return true;}
 public int BindTargets(IntPtr[] textures,uint width,uint height,ulong token){BindCount++;Target=token;Retiring=false;for(int i=0;i<3;i++)Ready[i]=false;return FailBind?-3:0;}
 internal void Produce(int count=1){for(int n=0;n<count;n++){SourceSequence++;if(Target==0||Retiring||Closed)continue;for(int k=0;k<3;k++){int i=(next+k)%3;if(Held[i])continue;Frames[i]=new NativeLiveTextureFrame{Sequence=SourceSequence,Generation=Generation,TargetGeneration=Target,Slot=(uint)i,Width=2,Height=2,SourceWidth=1920,SourceHeight=1080,ClockId=Clock,DecodedTimestampUs=(long)SourceSequence*100,PresentationTimestampUs=-1,RowOrigin=0,ColorSpace=Color};Ready[i]=true;next=(i+1)%3;break;}}}
 public bool TryPoll(ulong after,out NativeLiveTextureFrame frame){frame=default;int pick=-1;for(int i=0;i<3;i++)if(Ready[i]&&!Held[i]&&Frames[i].Sequence>after&&(pick<0||Frames[i].Sequence>Frames[pick].Sequence))pick=i;if(pick<0)return false;frame=Frames[pick];Held[pick]=true;Ready[pick]=false;return true;}
 public int ReleaseSlot(uint slot,ulong token,ulong sequence){if(!Held[slot]||Frames[slot].TargetGeneration!=token||Frames[slot].Sequence!=sequence)return -1;Held[slot]=false;ReleasedSequences.Add(sequence);return 0;}
 public int RetireTargets(){Retiring=true;for(int i=0;i<3;i++)Ready[i]=false;return 0;}
 public bool TargetsRetired=>Retiring&&!Held[0]&&!Held[1]&&!Held[2];
 public int Close(){CloseCount++;Closed=true;return 0;}public bool TryRelease(){if(!TargetsRetired||Callbacks>0)return false;Released=true;return true;}
 public void PumpRenderEvent(){Pumps++;if(DeferCallbacks)Callbacks++;Produce();}internal void CompleteCallbacks(){Callbacks=0;}
}

 private readonly List<NativeLiveFrameConsumer> active = new List<NativeLiveFrameConsumer>();
 private readonly List<Fence> fences = new List<Fence>();
 [SetUp] public void RequireGraphics(){if(SystemInfo.graphicsDeviceType==GraphicsDeviceType.Null)Assert.Ignore("Realtime target ownership requires a graphics-enabled Editor.");}
 [TearDown] public void Retire(){foreach(var fence in fences)fence.Complete=true;bool pending=false;try{foreach(var consumer in active){consumer.Close();for(int i=0;i<4;i++)consumer.Tick();pending|=consumer.Pending;}}finally{active.Clear();fences.Clear();}Assert.That(pending,Is.False,"Every controlled consumer must retire without contaminating the next test.");}
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

 [Test] public void ClearingMatchedProtectionWaitsForSamplingAndQueuedCopyBeforeReturningSlot()
 {
  var backend=new Backend();var sampling=new[]{new Fence(),new Fence(),new Fence()};
  var consumer=Consumer(backend,sampling);consumer.Tick();consumer.Tick();
  Assert.That(consumer.TryGetLatestFrame(-1,out var photo),Is.True);
  Assert.That(consumer.ProtectMatchedFrame(in photo),Is.True);
  int index=Array.FindIndex(backend.Frames,x=>x.Sequence==(ulong)photo.FrameId);
  Assert.That(consumer.TryAcquireSourceCopyLease(in photo,out var copy),Is.True);
  var copyFence=NewFence(false);copy.RetireAfter(copyFence);
  for(int i=0;i<6;i++)consumer.Tick();
  sampling[index].Complete=false;ReleaseMatchedProtection(consumer,in photo);
  for(int i=0;i<3;i++)consumer.Tick();
  Assert.That(sampling[index].Arms,Is.GreaterThan(0),"Cleared presentation must re-arm GPU sampling retirement.");
  Assert.That(backend.ReleasedSequences.Contains((ulong)photo.FrameId),Is.False);
  sampling[index].Complete=true;consumer.Tick();
  Assert.That(backend.ReleasedSequences.Contains((ulong)photo.FrameId),Is.False,"A queued copy still owns the pixels.");
  copyFence.Complete=true;consumer.Tick();
  Assert.That(backend.ReleasedSequences.Contains((ulong)photo.FrameId),Is.True,"Finished photo retention must return its live target without restarting input.");
  Assert.That(consumer.TryGetLatestFrame(photo.FrameId,out _),Is.True);
 }

 [Test] public void ClearingAnOldPhotoCannotUnprotectItsReplacementOrANewGeneration()
 {
  var backend=new Backend();var consumer=Consumer(backend);consumer.Tick();consumer.Tick();
  consumer.TryGetLatestFrame(-1,out var old);Assert.That(consumer.ProtectMatchedFrame(in old),Is.True);
  for(int i=0;i<3;i++)consumer.Tick();consumer.TryGetLatestFrame(-1,out var replacement);
  Assert.That(consumer.ProtectMatchedFrame(in replacement),Is.True);
  ReleaseMatchedProtection(consumer,in old);for(int i=0;i<6;i++)consumer.Tick();
  Assert.That(backend.ReleasedSequences.Contains((ulong)replacement.FrameId),Is.False);
  backend.Generation=2;for(int i=0;i<5;i++)consumer.Tick();
  Assert.That(consumer.TryGetLatestFrame(-1,out var nextGeneration),Is.True);
  Assert.That(consumer.ProtectMatchedFrame(in nextGeneration),Is.True);
  ReleaseMatchedProtection(consumer,in replacement);for(int i=0;i<6;i++)consumer.Tick();
  Assert.That(backend.ReleasedSequences.Contains((ulong)nextGeneration.FrameId),Is.False);
 }

 private static void ReleaseMatchedProtection(NativeLiveFrameConsumer consumer,in HumanVisionTextureFrame frame)
 {
  // Missing retirement notification on the previous implementation leaves the
  // target pinned; keep that version runnable for a behavioral RED regression.
  typeof(NativeLiveFrameConsumer).GetMethod("ReleaseMatchedFrame",BindingFlags.Instance|BindingFlags.NonPublic)
   ?.Invoke(consumer,new object[]{frame});
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
