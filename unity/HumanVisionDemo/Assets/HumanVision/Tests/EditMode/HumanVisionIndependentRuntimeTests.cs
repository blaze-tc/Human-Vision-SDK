using System;
using System.Reflection;
using System.Runtime.InteropServices;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionIndependentRuntimeTests
    {
        private static FieldInfo PolicyField()
        {
            var field = typeof(HumanVisionSdkOptions).GetField("ExecutionPolicy");
            Assert.That(field, Is.Not.Null, "The SDK does not expose its execution policy yet.");
            return field;
        }
        private static void Policy(HumanVisionSdkOptions options, string value)
        {
            var field = PolicyField(); field.SetValue(options, Enum.Parse(field.FieldType, value));
        }
        [Test] public void CompatibleInputRemainsTheDefault()
        {
            var options = new HumanVisionSdkOptions();
            Assert.That(PolicyField().GetValue(options).ToString(), Is.EqualTo("CompatibleUnityInput"));
        }
        [TestCase(InputKind.Video)]
        [TestCase(InputKind.WebCamera)]
        public void IndependentRequiredRejectsUnsupportedDriverBeforeStarting(InputKind kind)
        {
            var options = new HumanVisionSdkOptions { SourceKind = kind, OpenInputOnInitialize = true };
            options.Input.VideoPath = "existing-Unity-owned-video.mp4";
            Policy(options, "IndependentRequired");
            var error = Assert.Throws<ArgumentException>(options.Validate);
            Assert.That(error.Message, Does.Contain("RTSP"));
        }
        [Test] public void IndependentRtspSettingsSurviveCloningWithoutMutatingTheCaller()
        {
            var options = new HumanVisionSdkOptions { SourceKind = InputKind.Rtsp };
            options.Input.RtspUrl = "rtsp://127.0.0.1:18569/Exercise-11.mp4";
            Policy(options, "IndependentRequired");
            var copy = options.Clone();
            Assert.That(PolicyField().GetValue(copy).ToString(), Is.EqualTo("IndependentRequired"));
            copy.Input.RtspUrl = "rtsp://127.0.0.1:18569/changed";
            Assert.That(options.Input.RtspUrl, Does.EndWith("Exercise-11.mp4"));
            Assert.DoesNotThrow(options.Validate);
        }
        [Test] public void IndependentProgressIsQueriedThroughTheController()
        {
            var method = typeof(HumanVisionManager).GetMethod("TryGetRuntimeProgress", BindingFlags.Public | BindingFlags.Instance);
            Assert.That(method, Is.Not.Null, "Native progress must be queryable without a Unity Update.");
            var owner = new GameObject("independent progress without a session");
            try {
                var manager = owner.AddComponent<HumanVisionManager>();
                var args = new object[] { null };
                Assert.False((bool)method.Invoke(manager, args), "An absent native counter must not be reported as a measured zero.");
            } finally { UnityEngine.Object.DestroyImmediate(owner); }
        }
        [TestCase(false, true, 1920, 1080, 0, 512, 288, 0, 0)]
        [TestCase(false, true, 3840, 2160, 0, 960, 544, 0, 0)]
        [TestCase(false, false, 640, 480, 0, 512, 288, 0, 0)]
        [TestCase(true, true, 1920, 1080, 0, 512, 288, 512, 288)]
        [TestCase(true, true, 3840, 2160, 0, 384, 224, 512, 288)]
        [TestCase(true, false, 1920, 1080, 0, 512, 288, 1920, 1080)]
        [TestCase(true, true, 1280, 720, 640, 256, 192, 1280, 720)]
        public void IndependentSourceOriginalDoesNotChangeAndroidAnalysisTransport(bool android, bool modelSized,
            int width, int height, int detectorWidth, int poseWidth, int poseHeight, int expectedWidth, int expectedHeight)
        {
            Assert.That(HumanVisionSdk.IndependentSourceSize(android, modelSized, new Vector2Int(width, height),
                detectorWidth, poseWidth, poseHeight), Is.EqualTo(new Vector2Int(expectedWidth, expectedHeight)));
        }
        [Test] public void SdkRtspColorUsesOriginalSourceWithoutMutatingRequestedAnalysisSettings()
        {
            var options = new HumanVisionSdkOptions { SourceKind = InputKind.Rtsp, AutoSelectRtspStream = true, RtspTcp = false };
            options.Input.RequestedWidth = 640; options.Input.RequestedHeight = 360;
            options.Input.RtspUrl = "rtsp://127.0.0.1:554/camera/substream";
            var source = (RtspSourceSettings)options.ToSourceSettings();
            Assert.That(source.RequestedWidth, Is.Zero); Assert.That(source.RequestedHeight, Is.Zero);
            Assert.That(options.Input.RequestedWidth, Is.EqualTo(640)); Assert.That(options.Input.RequestedHeight, Is.EqualTo(360));
            Assert.That(source.Transport, Is.EqualTo(RtspTransport.Udp));
            var stream = HumanVisionSdk.OriginalRtspStream(options);
            Assert.That(stream.Address.OriginalString, Is.EqualTo(options.Input.RtspUrl), "The explicit source URL is not replaced by a lower-load substream.");
        }
        [TestCase(InputKind.WebCamera)] [TestCase(InputKind.Video)]
        public void NonRtspCaptureRequestsRemainUnchanged(InputKind kind)
        {
            var options = new HumanVisionSdkOptions { SourceKind = kind };
            options.Input.RequestedWidth = 640; options.Input.RequestedHeight = 480;
            var source = options.ToSourceSettings();
            Assert.That(source.RequestedWidth, Is.EqualTo(640)); Assert.That(source.RequestedHeight, Is.EqualTo(480));
        }
        [Test] public void DeferredControlLayoutsMatchTheSeparateNativeHandleProtocol()
        {
            var assembly = typeof(HumanVisionManager).Assembly;
            foreach (var pair in new[] {
                new object[] { "RuntimeControllerConfigNative", 48 },
                new object[] { "RuntimeSourceNative", 80 },
                new object[] { "RuntimeRequestStatusNative", 544 },
                new object[] { "RuntimeCapabilitiesNative", 912 },
                new object[] { "RuntimePreviewNative", 64 },
                new object[] { "RuntimeControlTableNative", 192 },
                new object[] { "RuntimeSourceProgressNative", 120 },
                new object[] { "RuntimeLiveBindingNative", 56 },
                new object[] { "RuntimeLiveTableNative", 104 },
                new object[] { "RuntimeLiveConfigNative", 32 },
                new object[] { "RuntimeLiveFrameNative", 152 }
            }) {
                var type = assembly.GetType("HumanVision.Interop." + pair[0]);
                Assert.That(type, Is.Not.Null, "Missing additive controller binding: " + pair[0]);
                Assert.That(Marshal.SizeOf(type), Is.EqualTo(pair[1]), type.Name);
            }
        }
        [Test] public void SourceProgressStreamingDoesNotDependOnClaimsOrCanonicalResults()
        {
            var value = new HumanVision.Interop.RuntimeSourceProgressNative {
                SourceId = 77, Generation = 3, ConfigEpoch = 9, State = 2, DecodedFrames = 100,
                AcquiredFrames = 99, LivePublishedFrames = 98, AnalysisPublishedFrames = 97, AnalysisClaimedFrames = 0
            }.Managed;
            Assert.That(value.Streaming, Is.True);
            Assert.That(value.SourceId, Is.EqualTo(77)); Assert.That(value.Generation, Is.EqualTo(3));
            Assert.That(value.AnalysisClaimedFrames, Is.Zero); Assert.That(value.LivePublishedFrames, Is.EqualTo(98));
        }
    }
}
