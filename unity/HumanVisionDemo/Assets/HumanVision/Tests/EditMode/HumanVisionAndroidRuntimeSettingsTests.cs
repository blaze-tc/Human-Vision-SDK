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
            CollectionAssert.AreEqual(
                new[]
                {
                    "body_pose", "multi_person", "gpu_input", "vulkan", "fp16-storage",
                    "fp16-arithmetic", "android-hardware-buffer", "external-sync-fd"
                },
                HumanVisionAndroidRuntimeModeRegistry.Resolve("android-ncnn-vulkan").RequiredCapabilities);
            foreach (var id in new[] { "android-ort-xnnpack", "android-ort-cpu" })
                CollectionAssert.AreEqual(
                    new[] { "body_pose", "multi_person", "tensor_inference" },
                    HumanVisionAndroidRuntimeModeRegistry.Resolve(id).RequiredCapabilities,
                    id);
            Assert.AreEqual(
                "android-ncnn-vulkan",
                HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId);
            Assert.Throws<InvalidOperationException>(
                () => HumanVisionAndroidRuntimeModeRegistry.Resolve("automatic"));
        }
    }
}
