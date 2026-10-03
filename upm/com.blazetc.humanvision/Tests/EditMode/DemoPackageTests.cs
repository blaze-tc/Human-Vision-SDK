#if UNITY_EDITOR
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.Build;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class DemoPackageTests
    {
        private static Type EditorType(string name) => AppDomain.CurrentDomain.GetAssemblies().Select(assembly => assembly.GetType("HumanVision.Editor." + name)).FirstOrDefault(type => type != null);
        private string runtime, index, receipt, before;
        private static string Hash(byte[] data) { using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant(); }
        private static void Prepare()
        {
            var type = EditorType("HumanVisionModelInstaller"); Assert.NotNull(type);
            try { type.GetMethod("Prepare").Invoke(null, null); }
            catch (TargetInvocationException error) { throw error.InnerException; }
        }
        [OneTimeSetUp] public void SetUp()
        {
            runtime = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
            index = Path.Combine(runtime, "index.json"); receipt = Path.Combine(runtime, "staged-runtime.json");
            if (File.Exists(receipt)) File.Delete(receipt);
            Prepare(); before = File.ReadAllText(index);
        }
        [TearDown] public void TearDown()
        {
            if (before != null) File.WriteAllText(index, before);
            if (File.Exists(receipt)) File.Delete(receipt);
            string fixture = Path.Combine(runtime, "profiles", "task10-local.json"); if (File.Exists(fixture)) File.Delete(fixture);
        }
        private void Stage(string text)
        {
            File.WriteAllText(index, text);
            File.WriteAllText(receipt, "{\"schema_version\":1,\"stage_id\":\"local-qualified-runtime\",\"local_evaluation_only\":true,\"index_sha256\":\"" + Hash(File.ReadAllBytes(index)) + "\"}");
        }
        [Test] public void ExplicitStagedRuntimeSurvivesInstaller()
        {
            string fixture = Path.Combine(runtime, "profiles", "task10-local.json");
            string configuration = "{\"profile\":\"task10-local\",\"fixture\":\"installer-only\"}";
            File.WriteAllText(fixture, configuration);
            string staged = "{\"version\":\"task10-local-installer-fixture\",\"files\":[{\"path\":\"profiles/task10-local.json\",\"sha256\":\"" + Hash(File.ReadAllBytes(fixture)) + "\"}]}";
            Stage(staged); Prepare();
            Assert.That(File.ReadAllText(index), Is.EqualTo(staged));
            Assert.That(File.ReadAllText(fixture), Is.EqualTo(configuration));
        }
        [Test] public void InvalidStagedRuntimeFailsBeforeOverwrite()
        {
            Stage("{\"version\":\"local\",\"files\":[{\"path\":\"../outside\",\"sha256\":\"" + new string('0', 64) + "\"}]}");
            string invalid = File.ReadAllText(index);
            Assert.Throws<BuildFailedException>(Prepare);
            Assert.That(File.ReadAllText(index), Is.EqualTo(invalid));
        }
        [Test] public void MissingOrWrongStagedIndexFailsBeforeOverwrite()
        {
            Stage(before); File.AppendAllText(index, " "); string wrong = File.ReadAllText(index);
            Assert.Throws<BuildFailedException>(Prepare); Assert.That(File.ReadAllText(index), Is.EqualTo(wrong));
            File.Delete(index); Assert.Throws<BuildFailedException>(Prepare); Assert.That(File.Exists(index), Is.False);
        }
        [Test] public void MissingOrWrongStagedFileFailsBeforeOverwrite()
        {
            Stage("{\"version\":\"local\",\"files\":[{\"path\":\"profiles/missing-task10.json\",\"sha256\":\"" + new string('0', 64) + "\"}]}");
            string invalid = File.ReadAllText(index); Assert.Throws<BuildFailedException>(Prepare);
            Assert.That(File.ReadAllText(index), Is.EqualTo(invalid));
            Stage("{\"version\":\"local\",\"files\":[{\"path\":\"profiles/cpu.json\",\"sha256\":\"" + new string('0', 64) + "\"}]}");
            invalid = File.ReadAllText(index); Assert.Throws<BuildFailedException>(Prepare); Assert.That(File.ReadAllText(index), Is.EqualTo(invalid));
        }
        [Test] public void DefaultPackagedIndexInstallsWithoutReceipt()
        {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.blazetc.humanvision/package.json");
            Assert.That(File.ReadAllText(index), Is.EqualTo(File.ReadAllText(Path.Combine(package.resolvedPath, "RuntimeData", "index.json"))));
        }
        [Test] public void NativeAuditRejectsWrongHashAndMissingArtifact()
        {
            var type = EditorType("HumanVisionAndroidBuildSettings"); Assert.NotNull(type);
            var method = type.GetMethod("ValidateStaticNcnnAudit"); Assert.NotNull(method);
            Assert.That(method.Invoke(null, new object[] { "missing.so", "missing.json" }), Is.False);
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.blazetc.humanvision/package.json"); Assert.NotNull(package);
            string library = Path.Combine(package.resolvedPath, "Runtime", "Plugins", "Android", "arm64-v8a", "libhumanvision.so");
            string audit = Path.Combine(package.resolvedPath, "android-gpu-bridge-symbols.json");
            Assert.That(method.Invoke(null, new object[] { library, audit }), Is.True);
            string wrong = Path.Combine(runtime, "wrong-audit.json");
            try {
                File.WriteAllText(wrong, "{\"native_sha256\":\"" + new string('0', 64) + "\",\"abi\":\"arm64-v8a\",\"api_level\":26,\"ncnn_vulkan_symbols_verified\":true}");
                Assert.That(method.Invoke(null, new object[] { library, wrong }), Is.False);
            } finally { if (File.Exists(wrong)) File.Delete(wrong); }
        }
        [Test] public void UnknownRuntimeModeIsRejected()
        {
            var type = EditorType("HumanVisionAndroidRuntimeModeRegistry"); Assert.NotNull(type);
            Assert.Throws<TargetInvocationException>(() => type.GetMethod("Resolve").Invoke(null, new object[] { "unknown-mode" }));
        }
        [Test] public void HostOnlyProfileCannotQualifyApkAssets()
        {
            string project = Directory.GetParent(Application.dataPath).FullName;
            string host = Path.Combine(project, "profiles", "android-ncnn-vulkan.json");
            Directory.CreateDirectory(Path.GetDirectoryName(host));
            Assert.That(File.Exists(host), Is.False);
            string staged = Path.Combine(runtime, "profiles", "android-ncnn-vulkan.json");
            string saved = File.Exists(staged) ? File.ReadAllText(staged) : null;
            try {
                if (saved != null) File.Delete(staged);
                File.WriteAllText(host, "{}");
                var descriptor = EditorType("HumanVisionAndroidRuntimeModeRegistry").GetMethod("Resolve").Invoke(null, new object[] { "android-ncnn-vulkan" });
                var environment = EditorType("HumanVisionAndroidBuildSettings").GetMethod("CaptureEnvironment", BindingFlags.NonPublic | BindingFlags.Static).Invoke(null, new[] { descriptor });
                Assert.That(environment.GetType().GetProperty("HasProfile").GetValue(environment), Is.False);
            } finally { File.Delete(host); if (saved != null) File.WriteAllText(staged, saved); }
        }
    }
}
#endif
