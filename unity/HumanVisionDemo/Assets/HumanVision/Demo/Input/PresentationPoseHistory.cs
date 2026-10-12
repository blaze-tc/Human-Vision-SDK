using System;
using UnityEngine;
namespace HumanVision.Demo {
 /// <summary>仅用于显示的有界骨骼时间线。所有输入先复制；不改变原始 SDK 数据或新骨骼计数。</summary>
 internal sealed class PresentationPoseHistory {
  private sealed class Snapshot {
   internal long frame=-1,time;internal int count;
   internal readonly HumanVisionBody[] bodies=Allocate();
  }
  private readonly Snapshot[] history=new Snapshot[8];
  private int next;private long latestTime=-1;
  internal readonly HumanVisionBody[] Bodies=Allocate();
  internal int Count{get;private set;}
  internal long BeforeFrame{get;private set;}=-1;
  internal long AfterFrame{get;private set;}=-1;
  internal string Mode{get;private set;}="unavailable";
  internal PresentationPoseHistory(){for(int i=0;i<history.Length;i++)history[i]=new Snapshot();}
  private static HumanVisionBody[] Allocate(){var a=new HumanVisionBody[8];for(int i=0;i<a.Length;i++)a[i]=new HumanVisionBody();return a;}
  internal void Reset(){foreach(var s in history){s.frame=-1;s.count=0;}latestTime=-1;next=0;Count=0;BeforeFrame=AfterFrame=-1;Mode="unavailable";}
  internal void Add(long frame,long time,HumanVisionBody[] bodies,int count){
   if(frame<0||time<=latestTime||bodies==null||count<0||count>8||count>bodies.Length)return;
   var s=history[next];next=(next+1)%history.Length;s.frame=frame;s.time=time;s.count=count;latestTime=time;
   for(int i=0;i<count;i++)Copy(bodies[i],s.bodies[i]);
  }
  internal void Sample(long time){
   Count=0;BeforeFrame=AfterFrame=-1;Mode="unavailable";Snapshot before=null,after=null;
   foreach(var s in history){if(s.frame<0)continue;
    if(s.time<=time&&(before==null||s.time>before.time))before=s;
    if(s.time>=time&&(after==null||s.time<after.time))after=s;}
   // A result interval can exceed 80 ms on CPU or under GPU contention. Do not
   // insert an empty skeleton between two genuine observations. A bounded hold
   // bridges missing completion only; an explicit empty observation still clears.
   if(before==null||time-before.time>300000)return;
   BeforeFrame=before.frame;AfterFrame=after?.frame??-1;
   bool blend=after!=null&&after.time>before.time&&after.time-before.time<=300000;
   Mode=blend?"interpolated":time==before.time?"observed":"held";
   float t=blend?(float)(time-before.time)/(after.time-before.time):0;
   for(int i=0;i<before.count;i++){
    var a=before.bodies[i];HumanVisionBody b=null;
    if(blend){for(int j=0;j<after.count;j++)if(after.bodies[j].StableTrackId==a.StableTrackId){b=after.bodies[j];break;}
     // Never morph one identity into another. Keep the earlier identity until
     // the display reaches the later observation, then change it atomically.
     if(b!=null&&a.RegionIndex!=b.RegionIndex)b=null;}
    var target=Bodies[Count++];Copy(a,target);if(!blend||b==null)continue;
    var box=a.BoundingBoxPixels;var end=b.BoundingBoxPixels;
    target.BoundingBoxPixels=new Rect(Vector2.Lerp(box.position,end.position,t),Vector2.Lerp(box.size,end.size,t));
    for(int j=0;j<17;j++)target.Joints[j]=Blend(a.Joints[j],b.Joints[j],t);
    for(int j=0;j<6;j++)target.HandJoints[j]=Blend(a.HandJoints[j],b.HandJoints[j],t);
    for(int j=0;j<32;j++)target.CanonicalJoints[j]=new HumanVisionCanonicalJoint(
     Blend(a.CanonicalJoints[j].Position,b.CanonicalJoints[j].Position,t),a.CanonicalJoints[j].ObservationTimestampUs,0);
   }
  }
  private static HumanVisionJoint Blend(HumanVisionJoint a,HumanVisionJoint b,float t)=>new HumanVisionJoint(
   Vector2.Lerp(a.Pixel,b.Pixel,t),Vector2.Lerp(a.Normalized,b.Normalized,t),Mathf.Min(a.Confidence,b.Confidence),a.Valid&&b.Valid,true);
  private static void Copy(HumanVisionBody from,HumanVisionBody to){
   to.TrackId=from.TrackId;to.StableTrackId=from.StableTrackId;to.RegionIndex=from.RegionIndex;
   to.ObservationTimestampUs=from.ObservationTimestampUs;to.BoundingBoxPixels=from.BoundingBoxPixels;to.DetectionConfidence=from.DetectionConfidence;
   Array.Copy(from.Joints,to.Joints,17);Array.Copy(from.HandJoints,to.HandJoints,6);Array.Copy(from.CanonicalJoints,to.CanonicalJoints,32);
  }
 }
}
