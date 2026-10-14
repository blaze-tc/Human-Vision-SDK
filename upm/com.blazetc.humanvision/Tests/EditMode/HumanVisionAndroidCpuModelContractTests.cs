using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidCpuModelContractTests
    {
        private const string Profile = "android-cpu-nohands";
        private const string ProfilePath = "profiles/android-cpu-nohands.json";
        private const string PackPath = "modelpacks/yolov8n-pose-512x288-ort/";
        private static readonly string[] Closure = { ProfilePath, PackPath + "manifest.json", PackPath + "body.onnx" };
        private string temporaryRoot;
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path, sha256; }

        private static string ActualRuntime(string variable)
        {
            string value = Environment.GetEnvironmentVariable(variable);
            if (string.IsNullOrEmpty(value)) Assert.Ignore("Set " + variable + " to the exact local CPU runtime fixture.");
            Assert.That(Directory.Exists(value), Is.True, "Missing actual CPU fixture: " + value);
            return value;
        }
        private static Index ReadIndex(string root) => JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(root, "index.json")));
        private static void WriteIndex(string root, Index index) => File.WriteAllText(Path.Combine(root, "index.json"), JsonUtility.ToJson(index));
        private static SharedRecognitionSettings Settings(int people) => new SharedRecognitionSettings {
            MaxBodies = people, AccelerationMode = HumanVisionAccelerationMode.Cpu, InputQuality = ModelInputQuality.High
        };
        private string CopyYoloClosure()
        {
            string source = ActualRuntime("HV_CPU_TEST_RUNTIME");
            var index = ReadIndex(source);
            temporaryRoot = Path.Combine(Path.GetTempPath(), "HumanVision-CpuModelContract-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(temporaryRoot);
            foreach (string relative in Closure) {
                string target = Path.Combine(temporaryRoot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                File.Copy(Path.Combine(source, relative), target);
            }
            // Keep the complete actual 52-row index. Only this CPU closure is copied.
            WriteIndex(temporaryRoot, index);
            return temporaryRoot;
        }
        [TearDown] public void Cleanup()
        {
            if (string.IsNullOrEmpty(temporaryRoot) || !Directory.Exists(temporaryRoot)) return;
            string full = Path.GetFullPath(temporaryRoot), temp = Path.GetFullPath(Path.GetTempPath());
            Assert.That(full.StartsWith(temp, StringComparison.OrdinalIgnoreCase), Is.True);
            Assert.That(Path.GetFileName(full).StartsWith("HumanVision-CpuModelContract-", StringComparison.Ordinal), Is.True);
            Directory.Delete(full, true);
            temporaryRoot = null;
        }

        [TestCase(1)] [TestCase(2)] [TestCase(3)] [TestCase(4)]
        [TestCase(5)] [TestCase(6)] [TestCase(7)] [TestCase(8)]
        public void Actual52FileYoloStageAdmitsFixed512By288AtEveryCapacity(int people)
        {
            string source = ActualRuntime("HV_CPU_TEST_RUNTIME");
            Assert.That(ReadIndex(source).files.Length, Is.EqualTo(52));
            Assert.DoesNotThrow(() => HumanVisionAndroidCpuModelContract.Validate(source));
            var settings = Settings(people);
            var contract = settings.ResolveContract(source, Profile);
            Assert.That(contract.ProfileId, Is.EqualTo(Profile));
            Assert.That(contract.ModelPackId, Is.EqualTo("yolov8n-pose-512x288-ort"));
            Assert.That(contract.DetectorWidth, Is.Zero); Assert.That(contract.DetectorHeight, Is.Zero);
            Assert.That(contract.PoseWidth, Is.EqualTo(512)); Assert.That(contract.PoseHeight, Is.EqualTo(288));
            Assert.That(contract.DetectionCadence, Is.EqualTo(1)); Assert.That(settings.MaxBodies, Is.EqualTo(people));
            var capabilities = HumanVisionSdkQualityCapabilities.Load(source, Profile, people);
            Assert.That(capabilities.Error, Is.Empty); Assert.That(capabilities.Selectable, Is.False);
            StringAssert.Contains("512×288", capabilities.Message);
        }
        [TestCase(1, "precision-t-26", 192, 256)] [TestCase(2, "precision-t-26", 192, 256)]
        [TestCase(3, "rtmo-t-416", 416, 416)] [TestCase(8, "rtmo-t-416", 416, 416)]
        public void Actual47FileLegacyRuntimeRetainsBothCapacityBranches(int people, string pack, int width, int height)
        {
            string source = ActualRuntime("HV_CPU_LEGACY_TEST_RUNTIME");
            Assert.That(ReadIndex(source).files.Length, Is.EqualTo(47));
            Assert.DoesNotThrow(() => HumanVisionAndroidCpuModelContract.Validate(source));
            var contract = Settings(people).ResolveContract(source, Profile);
            Assert.That(contract.ModelPackId, Is.EqualTo(pack));
            Assert.That(contract.PoseWidth, Is.EqualTo(width)); Assert.That(contract.PoseHeight, Is.EqualTo(height));
        }
        [Test] public void AdmittedYoloClosureNeedsOnlyItsThreePinnedFiles()
        {
            string root = CopyYoloClosure();
            Assert.That(File.Exists(Path.Combine(root, "modelpacks/rtmo-t-416/body.onnx")), Is.False);
            Assert.That(File.Exists(Path.Combine(root, "profiles/windows-pc-cpu.json")), Is.False);
            Assert.DoesNotThrow(() => HumanVisionAndroidCpuModelContract.Validate(root));
            Assert.That(Settings(8).ResolveContract(root, Profile).PoseWidth, Is.EqualTo(512));
        }

        [TestCase("unknown-profile-index")] [TestCase("reindexed-unknown-profile")] [TestCase("indexed-xnnpack-profile")]
        [TestCase("body-index-hash")] [TestCase("body-bytes")] [TestCase("reindexed-body")]
        [TestCase("missing-body")] [TestCase("ambiguous-manifest")] [TestCase("duplicate-index-path")]
        public void CpuClosureRejectsDamageBeforeAContractOrChoiceIsAdvertised(string mutation)
        {
            string root = CopyYoloClosure();
            var index = ReadIndex(root);
            string body = Path.Combine(root, PackPath + "body.onnx"), profile = Path.Combine(root, ProfilePath);
            string expectedMessage;
            switch (mutation) {
                case "unknown-profile-index":
                    index.files.Single(row => row.path == ProfilePath).sha256 = new string('0', 64);
                    expectedMessage = "profiles/android-cpu-nohands.json"; break;
                case "reindexed-unknown-profile":
                    string text = File.ReadAllText(profile); StringAssert.Contains("backend.ort.cpu", text);
                    File.WriteAllText(profile, text.Replace("backend.ort.cpu", "backend.ort.directml"));
                    index.files.Single(row => row.path == ProfilePath).sha256 = Hash(profile);
                    expectedMessage = "profiles/android-cpu-nohands.json"; break;
                case "indexed-xnnpack-profile":
                    File.Copy(Path.Combine(ActualRuntime("HV_CPU_LEGACY_TEST_RUNTIME"), "profiles/android-xnnpack-nohands.json"), profile, true);
                    Assert.That(Hash(profile), Is.EqualTo("6754f27d1624c49da96d9c38f4d5860a38e9dc2a0985e1a71a2f49b549c3a588"));
                    index.files.Single(row => row.path == ProfilePath).sha256 = Hash(profile);
                    expectedMessage = "profiles/android-cpu-nohands.json"; break;
                case "body-index-hash":
                    index.files.Single(row => row.path == PackPath + "body.onnx").sha256 = new string('0', 64);
                    expectedMessage = "body.onnx"; break;
                case "body-bytes": case "reindexed-body":
                    using (var file = File.Open(body, FileMode.Open, FileAccess.ReadWrite)) {
                        int original = file.ReadByte(); Assert.That(original, Is.GreaterThanOrEqualTo(0));
                        file.Position = 0; file.WriteByte((byte)(original ^ 1));
                    }
                    if (mutation == "reindexed-body") index.files.Single(row => row.path == PackPath + "body.onnx").sha256 = Hash(body);
                    expectedMessage = "body.onnx"; break;
                case "missing-body": File.Delete(body); expectedMessage = "body.onnx"; break;
                case "ambiguous-manifest":
                    File.Copy(Path.Combine(root, PackPath + "manifest.json"), Path.Combine(root, PackPath + "modelpack.json"));
                    expectedMessage = "Ambiguous"; break;
                case "duplicate-index-path":
                    index.files = index.files.Concat(new[] { index.files.First(row => row.path == ProfilePath) }).ToArray();
                    expectedMessage = "Duplicate"; break;
                default: throw new InvalidOperationException("Unknown test mutation.");
            }
            WriteIndex(root, index);
            var error = Assert.Throws<InvalidDataException>(() => Settings(4).ResolveContract(root, Profile));
            StringAssert.Contains(expectedMessage, error.Message);
            var capabilities = HumanVisionSdkQualityCapabilities.Load(root, Profile, 4);
            Assert.That(capabilities.Error, Is.Not.Empty); Assert.That(capabilities.Choices, Is.Empty);
        }
        private static string Hash(string path)
        {
            using (var file = File.OpenRead(path)) using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant();
        }
    }
}
