// Console-only unit contract. The real Demo, SDK query/config/body code and
// Unity math assemblies are compiled together; only SDK I/O and Time are stubs.
// These controlled observations are not hardware or model acceptance evidence.
using System;
using System.Collections.Generic;
using System.Reflection;
using HumanVision;
using HumanVision.Input;
using HumanVisionAcceptance;
using UnityEngine;

namespace LiveSkeletonContract
{
    internal static class Clock
    {
        internal static long NowUs = 1000000;
        internal static double realtimeSinceStartupAsDouble = 10;
        internal static void Advance(long us) { NowUs += us; realtimeSinceStartupAsDouble += us / 1000000d; }
    }
    internal static class Program
    {
        private static readonly List<string> Failures = new List<string>();
        private static readonly MethodInfo Configure = typeof(RuntimePrefabLiveSkeleton).GetMethod("Configure", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        private static int tests;
        private static void Equal<T>(T actual, T expected, string detail) { if (!Equals(actual, expected)) throw new Exception(detail + ": expected " + expected + ", got " + actual); }
        private static void Run(string name, Action test)
        {
            ++tests; Clock.NowUs = 1000000; Clock.realtimeSinceStartupAsDouble = 10;
            try { test(); Console.WriteLine("PASS " + name); }
            catch (Exception error) { Failures.Add(name); Console.WriteLine("FAIL " + name + ": " + (error.InnerException ?? error).Message); }
        }
        private static HumanVisionSdkConfiguration Settings(float confidence = .35f, float handAge = 100, float bodyAge = 1000) =>
            new HumanVisionSdkConfiguration { MinimumJointConfidence = confidence, MaximumHandAgeMilliseconds = handAge, MaximumResultAgeMilliseconds = bodyAge };
        private static HumanVisionBody Body() => new HumanVisionBody { StableTrackId = 7, RegionIndex = 0, ObservationTimestampUs = 1000000 };
        private static void Point(HumanVisionBody body, int id, float confidence = .8f, long timestamp = 1000000, float x = .7f, float pixel = 70)
        { body.CanonicalJoints[id] = new HumanVisionCanonicalJoint(new HumanVisionJoint(new Vector2(pixel, 50), new Vector2(x, .5f), confidence, true), timestamp, 20); }
        private static void Apply(RuntimePrefabLiveSkeleton live, HumanVisionSdk sdk, HumanVisionSdkConfiguration config)
        {
            // Before the fix, absent Configure deliberately leaves the actual
            // Sample behavior under test intact, so RED is behavioral, not a
            // compiler failure or a fake replacement of the filter.
            Configure?.Invoke(live, new object[] { config }); sdk.Settings = config.Clone();
        }
        private static HumanVisionTextureFrame Preview => new HumanVisionTextureFrame(5, 1, 512, 288);
        private static void Sample(RuntimePrefabLiveSkeleton live, HumanVisionSdk sdk) { var preview = Preview; live.Sample(sdk, in preview); }
        public static int Main()
        {
            Run("MinimumConfidenceFiltersActualSampledJoint", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0, .4f);
                Apply(live, sdk, Settings(.6f)); Sample(live, sdk); Equal(live.VisibleJoints, 0, "low-confidence sampled point must be hidden");
            });
            Run("HandUsesItsOwnObservationAge", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 7); Point(sdk.Body, 8, timestamp: 950000);
                Clock.Advance(100000); Apply(live, sdk, Settings(handAge: 100)); Sample(live, sdk);
                Equal(live.VisibleJoints, 1, "fresh wrist must remain while the independently old hand expires");
            });
            Run("BodyJointUsesItsOwnTimestamp", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0, timestamp: 700000);
                Clock.Advance(100000); Apply(live, sdk, Settings(bodyAge: 200)); Sample(live, sdk);
                Equal(live.VisibleJoints, 0, "fresh body must not refresh an older joint timestamp");
            });
            Run("NonFiniteConfidenceIsRejected", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0, float.NaN);
                Apply(live, sdk, Settings()); Sample(live, sdk); Equal(live.VisibleJoints, 0, "NaN confidence must not bypass the configuration");
            });
            Run("NonFinitePixelIsRejected", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0, pixel: float.NaN);
                Apply(live, sdk, Settings()); Sample(live, sdk); Equal(live.VisibleJoints, 0, "SDK finite-coordinate contract must apply");
            });
            Run("SuccessfulConfigurationChangeClearsCachedGeometryImmediately", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0);
                Apply(live, sdk, Settings(.2f)); Sample(live, sdk); Equal(live.VisibleJoints, 1, "initial eligible point");
                Apply(live, sdk, Settings(.9f)); Equal(live.VisibleJoints, 0, "new threshold cannot leave cached points visible");
            });
            Run("HoldPrunesExpiredHandButKeepsUnexpiredWrist", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 7); Point(sdk.Body, 8, timestamp: 950000);
                Apply(live, sdk, Settings(handAge: 100)); Sample(live, sdk); Equal(live.VisibleJoints, 2, "initial wrist and hand");
                sdk.IdentityAvailable = false; Clock.Advance(60000); Sample(live, sdk);
                Equal(live.VisibleJoints, 1, "180ms display hold cannot extend the hand's 100ms limit");
                Equal(live.VisibleBones, 0, "bone endpoint expiry must update visible bone count");
            });
            Run("IdentityGapPreservesValidShortHold", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0);
                Apply(live, sdk, Settings()); Sample(live, sdk); sdk.IdentityAvailable = false; Clock.Advance(20000); Sample(live, sdk);
                Equal(live.VisibleJoints, 1, "valid short display hold must remain available");
            });
            Run("IdentityGapStillExpiresAtDisplayDeadline", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0);
                Apply(live, sdk, Settings()); Sample(live, sdk); sdk.IdentityAvailable = false; Clock.Advance(181000); Sample(live, sdk);
                Equal(live.VisibleJoints, 0, "existing 180ms display hold deadline");
            });
            Run("NoFreshResultClearsWithoutUsingInfinityAsClock", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0);
                Apply(live, sdk, Settings()); Sample(live, sdk); sdk.HasFreshResult = false; Clock.Advance(20000); Sample(live, sdk);
                Equal(live.VisibleJoints, 0, "expired SDK result cannot retain the last points");
                Equal(sdk.NonFreshAgeReads, 0, "Infinity is not a valid native clock");
            });
            Run("FilteredGeometryRetainsSampledPosition", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0, x: .73f);
                Apply(live, sdk, Settings()); Sample(live, sdk);
                var points = (Vector2[,])typeof(RuntimePrefabLiveSkeleton).GetField("points", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(live);
                Equal(points[0, 0].x, .73f, "filtering must not replace the smooth sampled position with a raw coordinate");
            });
            Run("NewerNativeResultStillConfirmsTheSameSourceBinding", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0);
                sdk.ProgressFrameId = 29; Apply(live, sdk, Settings()); Sample(live, sdk);
                Equal(live.VisibleJoints, 1, "independent publication may advance after the manager snapshot without changing the source binding");
            });
            Run("FallbackConfigurationIsReadOnce", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0);
                for (int i = 0; i < 20; ++i) Sample(live, sdk);
                Equal(sdk.ConfigurationReads, 1, "legacy caller fallback must clone configuration only once");
                Equal(sdk.ResultSequence, 12L, "display filtering must not manufacture native observations");
            });
            Run("SameConfigurationPreservesValidGeometryDuringGap", () => {
                var live = new RuntimePrefabLiveSkeleton(); var sdk = new HumanVisionSdk(); Point(sdk.Body, 0); var config = Settings();
                Apply(live, sdk, config); Sample(live, sdk); sdk.IdentityAvailable = false;
                for (int i = 0; i < 10; ++i) Apply(live, sdk, config);
                Clock.Advance(20000); Sample(live, sdk); Equal(live.VisibleJoints, 1, "per-frame Configure of unchanged settings must not clear geometry");
            });
            Console.WriteLine("RESULT total=" + tests + " passed=" + (tests - Failures.Count) + " failed=" + Failures.Count);
            return Failures.Count == 0 ? 0 : 1;
        }
    }
}

namespace HumanVision.Input
{
    public enum InputSourceState { Stopped, Streaming }
    public enum FrameRowOrigin { UnityBottomLeft }
    public readonly struct HumanVisionTextureFrame
    {
        public readonly ulong SourceId, Generation;
        public readonly int Width, Height, AppliedRotationDegrees;
        public readonly bool AppliedMirrorX;
        public readonly FrameRowOrigin RowOrigin;
        public HumanVisionTextureFrame(ulong source, ulong generation, int width, int height)
        { SourceId = source; Generation = generation; Width = width; Height = height; AppliedRotationDegrees = 0; AppliedMirrorX = false; RowOrigin = FrameRowOrigin.UnityBottomLeft; }
    }
}
namespace HumanVision
{
    public sealed class HumanVisionSdkOptions { public HumanVisionSdkConfiguration Recognition; }
    public sealed class HumanVisionSdk
    {
        public HumanVisionSdkConfiguration Settings = new HumanVisionSdkConfiguration();
        public HumanVisionBody Body = new HumanVisionBody { StableTrackId = 7, RegionIndex = 0, ObservationTimestampUs = 1000000 };
        public bool IdentityAvailable = true, HasFreshResult = true;
        public long ProgressFrameId = 22;
        public int ConfigurationReads, NonFreshAgeReads;
        public InputSourceState InputState => InputSourceState.Streaming;
        public HumanVisionSdkOptions Configuration { get { ++ConfigurationReads; return new HumanVisionSdkOptions { Recognition = Settings.Clone() }; } }
        public long ResultSequence => 12;
        public long SourceFrameId => 22;
        public long SourceTimestampUs => 1000000;
        public double ResultAgeMilliseconds { get { if (!HasFreshResult) { ++NonFreshAgeReads; return double.PositiveInfinity; } return (LiveSkeletonContract.Clock.NowUs - SourceTimestampUs) / 1000d; } }
        public bool RegionsEnabled => Settings.UseRegions;
        public int GetMaxBodies() => Settings.MaxBodies;
        public int GetUsersCount() => Body == null ? 0 : 1;
        public bool TryGetSampledBodyByIndex(int slot, out HumanVisionBody body) { body = slot == 0 ? Body : null; return body != null; }
        public bool TryGetRuntimeProgress(out HumanVisionRuntimeProgress progress)
        {
            progress = new HumanVisionRuntimeProgress { SourceId = 5, Generation = 1, ConfigEpoch = 1, FrameId = ProgressFrameId, NativeFreshFrames = 12, ExecutionMode = "native", Streaming = true };
            return IdentityAvailable;
        }
        public bool TryGetAnalysisInputFrame(long after, out HumanVisionTextureFrame frame) { frame = default; return false; }
    }
    public struct HumanVisionPresentedFrame { }
    public sealed class HumanVisionRuntimeView
    {
        public HumanVisionSdk Sdk = new HumanVisionSdk();
        public bool TryGetFrame(out HumanVisionPresentedFrame frame) { frame = default; return false; }
        public bool TryGetBodyId(int index, out long id) { id = 0; return false; }
        public bool TryGetJointPosition(int index, HumanVisionCanonicalJointId joint, out Vector3 point) { point = default; return false; }
    }
}
namespace HumanVisionAcceptance
{
    internal static class RuntimePrefabRedrawRig { internal static Color GetSlotColor(int slot) => Color.white; }
}
