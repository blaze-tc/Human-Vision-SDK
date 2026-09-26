using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
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

        [Test]
        public void StaticNcnnAuditRejectsMissingProofAndWrongAbi()
        {
            var library = Path.GetTempFileName();
            var manifest = Path.GetTempFileName();
            try
            {
                var elf = new byte[20];
                elf[0] = 0x7f; elf[1] = (byte)'E'; elf[2] = (byte)'L'; elf[3] = (byte)'F';
                elf[4] = 2; elf[5] = 1; elf[18] = 183;
                File.WriteAllBytes(library, elf);
                var hash = BitConverter.ToString(SHA256.Create().ComputeHash(elf)).Replace("-", "").ToLowerInvariant();
                Assert.IsFalse(HumanVisionAndroidBuildSettings.ValidateStaticNcnnAudit(library, manifest));
                File.WriteAllText(manifest, "{\"native_sha256\":\"" + hash +
                    "\",\"abi\":\"x86_64\",\"api_level\":26,\"ncnn_vulkan_symbols_verified\":true}");
                Assert.IsFalse(HumanVisionAndroidBuildSettings.ValidateStaticNcnnAudit(library, manifest));
                File.WriteAllText(manifest, "{\"native_sha256\":\"" + hash +
                    "\",\"abi\":\"arm64-v8a\",\"api_level\":26,\"ncnn_vulkan_symbols_verified\":true}");
                Assert.IsTrue(HumanVisionAndroidBuildSettings.ValidateStaticNcnnAudit(library, manifest));
                elf[18] = 62; File.WriteAllBytes(library, elf);
                Assert.IsFalse(HumanVisionAndroidBuildSettings.ValidateStaticNcnnAudit(library, manifest));
            }
            finally { File.Delete(library); File.Delete(manifest); }
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
