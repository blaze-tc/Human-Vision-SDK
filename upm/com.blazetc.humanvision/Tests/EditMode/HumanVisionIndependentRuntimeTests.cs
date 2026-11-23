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
        [Test] public void DeferredControlLayoutsMatchTheSeparateNativeHandleProtocol()
        {
            var assembly = typeof(HumanVisionManager).Assembly;
            foreach (var pair in new[] {
                new object[] { "RuntimeControllerConfigNative", 48 },
                new object[] { "RuntimeSourceNative", 80 },
                new object[] { "RuntimeRequestStatusNative", 544 },
                new object[] { "RuntimeCapabilitiesNative", 912 },
                new object[] { "RuntimePreviewNative", 64 },
                new object[] { "RuntimeControlTableNative", 168 }
            }) {
                var type = assembly.GetType("HumanVision.Interop." + pair[0]);
                Assert.That(type, Is.Not.Null, "Missing additive controller binding: " + pair[0]);
                Assert.That(Marshal.SizeOf(type), Is.EqualTo(pair[1]), type.Name);
            }
        }
    }
}
