using System;
using System.IO;
using System.Reflection;
using NUnit.Framework;
using HumanVision.Editor;
using HumanVision.Demo;
using UnityEditor;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidGpuGateBuildTests
    {
        [Test]
        public void PreparedFixtureBlitUsesPointSamplingAndClampEdges()
        {
            var create = typeof(HumanVisionAndroidGpuGate).GetMethod(
                "CreatePreparedFixture", BindingFlags.NonPublic | BindingFlags.Static);
            Assert.NotNull(create, "Prepared gate must create a configured fixture for Graphics.Blit");
            var fixture = (Texture2D)create.Invoke(null, null);
            try
            {
                Assert.AreEqual(320, fixture.width);
                Assert.AreEqual(320, fixture.height);
                Assert.AreEqual(FilterMode.Point, fixture.filterMode);
                Assert.AreEqual(TextureWrapMode.Clamp, fixture.wrapMode);
            }
            finally { UnityEngine.Object.DestroyImmediate(fixture); }
        }

        [Test]
        public void OrdinaryEditorBuildCannotActivateGateBypass()
        {
            Assert.False(HumanVisionAndroidGpuGateBuild.IsAuthorizedGateBuild(null));
            var error = Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidGpuGateBuild.Build());
            StringAssert.Contains("isolated test script", error.Message);
        }

        [Test]
        public void PreparedGateRejectsChangedContractBeforeReadingModels()
        {
            string directory = Path.Combine(Path.GetTempPath(), "hv-prepared-gate-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            string manifest = Path.Combine(directory, "input-contract.json");
            try
            {
                File.WriteAllText(manifest, Manifest("android-ort-cpu", 320));
                var wrongProfile = Assert.Throws<InvalidOperationException>(() =>
                    HumanVisionAndroidGpuGateBuild.ValidatePreparedInputs(manifest, directory));
                StringAssert.Contains("pinned detector contract", wrongProfile.Message);

                File.WriteAllText(manifest, Manifest("android-ncnn-vulkan", 256));
                var wrongSize = Assert.Throws<InvalidOperationException>(() =>
                    HumanVisionAndroidGpuGateBuild.ValidatePreparedInputs(manifest, directory));
                StringAssert.Contains("pinned reference", wrongSize.Message);

                File.WriteAllText(manifest, Manifest("android-ncnn-vulkan", 320));
                var missingModel = Assert.Throws<InvalidOperationException>(() =>
                    HumanVisionAndroidGpuGateBuild.ValidatePreparedInputs(manifest, directory));
                StringAssert.Contains("model.param", missingModel.Message);
            }
            finally { Directory.Delete(directory, true); }
        }

        [Test]
        public void GeneratedGateSceneSkipsPreExistingUserScene()
        {
            const string preferred = "Assets/HumanVision/GpuGateGenerated/HumanVisionPreparedGate.unity";
            var selected = HumanVisionAndroidGpuGateBuild.FindAvailableGateScenePath(preferred,
                candidate => candidate == preferred);
            Assert.AreEqual("Assets/HumanVision/GpuGateGenerated/HumanVisionPreparedGate-1.unity", selected);
            Assert.Throws<InvalidOperationException>(() =>
                HumanVisionAndroidGpuGateBuild.FindAvailableGateScenePath(
                    "Assets/Scenes/HumanVisionPreparedGate.unity", _ => false));
        }

        [Test]
        public void GeneratedManifestCopySkipsExistingAssetAndImportsAsTextAsset()
        {
            const string preferred = "Assets/HumanVision/GpuGateGenerated/input-contract.json";
            string path = HumanVisionAndroidGpuGateBuild.FindAvailableGeneratedAssetPath(preferred,
                candidate => candidate == preferred);
            Assert.AreEqual("Assets/HumanVision/GpuGateGenerated/input-contract-1.json", path);
            string projectRoot = Directory.GetParent(Application.dataPath).FullName;
            path = HumanVisionAndroidGpuGateBuild.FindAvailableGeneratedAssetPath(
                "Assets/HumanVision/GpuGateGenerated/input-contract-test-" + Guid.NewGuid().ToString("N") + ".json",
                candidate => File.Exists(Path.Combine(projectRoot, candidate)) ||
                    File.Exists(Path.Combine(projectRoot, candidate) + ".meta") ||
                    AssetDatabase.LoadMainAssetAtPath(candidate) != null);
            string absolute = Path.Combine(projectRoot, path);
            Directory.CreateDirectory(Path.GetDirectoryName(absolute));
            try
            {
                File.WriteAllText(absolute, "{\"gate_fixture\":true}");
                AssetDatabase.Refresh();
                Assert.NotNull(AssetDatabase.LoadAssetAtPath<TextAsset>(path));
            }
            finally
            {
                if (!AssetDatabase.DeleteAsset(path))
                {
                    if (File.Exists(absolute)) File.Delete(absolute);
                    if (File.Exists(absolute + ".meta")) File.Delete(absolute + ".meta");
                    AssetDatabase.Refresh();
                }
            }
        }

        [Test]
        public void InteractiveGateUsesDedicatedAndroidApplicationId()
        {
            string originalId = PlayerSettings.GetApplicationIdentifier(BuildTargetGroup.Android);
            string originalProductName = PlayerSettings.productName;
            try
            {
                HumanVisionAndroidGpuGateBuild.SetInteractivePreparedPackageIdentity();
                Assert.AreEqual("com.blazetc.humanvision.preparedgate",
                    PlayerSettings.GetApplicationIdentifier(BuildTargetGroup.Android));
                Assert.AreEqual("HumanVision Prepared Detector Gate", PlayerSettings.productName);
            }
            finally
            {
                PlayerSettings.productName = originalProductName;
                PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android, originalId);
            }
        }

        [Test]
        public void SerializedAndroidSettingsSnapshotDetectsPackageIdentityChange()
        {
            string projectRoot = Directory.GetParent(Application.dataPath).FullName;
            string projectSettings = Path.Combine(projectRoot, "ProjectSettings/ProjectSettings.asset");
            string temporary = Path.GetTempFileName();
            try
            {
                string original = File.ReadAllText(projectSettings);
                File.WriteAllText(temporary, original);
                string before = HumanVisionAndroidGpuGateBuild.ReadSerializedAndroidSettings(temporary);
                StringAssert.Contains("productName:", before);
                StringAssert.Contains("Android:", before);
                File.WriteAllText(temporary, original.Replace("  productName:", "  productName: Changed-"));
                string after = HumanVisionAndroidGpuGateBuild.ReadSerializedAndroidSettings(temporary);
                Assert.AreNotEqual(before, after);
            }
            finally { File.Delete(temporary); }
        }

        [Test]
        public void StagedPinnedManifestAndDetectorFilesPass()
        {
            StagedPaths(out var manifest, out var detector);
            HumanVisionAndroidGpuGateBuild.ValidatePreparedInputs(manifest, detector);
        }

        [Test]
        public void StagedDetectorTamperFailsSha256()
        {
            StagedPaths(out var manifest, out var detector);
            string directory = Path.Combine(Path.GetTempPath(), "hv-prepared-gate-tamper-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try
            {
                File.Copy(Path.Combine(detector, "model.param"), Path.Combine(directory, "model.param"));
                File.Copy(Path.Combine(detector, "model.bin"), Path.Combine(directory, "model.bin"));
                using (var stream = new FileStream(Path.Combine(directory, "model.bin"), FileMode.Open, FileAccess.ReadWrite))
                {
                    int original = stream.ReadByte();
                    stream.Position = 0;
                    stream.WriteByte((byte)(original ^ 0xff));
                }
                var error = Assert.Throws<InvalidOperationException>(() =>
                    HumanVisionAndroidGpuGateBuild.ValidatePreparedInputs(manifest, directory));
                StringAssert.Contains("SHA-256 mismatch", error.Message);
            }
            finally { Directory.Delete(directory, true); }
        }

        private static void StagedPaths(out string manifest, out string detector)
        {
            string projectRoot = Directory.GetParent(Application.dataPath).FullName;
            string root = Path.Combine(projectRoot, "Assets/StreamingAssets/HumanVisionPreparedGate");
            manifest = Path.Combine(root, "input-contract.json");
            detector = Path.Combine(root, "detector");
            if (!File.Exists(manifest) || !File.Exists(Path.Combine(detector, "model.param")) ||
                !File.Exists(Path.Combine(detector, "model.bin")))
                Assert.Ignore("PREPARED gate staging is absent from this Unity project.");
        }

        private static string Manifest(string profile, int width)
        {
            return "{\"schema_version\":2,\"pack_id\":\"precision-t-26-ncnn-fp16\"," +
                "\"profile_id\":\"" + profile + "\",\"active_role\":\"detector\"," +
                "\"asset_root\":\"__ASSET_ROOT__\",\"models\":[{" +
                "\"role\":\"detector\",\"param_path\":\"detector/model.param\"," +
                "\"bin_path\":\"detector/model.bin\"," +
                "\"param_sha256\":\"9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3\"," +
                "\"bin_sha256\":\"4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da\"," +
                "\"input_contract\":{\"image_format\":\"rgba8-unorm\",\"color_order\":\"rgb\"," +
                "\"crop\":\"letterbox\",\"resize_interpolation\":\"bilinear\"," +
                "\"tensor_dtype\":\"fp16\",\"input_blob\":\"in0\",\"width\":" + width + "," +
                "\"height\":320,\"elempack\":1,\"pad_rgb\":[114,114,114]," +
                "\"normalization\":{\"mean\":[123.675,116.28,103.53]," +
                "\"norm\":[0.017124753831663668,0.01750700280112045,0.017429193899782137]}}}," +
                "{\"role\":\"body\"}]}";
        }
    }
}
