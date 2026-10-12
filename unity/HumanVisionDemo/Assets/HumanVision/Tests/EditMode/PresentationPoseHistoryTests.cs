using NUnit.Framework;
using UnityEngine;
using HumanVision.Demo;
namespace HumanVision.Tests {
 public sealed class PresentationPoseHistoryTests {
  private static HumanVisionBody Body(long id,float x){var b=new HumanVisionBody{StableTrackId=id,TrackId=(int)id};
   b.Joints[0]=new HumanVisionJoint(new Vector2(x,0),new Vector2(x/100,0),1,true);return b;}
  [Test] public void SamplesDisplayTimestampWithoutChangingRawPoseOrCountingInference(){
   var h=new PresentationPoseHistory();var a=Body(1,10);var b=Body(1,30);
   h.Add(1,1000000,new[]{a},1);h.Add(2,1100000,new[]{b},1);h.Sample(1050000);
   Assert.AreEqual(1,h.Count);Assert.AreEqual(20,h.Bodies[0].Joints[0].Pixel.x,.001);
   Assert.AreEqual(10,a.Joints[0].Pixel.x);Assert.AreEqual(30,b.Joints[0].Pixel.x);
   Assert.AreEqual("interpolated",h.Mode);Assert.AreEqual(1,h.BeforeFrame);Assert.AreEqual(2,h.AfterFrame);
  }
  [Test] public void DoesNotInterpolateAcrossDifferentPeopleOrKeepAnOldPoseIndefinitely(){
   var h=new PresentationPoseHistory();h.Add(1,1000000,new[]{Body(1,10)},1);h.Add(2,1100000,new[]{Body(2,90)},1);
   h.Sample(1050000);Assert.AreEqual(1,h.Count);Assert.AreEqual(1,h.Bodies[0].StableTrackId);Assert.AreEqual(10,h.Bodies[0].Joints[0].Pixel.x);
   h.Sample(1100000);Assert.AreEqual(2,h.Bodies[0].StableTrackId);
   h.Sample(1450000);Assert.AreEqual(0,h.Count);h.Reset();h.Sample(1100000);Assert.AreEqual(0,h.Count);
  }
  [Test] public void SlowGenuineObservationsDoNotInsertBlankRenderFrames(){
   var h=new PresentationPoseHistory();h.Add(1,1000000,new[]{Body(1,10)},1);h.Add(2,1125000,new[]{Body(1,30)},1);
   for(long t=1000000;t<=1250000;t+=16000){h.Sample(t);Assert.AreEqual(1,h.Count,"blank display at "+t);}
   h.Sample(1500000);Assert.AreEqual(0,h.Count,"bounded hold must expire");
  }
  [Test] public void RejectsLateResultsAndEmptyObservationClearsVisibleBodies(){
   var h=new PresentationPoseHistory();h.Add(2,1100000,new[]{Body(1,30)},1);h.Add(1,1000000,new[]{Body(1,10)},1);
   h.Sample(1100000);Assert.AreEqual(30,h.Bodies[0].Joints[0].Pixel.x);
   h.Add(3,1200000,new HumanVisionBody[0],0);h.Sample(1200000);Assert.AreEqual(0,h.Count);
  }
 }
}
