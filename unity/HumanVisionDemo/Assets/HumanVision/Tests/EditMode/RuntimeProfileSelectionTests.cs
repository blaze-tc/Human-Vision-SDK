using System;
using System.Reflection;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class RuntimeProfileSelectionTests
    {
        private static string Select(SharedRecognitionSettings settings, RuntimePlatform platform)
        {
            var method = typeof(SharedRecognitionSettings).GetMethod("RuntimeProfileFor");
            Assert.That(method, Is.Not.Null, "The formal Demo must share an explicit platform profile selection.");
            return (string)method.Invoke(settings, new object[] { platform });
        }
        [TestCase(RuntimePlatform.WindowsEditor)] [TestCase(RuntimePlatform.WindowsPlayer)]
        public void WindowsDefaultRestoresAcceptedDirectMlWithoutUnconditionalCpu(RuntimePlatform platform)
        {
            var settings = JsonUtility.FromJson<SharedRecognitionSettings>("{\"Version\":1,\"MaxBodies\":4}");
            Assert.That(Select(settings, platform), Is.EqualTo("windows-pc-directml"));
            Assert.That(settings.MaxBodies, Is.EqualTo(4));
        }
        [Test] public void ExplicitCpuChoicePersistsAndAndroidStillUsesItsBakedRuntimeMode()
        {
            var settings = JsonUtility.FromJson<SharedRecognitionSettings>("{\"Version\":1,\"MaxBodies\":8,\"UseWindowsCpu\":true}");
            Assert.That(Select(settings, RuntimePlatform.WindowsEditor), Is.EqualTo("windows-pc-cpu"));
            var restored = JsonUtility.FromJson<SharedRecognitionSettings>(JsonUtility.ToJson(settings));
            Assert.That(Select(restored, RuntimePlatform.WindowsPlayer), Is.EqualTo("windows-pc-cpu"));
            Assert.That(Select(restored, RuntimePlatform.Android), Is.EqualTo("auto"));
            Assert.That(restored.MaxBodies, Is.EqualTo(8));
        }
    }
}
