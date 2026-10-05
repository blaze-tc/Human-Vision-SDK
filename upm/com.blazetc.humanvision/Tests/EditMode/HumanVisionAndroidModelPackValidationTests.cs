using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidModelPackValidationTests
    {
        private const string Profile = "android-ncnn-vulkan";
        private const string Pack = "yolov8n-pose-rectangle640x384-fp32-local";
        private string root;
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path; public string sha256; }

        [SetUp] public void SetUp()
        {
            if (!Directory.Exists(Path.Combine(Application.dataPath, "..", "ApprovedRuntime")))
                Assert.Ignore("Run the isolated Android build contract fixture to stage the exact approved runtime closure.");
            root = Path.Combine(Path.GetTempPath(), "HumanVision-pack-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(root);
            CopyTree(Path.Combine(Application.dataPath, "..", "ApprovedRuntime"), root);
        }
        [TearDown] public void TearDown() { if (Directory.Exists(root)) Directory.Delete(root, true); }

        [Test] public void ApprovedVersionedYoloClosurePassesWithoutLegacyAssetsOrProfileId()
        {
            Assert.That(File.ReadAllText(Manifest), Does.Not.Contain("profile_id"));
            Assert.That(Directory.Exists(Path.Combine(root, "modelpacks", "precision-t-26-ncnn-fp16")), Is.False);
            Assert.DoesNotThrow(() => Assert.That(Validate(), Is.True));
        }

        [Test] public void ExistingTopDownClosureContinuesToPass()
        {
            Directory.Delete(root, true);
            Directory.CreateDirectory(root);
            CopyTree(Path.Combine(Application.dataPath, "..", "ApprovedTopDownRuntime"), root);
            Assert.That(Validate(), Is.True);
        }

        [Test] public void EnvironmentDerivesAssetsFromValidatedSelectedYoloPack()
        {
            string staged = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
            string backup = Directory.Exists(staged) ? Path.Combine(Path.GetDirectoryName(staged), "Runtime-environment-" + Guid.NewGuid().ToString("N")) : null;
            if (backup != null) Directory.Move(staged, backup);
            Directory.CreateDirectory(staged);
            try
            {
                CopyTree(root, staged);
                var type = EditorType();
                var registry = type.Assembly.GetType("HumanVision.Editor.HumanVisionAndroidRuntimeModeRegistry");
                var descriptor = registry.GetMethod("Resolve").Invoke(null, new object[] { Profile });
                var environment = type.GetMethod("CaptureEnvironment", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, new[] { descriptor });
                Assert.That(environment.GetType().GetProperty("HasNcnnModelPackAssets").GetValue(environment), Is.True);
                Assert.That(environment.GetType().GetProperty("HasNcnnModelPackSha256Index").GetValue(environment), Is.True);
            }
            finally { Directory.Delete(staged, true); if (backup != null) Directory.Move(backup, staged); }
        }

        [Test] public void ExplicitOrtSelectionDoesNotRequireNcnnClosure()
        {
            var type = EditorType();
            var registry = type.Assembly.GetType("HumanVision.Editor.HumanVisionAndroidRuntimeModeRegistry");
            var descriptor = registry.GetMethod("Resolve").Invoke(null, new object[] { "android-ort-cpu" });
            Assert.DoesNotThrow(() => type.GetMethod("CaptureEnvironment", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, new[] { descriptor }));
        }

        [TestCase("missing-version")]
        [TestCase("missing-file")]
        [TestCase("tampered-file")]
        [TestCase("unindexed-file")]
        [TestCase("declaration-hash")]
        [TestCase("profile-hash")]
        [TestCase("profile-identity")]
        [TestCase("profile-id")]
        [TestCase("pack-id")]
        [TestCase("pipeline-binding")]
        [TestCase("unknown-pipeline")]
        [TestCase("unknown-role")]
        [TestCase("duplicate-role")]
        [TestCase("duplicate-index")]
        [TestCase("index-escape")]
        [TestCase("model-escape")]
        [TestCase("absolute-model")]
        [TestCase("pack-escape")]
        [TestCase("decoder")]
        [TestCase("execution")]
        [TestCase("model-execution")]
        [TestCase("non-local")]
        [TestCase("hands")]
        [TestCase("fallback")]
        [TestCase("backend")]
        [TestCase("ambiguous-manifest")]
        public void InvalidSelectedClosureFailsBeforeBuild(string mutation)
        {
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(root, "index.json")));
            string model = "modelpacks/" + Pack + "/yolov8n_pose.ncnn.param";
            switch (mutation)
            {
                case "missing-version": index.version = null; break;
                case "missing-file": File.Delete(Path.Combine(root, model)); break;
                case "tampered-file": File.AppendAllText(Path.Combine(root, model), "tamper"); break;
                case "unindexed-file": index.files = index.files.Where(x => x.path != model).ToArray(); break;
                case "declaration-hash": Replace(Manifest, "908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905", new string('0', 64)); break;
                case "profile-hash": Replace(Manifest, "5cd72fca9ea22a276bf94bb279777ea687488eee89a5308f432ee48edc68185e", new string('0', 64)); break;
                case "profile-identity": Replace(ProfilePath, "\"profile\": \"android-ncnn-vulkan\"", "\"profile\": \"different\""); break;
                case "profile-id": Replace(Manifest, "\"pack_version\"", "\"profile_id\": \"different\", \"pack_version\""); break;
                case "pack-id": Replace(Manifest, "\"pack_id\": \"" + Pack + "\"", "\"pack_id\": \"different\""); break;
                case "pipeline-binding": Replace(Manifest, "pipeline.yolo.pose", "pipeline.topdown"); break;
                case "unknown-pipeline": Replace(Manifest, "pipeline.yolo.pose", "pipeline.unknown"); Replace(ProfilePath, "pipeline.yolo.pose", "pipeline.unknown"); RebindProfile(); break;
                case "unknown-role": Replace(Manifest, "\"role\": \"body\"", "\"role\": \"hands\""); break;
                case "duplicate-role":
                    var text = File.ReadAllText(Manifest); var start = text.IndexOf("    {", text.IndexOf("\"models\"", StringComparison.Ordinal), StringComparison.Ordinal); var end = text.LastIndexOf("\n    }");
                    File.WriteAllText(Manifest, text.Insert(end + 6, ",\n" + text.Substring(start, end + 6 - start))); break;
                case "duplicate-index": index.files = index.files.Concat(new[] { index.files[0] }).ToArray(); break;
                case "index-escape": index.files[0].path = "../escape"; break;
                case "model-escape": Replace(Manifest, "\"param_path\": \"yolov8n_pose.ncnn.param\"", "\"param_path\": \"../yolov8n_pose.ncnn.param\""); break;
                case "absolute-model": Replace(Manifest, "\"param_path\": \"yolov8n_pose.ncnn.param\"", "\"param_path\": \"/yolov8n_pose.ncnn.param\""); break;
                case "pack-escape": Replace(ProfilePath, Pack, "../" + Pack); RebindProfile(); break;
                case "decoder": Replace(Manifest, "yolov8_pose_dfl17_v1", "unknown_decoder"); break;
                case "execution": Replace(Manifest, "raw_tensor_fp32_v1", "unknown_execution"); break;
                case "model-execution": var m = File.ReadAllText(Manifest); var pos = m.LastIndexOf("raw_tensor_fp32_v1", StringComparison.Ordinal); File.WriteAllText(Manifest, m.Remove(pos, 18).Insert(pos, "wrong_execution")); break;
                case "non-local": Replace(Manifest, "\"local_evaluation_only\": true", "\"local_evaluation_only\": false"); break;
                case "hands": Replace(ProfilePath, "\"enabled\": false", "\"enabled\": true"); RebindProfile(); break;
                case "fallback": Replace(ProfilePath, "\"allow_fallback\": false", "\"allow_fallback\": true"); RebindProfile(); break;
                case "backend": Replace(ProfilePath, "backend.ncnn.vulkan", "backend.ort.cpu"); RebindProfile(); break;
                case "ambiguous-manifest": File.Copy(Manifest, Path.Combine(Path.GetDirectoryName(Manifest), "manifest.json")); break;
            }
            foreach (var entry in index.files)
                if (entry.path == "profiles/" + Profile + ".json" || entry.path == "modelpacks/" + Pack + "/modelpack.json") entry.sha256 = Hash(Path.Combine(root, entry.path));
            File.WriteAllText(Path.Combine(root, "index.json"), JsonUtility.ToJson(index));
            var error = Assert.Throws<TargetInvocationException>(() => Validate());
            Assert.That(error.InnerException.GetType().FullName, Is.EqualTo("UnityEditor.Build.BuildFailedException"));
            Assert.That(error.InnerException.Message, Does.Contain("NCNN model-pack validation failed"));
        }

        private string Manifest => Path.Combine(root, "modelpacks", Pack, "modelpack.json");
        private string ProfilePath => Path.Combine(root, "profiles", Profile + ".json");
        private void RebindProfile() { Replace(Manifest, "5cd72fca9ea22a276bf94bb279777ea687488eee89a5308f432ee48edc68185e", Hash(ProfilePath)); }
        private bool Validate()
        {
            var type = EditorType();
            return (bool)type.GetMethod("ValidateNcnnModelPack", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, new object[] { root, Profile });
        }
        private static Type EditorType() => AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.Editor.HumanVisionAndroidBuildSettings")).First(t => t != null);
        private static void Replace(string path, string from, string to) { var value = File.ReadAllText(path); Assert.That(value, Does.Contain(from)); File.WriteAllText(path, value.Replace(from, to)); }
        private static string Hash(string path) { using (var stream = File.OpenRead(path)) using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant(); }
        private static void CopyTree(string source, string target)
        {
            Assert.That(Directory.Exists(source), Is.True, "Real approved runtime fixture must be staged.");
            foreach (var directory in Directory.GetDirectories(source, "*", SearchOption.AllDirectories)) Directory.CreateDirectory(Path.Combine(target, directory.Substring(source.Length + 1)));
            foreach (var file in Directory.GetFiles(source, "*", SearchOption.AllDirectories)) File.Copy(file, Path.Combine(target, file.Substring(source.Length + 1)));
        }
    }
}
