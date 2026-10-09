using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using HumanVision.Demo;
using HumanVision.Input;
using HumanVision.Interop;
using NUnit.Framework;
using UnityEngine;
namespace HumanVision.Tests
{
    public sealed class Runtime040Tests
    {
        [TestCase(FrameTimestampKind.UnityObserved)]
        [TestCase(FrameTimestampKind.LocalDecode)]
        public void CaptureClockTranslationPreservesAgeInsteadOfRelabelingFresh(FrameTimestampKind kind)
        {
            var texture = new Texture2D(1, 1);
            try {
                var frame = new HumanVisionTextureFrame(1, 1, 1, texture, 9000240000L, 123,
                    0, false, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Linear, kind,
                    1, 9000000000L, FrameClockDomain.InputMonotonic, 0);
                // The adapter's first translation includes the source's 240 ms queue,
                // rather than treating the later publication as a new observation.
                long unityTimestamp = InputTimestampMapping.ToUnity(in frame, 9000250000L, 2000000L);
                Assert.That(unityTimestamp, Is.EqualTo(1750000L));
                // CompleteReadback translates the same timestamp after another 400 ms.
                const long nativeNow = 700000000000L;
                long submittedTimestamp = InputTimestampMapping.Map(unityTimestamp, 2400000L, nativeNow);
                Assert.That(nativeNow - submittedTimestamp, Is.EqualTo(650000L));
                Assert.That(frame.SourceTimestampUs, Is.EqualTo(9000000000L));
                Assert.That(frame.PresentationTimestampUs, Is.EqualTo(123));

                var queries = new HumanVisionSkeletonQueries(new HumanVisionSdkConfiguration {
                    MaxBodies = 1, Regions = HumanVisionSdkConfiguration.CreateEqualRegions(1),
                    MaximumResultAgeMilliseconds = 1000
                });
                Assert.True(queries.Observe(null, 0, 1, 1, submittedTimestamp, true));
                Assert.True(queries.HasFreshResult(nativeNow + 349999));
                Assert.False(queries.HasFreshResult(nativeNow + 350001), "Neither translation may revive stale observations.");
            } finally { UnityEngine.Object.DestroyImmediate(texture); }
        }
        [Test] public void UnmappedLocalDecodeClockRetainsPublicationAgeAndOriginalMetadata()
        {
            var texture = new Texture2D(1, 1);
            try {
                var frame = new HumanVisionTextureFrame(1, 1, 1, texture, 990000, 123,
                    0, false, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Linear,
                    FrameTimestampKind.LocalDecode, 1, 700000000000L, FrameClockDomain.SourceLocalMonotonic, 7);
                long unityTimestamp = InputTimestampMapping.ToUnity(in frame, 1000000, 2000000);
                long nativeTimestamp = InputTimestampMapping.Map(unityTimestamp, 2400000, 900000000000L);
                Assert.That(900000000000L - nativeTimestamp, Is.EqualTo(410000L));
                Assert.That(frame.SourceTimestampUs, Is.EqualTo(700000000000L));
                Assert.That(frame.SourceClockId, Is.EqualTo(7));
                Assert.That(frame.PresentationTimestampUs, Is.EqualTo(123));
            } finally { UnityEngine.Object.DestroyImmediate(texture); }
        }
        [Test] public void BatchedCanonicalMeshMapsImageCoordinatesAndPixelThickness()
        {
            var obj = new GameObject("Mesh test", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionSkeletonGraphic));
            try {
                var graphic = obj.GetComponent<HumanVisionSkeletonGraphic>();
                var body = new HumanVisionBody();
                body.CanonicalJoints[0] = new HumanVisionCanonicalJoint(new HumanVisionJoint(Vector2.zero, new Vector2(.25f,.25f),1,true),100,0);
                body.CanonicalJoints[1] = new HumanVisionCanonicalJoint(new HumanVisionJoint(Vector2.zero, new Vector2(.75f,.25f),1,true),100,0);
                using(var mesh = new UnityEngine.UI.VertexHelper()) {
                    for(int i=0;i<8;i++) graphic.AppendBody(mesh,body,new Rect(-100,-50,200,100),true,true,4,8,2,Color.white);
                    Assert.That(mesh.currentVertCount,Is.EqualTo(8*(4+22)),"Eight bodies share one mesh; invalid joints add no vertices");
                    var vertex = new UnityEngine.UIVertex(); mesh.PopulateUIVertex(ref vertex,0);
                    Assert.That(vertex.position.x,Is.EqualTo(-50));Assert.That(vertex.position.y,Is.EqualTo(24));
                    mesh.PopulateUIVertex(ref vertex,1);Assert.That(vertex.position.y,Is.EqualTo(26),"4 screen pixels at canvas scale 2");
                    mesh.PopulateUIVertex(ref vertex,4);Assert.That(vertex.position,Is.EqualTo(new Vector3(-50,25,0)),"Image Y points down");
                }
            } finally { UnityEngine.Object.DestroyImmediate(obj); }
        }
        [Test] public void SemanticLayoutsMatchNativeAbi()
        {
            Assert.That(Marshal.SizeOf<RuntimeConfigNative>(), Is.EqualTo(32));
            Assert.That(Marshal.SizeOf<RuntimeStatsNative>(), Is.EqualTo(80));
            Assert.That(Marshal.SizeOf<CanonicalHeaderNative>(), Is.EqualTo(72));
            Assert.That(Marshal.SizeOf<CanonicalJointNative>(), Is.EqualTo(48));
            Assert.That(Marshal.OffsetOf<CanonicalJointNative>(nameof(CanonicalJointNative.Timestamp)).ToInt32(), Is.EqualTo(32));
            var config = new HumanVisionConfig { RuntimeRoot = "prepared/runtime", Profile = "auto", MaxBodies = 8 };
            Assert.DoesNotThrow(config.Validate); config.MaxBodies = 9; Assert.Throws<ArgumentException>(config.Validate);
        }
        [Test] public void CameraDemoResolvesOnlyExplicitAndroidBenchmarkProfiles()
        {
            Assert.That(typeof(HumanVisionCameraManager).GetField("runtimeProfileOverride"), Is.Not.Null);
            foreach (string profile in new[] { "android-cpu-nohands", "android-xnnpack-nohands", "android-nnapi-nohands" })
                Assert.That(HumanVisionCameraManager.ResolveRuntimeProfile(profile, false), Is.EqualTo(profile));
            Assert.That(HumanVisionCameraManager.ResolveRuntimeProfile("", false), Is.EqualTo("auto"));
            Assert.That(HumanVisionCameraManager.ResolveRuntimeProfile(null, true), Is.EqualTo("cpu"));
            Assert.Throws<ArgumentException>(() => HumanVisionCameraManager.ResolveRuntimeProfile("unknown-provider", false));
        }
        [Test] public void RealRuntimeCopiesCanonicalAndLegacyViewsThroughPInvoke()
        {
            string marker = Path.GetFullPath(Path.Combine(Application.dataPath, "../runtime-root.txt"));
            Assert.That(File.Exists(marker), Is.True, "Run using tools/test/run_unity040_tests.ps1");
            string root = File.ReadAllText(marker).Trim();
            using (var session = new HumanVisionRuntimeSession(new HumanVisionConfig { RuntimeRoot=root, Profile="cpu",MaxBodies=1 })) {
                byte[] pixels = File.ReadAllBytes(Path.Combine(root,"tests/testdata/d0_1_human_pose.bgr"));
                var pin = GCHandle.Alloc(pixels,GCHandleType.Pinned);
                try { session.SubmitFrame(pin.AddrOfPinnedObject(),218,346,218*3,HumanVisionPixelFormat.Bgr24,7,1000000,pixels.Length); }
                finally { pin.Free(); }
                var end = DateTime.UtcNow.AddSeconds(20);
                while(session.ResultSequence==0 && DateTime.UtcNow<end) { session.PollLatestResult(); Thread.Sleep(10); }
                Assert.That(session.BodyCount,Is.EqualTo(1));Assert.That(session.SourceFrameId,Is.EqualTo(7));
                var body=session.Bodies[0];var nose=body.CanonicalJoints[(int)HumanVisionCanonicalJointId.Nose];
                Assert.That(nose.Position.Valid,Is.True);Assert.That(nose.ObservationTimestampUs,Is.EqualTo(1000000));
                Assert.That(body.Joints[0].Pixel,Is.EqualTo(nose.Position.Pixel));
                Assert.That(session.Diagnostics,Does.Contain("CPU"));
                session.SetRegions(new[]{new Rect(0,0,.5f,1)},1);session.PollLatestResult();Assert.That(session.BodyCount,Is.Zero);
            }
        }
    }
}
