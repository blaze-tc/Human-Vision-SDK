// Console contract for the actual sample and SDK query/config/body code.
// SDK I/O is controlled; this is not model or hardware acceptance.
using System;
using System.Collections.Generic;
using System.Reflection;
using HumanVision;
using HumanVision.Input;
using HumanVisionAcceptance;
using UnityEngine;
namespace LiveSkeletonContract
{
    internal static class Clock { internal static long NowUs=1000000;internal static double realtimeSinceStartupAsDouble=10;internal static void Advance(long us){NowUs+=us;realtimeSinceStartupAsDouble+=us/1000000d;} }
    internal static class Program
    {
        private static readonly List<string> Failures=new List<string>();private static int tests;
        private static void Equal<T>(T actual,T expected,string reason){if(!Equals(actual,expected))throw new Exception(reason+": expected "+expected+", got "+actual);}
        private static void Run(string name,Action test){++tests;Clock.NowUs=1000000;Clock.realtimeSinceStartupAsDouble=10;try{test();Console.WriteLine("PASS "+name);}catch(Exception e){Failures.Add(name);Console.WriteLine("FAIL "+name+": "+(e.InnerException??e).Message);}}
        private static HumanVisionSdkConfiguration Settings(float confidence=.35f,float handAge=100,float bodyAge=1000)=>new HumanVisionSdkConfiguration{MinimumJointConfidence=confidence,MaximumHandAgeMilliseconds=handAge,MaximumResultAgeMilliseconds=bodyAge};
        private static void Point(HumanVisionBody body,int joint=0,float confidence=.8f,long timestamp=1000000,float x=.7f,float pixel=70){body.CanonicalJoints[joint]=new HumanVisionCanonicalJoint(new HumanVisionJoint(new Vector2(pixel,50),new Vector2(x,.5f),confidence,true),timestamp,20);}
        private static void Apply(RuntimePrefabLiveSkeleton live,HumanVisionSdk sdk,HumanVisionSdkConfiguration value){typeof(RuntimePrefabLiveSkeleton).GetMethod("Configure",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)?.Invoke(live,new object[]{value});sdk.Settings=value;}
        private static void Sample(RuntimePrefabLiveSkeleton live,HumanVisionSdk sdk){var method=typeof(RuntimePrefabLiveSkeleton).GetMethod("Sample",BindingFlags.Instance|BindingFlags.NonPublic);var args=method.GetParameters().Length==1?new object[]{sdk}:new object[]{sdk,new HumanVisionTextureFrame(99,44,512,288)};method.Invoke(live,args);}
        public static int Main()
        {
            Run("DrawsSdkCoordinatesWithoutPreviewOrAnalysisTexture",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);Equal(live.VisibleJoints,1,"ordinary Update only needs a valid SDK joint");Equal(sdk.ProgressReads,0,"native progress is not a display admission gate");Equal(sdk.AnalysisFrameReads,0,"a Unity analysis texture is not required");});
            Run("RepeatedUpdateReadsSameValidObservation",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);for(int i=0;i<20;i++)Sample(live,sdk);Equal(live.VisibleJoints,1,"same result sequence remains usable");Equal(sdk.ResultSequence,12L,"display must not invent new observations");});
            Run("SdkBodyAgeReplacesArbitrary180MsDisplayHold",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);Clock.Advance(300000);Sample(live,sdk);Equal(live.VisibleJoints,1,"valid 1s SDK cache must survive 180ms");});
            Run("MinimumConfidenceStillUsesSdkQuery",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body,confidence:.4f);Apply(live,sdk,Settings(.6f));Sample(live,sdk);Equal(live.VisibleJoints,0,"SDK confidence threshold applies");Apply(live,sdk,Settings(.2f));Sample(live,sdk);Equal(live.VisibleJoints,1,"applied threshold can restore points");});
            Run("HandKeepsItsOwnObservationAge",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body,7);Point(sdk.Body,8,timestamp:950000);Apply(live,sdk,Settings(handAge:100));Sample(live,sdk);Equal(live.VisibleJoints,2,"initial wrist and hand");Clock.Advance(60000);Sample(live,sdk);Equal(live.VisibleJoints,1,"old hand must expire while wrist remains valid");Equal(live.VisibleBones,0,"expired endpoint removes its bone");});
            Run("BodyJointKeepsItsOwnObservationAge",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body,timestamp:700000);Apply(live,sdk,Settings(bodyAge:200));Sample(live,sdk);Equal(live.VisibleJoints,0,"fresh body cannot refresh an old joint");});
            Run("NonFiniteConfidenceUsesSdkRejection",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body,confidence:float.NaN);Sample(live,sdk);Equal(live.VisibleJoints,0,"invalid confidence cannot be drawn");});
            Run("NonFinitePixelUsesSdkRejection",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body,pixel:float.NaN);Sample(live,sdk);Equal(live.VisibleJoints,0,"invalid pixel cannot bypass SDK filtering");});
            Run("ExpiredResultClearsPreviousGeometry",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);Clock.Advance(1001000);Sample(live,sdk);Equal(live.VisibleJoints,0,"over-age cached body clears in the next Update");});
            Run("StoppedSdkClearsPreviousGeometry",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);sdk.InputState=InputSourceState.Stopped;Sample(live,sdk);Equal(live.VisibleJoints,0,"SDK stop invalidates all current queries");});
            Run("SourceResetClearsPreviousGeometry",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);sdk.SourceCleared=true;Sample(live,sdk);Equal(live.VisibleJoints,0,"SDK source reset cannot prolong old points");});
            Run("NullSdkClearsPreviousGeometry",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);Sample(live,null);Equal(live.VisibleJoints,0,"destroyed SDK clears display");});
            Run("AllCapacitySlotsAreQueriedDespiteEmptyFirstSlots",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();sdk.Settings=new HumanVisionSdkConfiguration{MaxBodies=8,UseRegions=false};sdk.Bodies[0]=null;sdk.Bodies[7]=new HumanVisionBody{StableTrackId=9,ObservationTimestampUs=1000000};Point(sdk.Bodies[7]);Sample(live,sdk);Equal(live.VisibleBodies,1,"active slot seven must not depend on a dense body count");});
            Run("UsesNormalizedSdkCoordinatesWithoutAnotherTransform",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body,x:.73f);Sample(live,sdk);var points=(Vector2[,])typeof(RuntimePrefabLiveSkeleton).GetField("points",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(live);Equal(points[0,0].x,.73f,"query coordinates must not be mirrored or scaled twice");});
            Run("RejectedCoordinatesDoNotLeaveAnOldPoint",()=>{var live=new RuntimePrefabLiveSkeleton();var sdk=new HumanVisionSdk();Point(sdk.Body);Sample(live,sdk);sdk.HasFreshResult=false;Sample(live,sdk);Equal(live.VisibleJoints,0,"a failed SDK query is not a separate display hold");});
            Console.WriteLine("RESULT total="+tests+" passed="+(tests-Failures.Count)+" failed="+Failures.Count);return Failures.Count==0?0:1;
        }
    }
}
namespace HumanVision.Input
{
    public enum InputSourceState{Stopped,Streaming}public enum FrameRowOrigin{UnityBottomLeft}
    public readonly struct HumanVisionTextureFrame {public readonly ulong SourceId,Generation;public readonly int Width,Height,AppliedRotationDegrees;public readonly bool AppliedMirrorX;public readonly FrameRowOrigin RowOrigin;public HumanVisionTextureFrame(ulong source,ulong generation,int width,int height){SourceId=source;Generation=generation;Width=width;Height=height;AppliedRotationDegrees=0;AppliedMirrorX=false;RowOrigin=FrameRowOrigin.UnityBottomLeft;}}
}
namespace HumanVision
{
    public sealed class HumanVisionSdkOptions{public HumanVisionSdkConfiguration Recognition;}
    public sealed class HumanVisionSdk
    {
        private HumanVisionSdkConfiguration settings=new HumanVisionSdkConfiguration();private HumanVisionSkeletonQueries queries;
        public readonly HumanVisionBody[] Bodies=new HumanVisionBody[8];public HumanVisionBody Body{get=>Bodies[0];set=>Bodies[0]=value;}
        public bool HasFreshResult=true,SourceCleared;public int ProgressReads,AnalysisFrameReads;public InputSourceState InputState=InputSourceState.Streaming;
        public HumanVisionSdk(){Body=new HumanVisionBody{StableTrackId=7,RegionIndex=0,ObservationTimestampUs=1000000};}
        public HumanVisionSdkConfiguration Settings{get=>settings;set{settings=value;queries=null;}}
        public HumanVisionSdkOptions Configuration=>new HumanVisionSdkOptions{Recognition=Settings.Clone()};public long ResultSequence=>12;public long SourceFrameId=>22;public long SourceTimestampUs=>1000000;
        public bool RegionsEnabled=>Settings.UseRegions;public double ResultAgeMilliseconds=>(LiveSkeletonContract.Clock.NowUs-SourceTimestampUs)/1000d;public int GetMaxBodies()=>Settings.MaxBodies;public int GetUsersCount()=>1;
        public bool TryGetJointNormalizedPosition(int slot,HumanVisionCanonicalJointId joint,out Vector2 value){value=default;if(!HasFreshResult||SourceCleared||InputState!=InputSourceState.Streaming)return false;if(queries==null){queries=new HumanVisionSkeletonQueries(Settings);queries.Observe(Bodies,Settings.MaxBodies,ResultSequence,SourceFrameId,SourceTimestampUs,true);}if(!queries.TryGetJoint(slot,joint,LiveSkeletonContract.Clock.NowUs,out var point))return false;value=point.Position.Normalized;return true;}
        public bool TryGetSampledBodyByIndex(int slot,out HumanVisionBody value){value=Bodies[slot];return value!=null;}
        public bool TryGetRuntimeProgress(out HumanVisionRuntimeProgress value){++ProgressReads;value=default;return false;}
        public bool TryGetAnalysisInputFrame(long after,out HumanVisionTextureFrame value){++AnalysisFrameReads;value=default;return false;}
    }
    public struct HumanVisionPresentedFrame{}
    public sealed class HumanVisionRuntimeView {public HumanVisionSdk Sdk=new HumanVisionSdk();public bool TryGetFrame(out HumanVisionPresentedFrame value){value=default;return false;}public bool TryGetBodyId(int index,out long id){id=0;return false;}public bool TryGetJointPosition(int index,HumanVisionCanonicalJointId joint,out Vector3 value){value=default;return false;}}
}
namespace HumanVisionAcceptance{internal static class RuntimePrefabRedrawRig{internal static Color GetSlotColor(int slot)=>Color.white;}}
