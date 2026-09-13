using System;
using System.Linq;
using NUnit.Framework;
using HumanVision.Editor;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidRuntimeSettingsTests
    {
        [Test]
        public void RegistryExposesOnlyApprovedModes()
        {
            CollectionAssert.AreEqual(
                new[] { "android-ncnn-vulkan", "android-ort-xnnpack", "android-ort-cpu" },
                HumanVisionAndroidRuntimeModeRegistry.All.Select(x => x.Id).ToArray());
            foreach (var id in new[] { "android-ncnn-vulkan", "android-ort-xnnpack", "android-ort-cpu" })
                Assert.AreEqual(id, HumanVisionAndroidRuntimeModeRegistry.Resolve(id).ProfileId);
            Assert.AreEqual(
                "android-ncnn-vulkan",
                HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId);
            Assert.Throws<InvalidOperationException>(
                () => HumanVisionAndroidRuntimeModeRegistry.Resolve("automatic"));
        }
    }
}
