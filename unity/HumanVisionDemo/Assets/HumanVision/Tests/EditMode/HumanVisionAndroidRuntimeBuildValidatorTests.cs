using System.IO;
using System.Linq;
using System.Xml;
using HumanVision;
using HumanVision.Editor;
using NUnit.Framework;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidRuntimeBuildValidatorTests
    {
        [Test]
        public void NcnnVulkanAcceptsACompleteAndroidEnvironment()
        {
            var issues = HumanVisionAndroidRuntimeBuildValidator.Validate(
                HumanVisionAndroidRuntimeModeRegistry.Resolve("android-ncnn-vulkan"),
                CompleteEnvironment());

            Assert.IsEmpty(issues.Where(issue => issue.IsError));
        }

        [TestCase("MinimumApiLevel", 25)]
        [TestCase("Arm64Only", false)]
        [TestCase("Il2Cpp", false)]
        [TestCase("VulkanAvailable", false)]
        [TestCase("VulkanFirst", false)]
        [TestCase("AutomaticGraphicsApis", true)]
        [TestCase("HasHumanVisionLibrary", false)]
        [TestCase("HasNcnnLibrary", false)]
        [TestCase("HasBridgeSymbolManifest", false)]
        [TestCase("HasProfile", false)]
        [TestCase("HasNcnnModelPackAssets", false)]
        [TestCase("HasNcnnModelPackSha256Index", false)]
        public void NcnnVulkanReportsEachRequiredBuildContract(string property, object value)
        {
            var environment = CompleteEnvironment();
            typeof(AndroidBuildEnvironment).GetProperty(property).SetValue(environment, value);

            var issues = HumanVisionAndroidRuntimeBuildValidator.Validate(
                HumanVisionAndroidRuntimeModeRegistry.Resolve("android-ncnn-vulkan"), environment);

            Assert.IsTrue(issues.Any(issue => issue.IsError && issue.Code == property),
                "Expected error code " + property + ". Actual: " + string.Join(", ", issues.Select(issue => issue.Code)));
        }

        [Test]
        public void OrtModesDoNotRequireVulkanOrInheritNcnnRequirements()
        {
            var environment = CompleteEnvironment();
            environment.VulkanAvailable = false;
            environment.VulkanFirst = false;
            environment.AutomaticGraphicsApis = true;
            environment.HasNcnnLibrary = false;
            environment.HasBridgeSymbolManifest = false;
            environment.HasNcnnModelPackAssets = false;
            environment.HasNcnnModelPackSha256Index = false;

            foreach (var mode in new[] { "android-ort-xnnpack", "android-ort-cpu" })
            {
                var issues = HumanVisionAndroidRuntimeBuildValidator.Validate(
                    HumanVisionAndroidRuntimeModeRegistry.Resolve(mode), environment);
                Assert.IsEmpty(issues.Where(issue => issue.IsError), mode + " must remain independently buildable.");
            }
        }

        [Test]
        public void BakedProfileAcceptsEmptyAndMatchingExplicitConfiguration()
        {
            const string baked = "android-ncnn-vulkan";

            Assert.AreEqual(baked, HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(string.Empty, baked));
            Assert.AreEqual(baked, HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", baked));
            Assert.AreEqual(baked, HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(baked, baked));
        }

        [Test]
        public void BakedProfileRejectsAConflictingExplicitConfiguration()
        {
            var exception = Assert.Throws<System.InvalidOperationException>(
                () => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(
                    "android-ort-cpu", "android-ncnn-vulkan"));

            StringAssert.Contains("android-ort-cpu", exception.Message);
            StringAssert.Contains("android-ncnn-vulkan", exception.Message);
        }

        [Test]
        public void GradleManifestContainsTheExactBakedSelectionMetadata()
        {
            var manifestPath = Path.GetTempFileName();
            try
            {
                File.WriteAllText(manifestPath,
                    "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"><application /></manifest>");
                HumanVisionAndroidGradleMetadataWriter.WriteSelectionMetadata(
                    manifestPath, HumanVisionAndroidRuntimeModeRegistry.Resolve("android-ncnn-vulkan"));

                var document = new XmlDocument();
                document.Load(manifestPath);
                var metadata = document.SelectNodes("/manifest/application/meta-data");
                Assert.AreEqual(2, metadata.Count);
                Assert.AreEqual("android-ncnn-vulkan", MetadataValue(metadata, "com.blazetc.humanvision.runtime_mode"));
                Assert.AreEqual("android-ncnn-vulkan", MetadataValue(metadata, "com.blazetc.humanvision.profile_id"));
            }
            finally
            {
                File.Delete(manifestPath);
            }
        }

        private static AndroidBuildEnvironment CompleteEnvironment()
        {
            return new AndroidBuildEnvironment
            {
                MinimumApiLevel = 26,
                Arm64Only = true,
                Il2Cpp = true,
                VulkanAvailable = true,
                VulkanFirst = true,
                AutomaticGraphicsApis = false,
                HasHumanVisionLibrary = true,
                HasNcnnLibrary = true,
                HasBridgeSymbolManifest = true,
                HasProfile = true,
                HasNcnnModelPackAssets = true,
                HasNcnnModelPackSha256Index = true
            };
        }

        private static string MetadataValue(XmlNodeList nodes, string key)
        {
            const string android = "http://schemas.android.com/apk/res/android";
            foreach (XmlElement node in nodes)
                if (node.GetAttribute("name", android) == key)
                    return node.GetAttribute("value", android);
            return null;
        }
    }
}
