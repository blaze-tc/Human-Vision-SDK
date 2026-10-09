using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Xml;
using HumanVision.Demo;
using HumanVision.Editor;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    // This lane consumes the actual private model and saved offline receipts. It never initializes a driver.
    public sealed class HumanVisionNeuralContractTests
    {
        private const string Profile = "android-rknn-npu-quality-low";
        private const string Pack = "yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental";
        private string root, source, profile, manifest, conversion, comparison;
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path; public string sha256; }

        [SetUp]
        public void CopyActualPrivateRuntime()
        {
            source = Environment.GetEnvironmentVariable("HV_TEST_NEURAL_RUNTIME_ROOT");
            if (string.IsNullOrEmpty(source)) {
                source = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
                if (!File.Exists(Path.Combine(source, "profiles", Profile + ".json"))) {
                    string repository = Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT");
                    if (!string.IsNullOrEmpty(repository)) source = Path.Combine(repository, "out", "rknn-dual-20261009", "stage", "Runtime");
                }
            }
            Assert.That(Directory.Exists(source), Is.True, "Stage the real private dual Runtime or set HV_TEST_NEURAL_RUNTIME_ROOT.");
            root = Path.Combine(Path.GetTempPath(), "HumanVision-neural-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(root);
            string profileRelative = "profiles/" + Profile + ".json", prefix = "modelpacks/" + Pack + "/";
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(source, "index.json")));
            index.files = index.files.Where(row => row.path == profileRelative || row.path.StartsWith(prefix, StringComparison.Ordinal)).ToArray();
            foreach (var row in index.files) {
                string file = Path.Combine(source, row.path);
                string target = Path.Combine(root, file.Substring(source.Length + 1));
                Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(file, target);
            }
            File.WriteAllText(Path.Combine(root, "index.json"), JsonUtility.ToJson(index));
            profile = Path.Combine(root, "profiles", Profile + ".json");
            string pack = Path.Combine(root, "modelpacks", Pack);
            manifest = Path.Combine(pack, "manifest.json");
            conversion = Path.Combine(pack, "conversion-receipt.json");
            comparison = Path.Combine(pack, "simulator-comparison.json");
        }
        [TearDown] public void Cleanup() { if (Directory.Exists(root)) Directory.Delete(root, true); }

        [Test]
        public void ActualPinnedAssetsExposeOnlyLowAndTheirRealAnalysisSize()
        {
            Assert.DoesNotThrow(() => HumanVisionNeuralModelContract.Validate(root));
            var choices = HumanVisionSdkQualityCapabilities.Load(root, Profile);
            Assert.That(choices.Error, Is.Empty);
            Assert.That(choices.Selectable, Is.False);
            Assert.That(choices.Choices.Select(value => value.Quality), Is.EqualTo(new[] { ModelInputQuality.Low }));
            var contract = new SharedRecognitionSettings { InputQuality = ModelInputQuality.Low, AccelerationMode = HumanVisionAccelerationMode.Neural }.ResolveContract(root, Profile);
            Assert.That(contract.ProfileId, Is.EqualTo(Profile));
            Assert.That(contract.ModelPackId, Is.EqualTo(Pack));
            Assert.That(new Vector2Int(contract.PoseWidth, contract.PoseHeight), Is.EqualTo(new Vector2Int(512, 288)));
        }

        [TestCase("profile-schema")]
        [TestCase("profile-execution")]
        [TestCase("profile-experimental")]
        [TestCase("profile-capabilities")]
        [TestCase("pack-profile")]
        [TestCase("pack-capacity")]
        [TestCase("pack-capabilities")]
        [TestCase("output-columns")]
        [TestCase("qualification-bank")]
        [TestCase("hardware-claim")]
        [TestCase("conversion-int8")]
        [TestCase("conversion-input-shape")]
        [TestCase("simulator-failed")]
        [TestCase("simulator-controls")]
        [TestCase("simulator-loosened-limits")]
        [TestCase("simulator-stripped-measurements")]
        public void RehashedWrongContractOrReceiptCannotAdvertiseNeuralQuality(string mutation)
        {
            switch (mutation) {
                case "profile-schema": Replace(profile, "\"schema_version\": 1", "\"schema_version\": 2"); break;
                case "profile-execution": Replace(profile, "raw_tensor_rknn_nonquantized_v1", "raw_tensor_fp32_v1"); break;
                case "profile-experimental": Replace(profile, "\"experimental\": true", "\"experimental\": false"); break;
                case "profile-capabilities": Replace(profile, "\"tensor_inference\"", "\"hand_pose\""); break;
                case "pack-profile": Replace(manifest, "\"profile_id\": \"" + Profile + "\"", "\"profile_id\": \"android-ncnn-vulkan\""); break;
                case "pack-capacity": Replace(manifest, "\"max_people\": 8", "\"max_people\": 4"); break;
                case "pack-capabilities": Replace(manifest, "\"multi_person\"", "\"hand_pose\""); break;
                case "output-columns": Replace(manifest, "65,", "64,"); break;
                case "qualification-bank": Replace(manifest, "5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34", new string('0', 64)); break;
                case "hardware-claim": Replace(manifest, "\"device_performance_verified\": false", "\"device_performance_verified\": true"); break;
                case "conversion-int8": Replace(conversion, "\"precision_request\": \"non-quantized\"", "\"precision_request\": \"int8\""); break;
                case "conversion-input-shape": Replace(conversion, "288,", "224,"); break;
                case "simulator-failed": Replace(comparison, "\"offline_numerical_passed\": true", "\"offline_numerical_passed\": false"); break;
                case "simulator-controls": Replace(comparison, "\"expected_people\": 7", "\"expected_people\": 6"); break;
                case "simulator-loosened-limits": Replace(comparison, "\"joint_xy\": 3.0", "\"joint_xy\": 30.0"); break;
                case "simulator-stripped-measurements": Replace(comparison, "\"raw\": {", "\"removed_raw\": {"); break;
            }
            // Rebind checksums deliberately: a self-consistent SHA index is not semantic admission.
            Rebind("profile_sha256", Hash(profile));
            foreach (var row in new[] { new { Path = conversion, Field = "conversion_receipt_sha256" }, new { Path = comparison, Field = "simulator_comparison_sha256" } }) {
                string before = Regex.Match(File.ReadAllText(manifest), "\"" + row.Field + "\": \"([a-f0-9]{64})\"").Groups[1].Value;
                Replace(manifest, before, Hash(row.Path));
            }
            Reindex();
            Assert.Throws<InvalidDataException>(() => HumanVisionNeuralModelContract.Validate(root));
            var choices = HumanVisionSdkQualityCapabilities.Load(root, Profile);
            Assert.That(choices.Choices, Is.Empty); Assert.That(choices.Error, Is.Not.Empty);
        }

        [TestCase("conversion-receipt.json")]
        [TestCase("simulator-comparison.json")]
        public void MissingDeclaredEvidencePreventsNeuralAdmission(string missing)
        {
            File.Delete(Path.Combine(Path.GetDirectoryName(manifest), missing));
            Assert.Throws<InvalidDataException>(() => HumanVisionNeuralModelContract.Validate(root));
        }

        [Test]
        public void DualBuildMetadataBakesExactlyFiveProfilesAndKeepsGraphicsDefault()
        {
            string androidManifest = Path.Combine(Path.GetTempPath(), "HumanVision-manifest-" + Guid.NewGuid().ToString("N") + ".xml");
            try {
                // Only metadata baking needs the full existing Vulkan closure.
                foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories).Where(value => !value.EndsWith(".meta", StringComparison.Ordinal))) {
                    string target = Path.Combine(root, file.Substring(source.Length + 1));
                    Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(file, target, true);
                }
                File.WriteAllText(androidManifest, "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"><application /></manifest>");
                HumanVisionAndroidGradleMetadataWriter.WriteSelectionMetadata(androidManifest, HumanVisionAndroidRuntimeModeRegistry.Resolve("android-dual-vulkan-npu"), root);
                var document = new XmlDocument(); document.Load(androidManifest);
                var values = document.SelectNodes("//meta-data").Cast<XmlElement>().ToDictionary(value => value.GetAttribute("name", "http://schemas.android.com/apk/res/android"), value => value.GetAttribute("value", "http://schemas.android.com/apk/res/android"));
                Assert.That(values[HumanVisionAndroidRuntimeSelection.RuntimeModeMetadataKey], Is.EqualTo("android-dual-vulkan-npu"));
                Assert.That(values[HumanVisionAndroidRuntimeSelection.ProfileIdMetadataKey], Is.EqualTo("android-ncnn-vulkan"));
                CollectionAssert.AreEqual(new[] { "android-ncnn-vulkan-quality-low", "android-ncnn-vulkan", "android-ncnn-vulkan-quality-high", Profile, "android-cpu-nohands" }, HumanVisionAndroidRuntimeSelection.ParseBakedQualityProfiles(values[HumanVisionAndroidRuntimeSelection.QualityProfilesMetadataKey]));
            } finally { File.Delete(androidManifest); }
        }

        private void Rebind(string field, string hash)
        {
            string text = File.ReadAllText(manifest);
            File.WriteAllText(manifest, Regex.Replace(text, "(\"" + field + "\": \")[a-f0-9]{64}(\")", "${1}" + hash + "${2}"));
        }
        private void Reindex()
        {
            string path = Path.Combine(root, "index.json"); var index = JsonUtility.FromJson<Index>(File.ReadAllText(path));
            foreach (var entry in index.files) entry.sha256 = Hash(Path.Combine(root, entry.path));
            File.WriteAllText(path, JsonUtility.ToJson(index));
        }
        private static void Replace(string path, string before, string after)
        {
            string text = File.ReadAllText(path); Assert.That(text, Does.Contain(before)); File.WriteAllText(path, text.Replace(before, after));
        }
        private static string Hash(string path)
        {
            using (var file = File.OpenRead(path)) using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant();
        }
    }
}
