using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Xml;
using UnityEditor;
using UnityEditor.Android;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Editor
{
    public sealed class HumanVisionAndroidBuildSettings : IPreprocessBuildWithReport
    {
        public int callbackOrder => 0;
        public void OnPreprocessBuild(BuildReport report)
        {
            if (report.summary.platform != BuildTarget.Android) return;
            if (HumanVisionAndroidGpuGateBuild.IsAuthorizedGateBuild(report)) return;
            var descriptor = HumanVisionAndroidRuntimeModeRegistry.Resolve(HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId);
            var issues = HumanVisionAndroidRuntimeBuildValidator.Validate(descriptor, CaptureEnvironment(descriptor));
            foreach (var issue in issues.Where(issue => !issue.IsError))
                Debug.LogWarning(issue.Message);
            var errors = issues.Where(issue => issue.IsError).Select(issue => issue.Message).ToArray();
            if (errors.Length != 0)
                throw new BuildFailedException(string.Join("\n", errors));
        }

        internal static AndroidBuildEnvironment CaptureEnvironment(HumanVisionAndroidRuntimeModeDescriptor descriptor)
        {
            var graphicsApis = PlayerSettings.GetGraphicsAPIs(BuildTarget.Android);
            bool validatedPack = descriptor.RequiresNcnn && ValidateNcnnModelPack(
                Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime"), descriptor.ProfileId);
            return new AndroidBuildEnvironment
            {
                MinimumApiLevel = (int)PlayerSettings.Android.minSdkVersion,
                Arm64Only = PlayerSettings.Android.targetArchitectures == AndroidArchitecture.ARM64,
                Il2Cpp = PlayerSettings.GetScriptingBackend(BuildTargetGroup.Android) == ScriptingImplementation.IL2CPP,
                VulkanAvailable = graphicsApis.Contains(GraphicsDeviceType.Vulkan),
                VulkanFirst = graphicsApis.Length > 0 && graphicsApis[0] == GraphicsDeviceType.Vulkan,
                AutomaticGraphicsApis = PlayerSettings.GetUseDefaultGraphicsAPIs(BuildTarget.Android),
                OpenGlesAvailable = graphicsApis.Contains(GraphicsDeviceType.OpenGLES3) || graphicsApis.Contains(GraphicsDeviceType.OpenGLES2),
                HasHumanVisionLibrary = HasAndroidArm64Plugin("libhumanvision.so"),
                // The production Android build links ncnn statically into libhumanvision.so.
                // The symbol manifest is emitted only after the ELF/ncnn audit succeeds.
                HasNcnnLibrary = HasAndroidArm64Plugin("libncnn.so") || HasAuditedStaticNcnn(),
                HasBridgeSymbolManifest = HasAuditedStaticNcnn(),
                HasProfile = HasProjectFile(Path.Combine("profiles", descriptor.ProfileId + ".json")),
                HasNcnnModelPackAssets = validatedPack,
                // This DTO flag means a verified schema-2 manifest/hash index, not a SHA256SUMS filename.
                HasNcnnModelPackSha256Index = validatedPack
            };
        }

        [Serializable] private sealed class PackIndex { public string version; public PackFile[] files; }
        [Serializable] private sealed class PackFile { public string path; public string sha256; }
        [Serializable] private sealed class NcnnPack { public int schema_version; public string pack_id; public string pack_version; public string profile_id; public string pipeline_id; public string profile_sha256; public bool local_evaluation_only; public string execution_contract; public int max_people; public string[] capabilities; public NcnnModel[] models; public PackFile[] evidence; }
        [Serializable] private sealed class NcnnModel { public string role; public string format; public string decoder_id; public string execution_contract; public string param_path; public string param_sha256; public string bin_path; public string bin_sha256; public ModelOutput output_contract; }
        [Serializable] private sealed class ModelOutput { public string decoder; }
        [Serializable] private sealed class RuntimeProfile { public int schema_version; public string profile; public ProfileBody body; public bool local_evaluation_only; public ProfileHands hands; public ProfileBackend backend; }
        [Serializable] private sealed class ProfileBody { public string pipeline; public string modelPack; }
        [Serializable] private sealed class ProfileHands { public bool enabled; }
        [Serializable] private sealed class ProfileBackend { public string[] preference; public bool allow_fallback = true; }
        internal static bool ValidateNcnnModelPack(string runtimeRoot, string profileId)
        {
            try {
                ValidateComponentId(profileId);
                var index = JsonUtility.FromJson<PackIndex>(File.ReadAllText(Path.Combine(runtimeRoot, "index.json")));
                if (index == null || string.IsNullOrEmpty(index.version) || index.files == null || index.files.Length == 0) throw new InvalidDataException("Empty runtime index.");
                var files = new System.Collections.Generic.Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                foreach (var file in index.files) {
                    if (file == null) throw new InvalidDataException("Missing runtime index entry.");
                    string path = ContainedPath(runtimeRoot, file.path);
                    if (files.ContainsKey(file.path)) throw new InvalidDataException("Duplicate runtime index path: " + file.path);
                    VerifyHash(path, file.sha256); files.Add(file.path, file.sha256);
                }
                string profileRelative = "profiles/" + profileId + ".json";
                string profilePath = ContainedPath(runtimeRoot, profileRelative);
                if (!files.ContainsKey(profileRelative)) throw new InvalidDataException("Selected runtime profile is not indexed.");
                var profile = JsonUtility.FromJson<RuntimeProfile>(File.ReadAllText(profilePath));
                if (profile == null || profile.schema_version != 1 || profile.profile != profileId || profile.body == null) throw new InvalidDataException("Selected runtime profile identity/schema mismatch.");
                ValidateComponentId(profile.body.modelPack);
                if (profile.hands == null || profile.hands.enabled || profile.backend == null || profile.backend.allow_fallback ||
                    profile.backend.preference == null || profile.backend.preference.Length != 1 || profile.backend.preference[0] != "backend.ncnn.vulkan")
                    throw new InvalidDataException("Selected NCNN profile requires hands disabled and exactly backend.ncnn.vulkan without fallback.");
                string prefix = "modelpacks/" + profile.body.modelPack + "/";
                bool hasModelpack = File.Exists(ContainedPath(runtimeRoot, prefix + "modelpack.json"));
                bool hasManifest = File.Exists(ContainedPath(runtimeRoot, prefix + "manifest.json"));
                if (hasModelpack && hasManifest) throw new InvalidDataException("Ambiguous model-pack manifests.");
                string manifestRelative = prefix + (hasModelpack ? "modelpack.json" : "manifest.json");
                if (!files.ContainsKey(manifestRelative)) throw new InvalidDataException("Selected model-pack manifest is not indexed.");
                var pack = JsonUtility.FromJson<NcnnPack>(File.ReadAllText(ContainedPath(runtimeRoot, manifestRelative)));
                if (pack == null || pack.schema_version != 2 || pack.pack_id != profile.body.modelPack ||
                    (pack.profile_id != null && pack.profile_id != profileId) || pack.pipeline_id != profile.body.pipeline ||
                    string.IsNullOrEmpty(pack.pack_version) || pack.max_people < 1 || pack.max_people > 8 || pack.models == null)
                    throw new InvalidDataException("Unsupported NCNN model-pack schema/components/profile binding.");
                VerifyHash(profilePath, pack.profile_sha256);
                bool yolo = pack.pipeline_id == "pipeline.yolo.pose";
                if ((!yolo && pack.pipeline_id != "pipeline.topdown") || pack.models.Length != (yolo ? 1 : 2))
                    throw new InvalidDataException("Unsupported NCNN pipeline/model cardinality.");
                // The existing local YOLO route is raw FP32; TopDown retains its two-role SimCC contract.
                if (yolo && (!pack.local_evaluation_only || !profile.local_evaluation_only || pack.execution_contract != "raw_tensor_fp32_v1"))
                    throw new InvalidDataException("YOLO requires the approved local raw_tensor_fp32_v1 profile/model pack.");
                if (!yolo && pack.execution_contract != null) throw new InvalidDataException("TopDown has no raw-tensor execution contract.");
                var capabilities = new System.Collections.Generic.HashSet<string>(pack.capabilities ?? new string[0], StringComparer.Ordinal);
                foreach (var required in new[] { "body_pose", "multi_person", "gpu_input", "vulkan" })
                    if (!capabilities.Contains(required)) throw new InvalidDataException("Model-pack missing capability: " + required);
                if (yolo ? capabilities.Contains("fp16-storage") || capabilities.Contains("fp16-arithmetic") :
                    !capabilities.Contains("fp16-storage") || !capabilities.Contains("fp16-arithmetic"))
                    throw new InvalidDataException("Model-pack FP32/FP16 eligibility mismatch.");
                var roles = new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);
                var modelPaths = new System.Collections.Generic.HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var model in pack.models) {
                    if (model == null || model.format != "ncnn" || (yolo ? model.role != "body" : model.role != "detector" && model.role != "body") || !roles.Add(model.role)) throw new InvalidDataException("Unsupported/duplicate NCNN component.");
                    string decoder = yolo ? "yolov8_pose_dfl17_v1" : model.role == "detector" ? "rtmdet_nano_raw_v1" : "simcc_body26_v1";
                    if (model.decoder_id != decoder || model.output_contract == null || model.output_contract.decoder != decoder ||
                        (yolo ? model.execution_contract != pack.execution_contract : model.execution_contract != null))
                        throw new InvalidDataException("NCNN decoder/execution contract mismatch.");
                    foreach (var item in new[] { new PackFile { path = model.param_path, sha256 = model.param_sha256 }, new PackFile { path = model.bin_path, sha256 = model.bin_sha256 } }) {
                        string relative = prefix + item.path;
                        ContainedPath(Path.Combine(runtimeRoot, "modelpacks", pack.pack_id), item.path);
                        if (!modelPaths.Add(relative) || !files.TryGetValue(relative, out string indexed) || !string.Equals(indexed, item.sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Model declaration/index mismatch: " + relative);
                        VerifyHash(ContainedPath(runtimeRoot, relative), item.sha256);
                    }
                }
                foreach (var item in pack.evidence ?? new PackFile[0]) {
                    if (item == null) throw new InvalidDataException("Missing model-pack evidence declaration.");
                    ContainedPath(Path.Combine(runtimeRoot, "modelpacks", pack.pack_id), item.path);
                    string relative = prefix + item.path;
                    if (!files.TryGetValue(relative, out string indexed) || indexed != item.sha256)
                        throw new InvalidDataException("Model-pack evidence declaration/index mismatch: " + relative);
                    VerifyHash(ContainedPath(runtimeRoot, relative), item.sha256);
                }
                return true;
            } catch (Exception error) { throw new BuildFailedException("NCNN model-pack validation failed for selected runtime profile " + profileId + ": " + error.Message); }
        }
        private static void ValidateComponentId(string id)
        {
            if (id == null || !System.Text.RegularExpressions.Regex.IsMatch(id, "^[a-z0-9][a-z0-9._-]{0,63}$"))
                throw new InvalidDataException("Invalid profile/model-pack component id.");
        }
        private static string ContainedPath(string root, string relative)
        {
            if (string.IsNullOrEmpty(relative) || relative.Contains("..") || relative.Contains(":") || relative.Contains("\\") || relative.StartsWith("/")) throw new InvalidDataException("Unsafe runtime/model path.");
            if (relative.Split('/').Any(part => part.Length == 0 || part == ".")) throw new InvalidDataException("Noncanonical runtime/model path.");
            string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            string path = Path.GetFullPath(Path.Combine(root, relative));
            if (!path.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Runtime/model path escapes root.");
            for (string current = path; current.Length >= fullRoot.Length; current = Path.GetDirectoryName(current))
                if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Runtime/model path uses a symbolic link or junction.");
            return path;
        }
        private static void VerifyHash(string path, string expected)
        {
            if (expected == null || !System.Text.RegularExpressions.Regex.IsMatch(expected, "^[0-9a-f]{64}$") || !File.Exists(path)) throw new InvalidDataException("Missing file/hash: " + path);
            using (var stream = File.OpenRead(path)) using (var sha = SHA256.Create())
                if (!string.Equals(BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", ""), expected, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("SHA-256 mismatch: " + path);
        }
        private static bool HasAndroidArm64Plugin(string filename)
        {
            return PluginImporter.GetAllImporters().Any(importer =>
                string.Equals(Path.GetFileName(importer.assetPath), filename, StringComparison.OrdinalIgnoreCase) &&
                importer.GetCompatibleWithPlatform(BuildTarget.Android) &&
                string.Equals(importer.GetPlatformData("Android", "CPU"), "ARM64", StringComparison.OrdinalIgnoreCase));
        }

        [Serializable]
        private sealed class StaticNcnnAudit
        {
            public string native_sha256;
            public string abi;
            public int api_level;
            public bool ncnn_vulkan_symbols_verified;
        }

        private static bool HasAuditedStaticNcnn()
        {
            if (!HasAndroidArm64Plugin("libhumanvision.so")) return false;
            var importer = PluginImporter.GetAllImporters().FirstOrDefault(value =>
                Path.GetFileName(value.assetPath) == "libhumanvision.so" && value.GetCompatibleWithPlatform(BuildTarget.Android));
            if (importer == null) return false;
            string library = ResolveAssetPath(importer.assetPath);
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath(importer.assetPath);
            string audit = package == null ? Path.Combine(Directory.GetParent(Application.dataPath).FullName, "android-gpu-bridge-symbols.json")
                : Path.Combine(package.resolvedPath, "android-gpu-bridge-symbols.json");
            return ValidateStaticNcnnAudit(library, audit);
        }

        public static bool ValidateStaticNcnnAudit(string library, string manifest)
        {
            if (!File.Exists(library) || !File.Exists(manifest)) return false;
            StaticNcnnAudit audit;
            try { audit = JsonUtility.FromJson<StaticNcnnAudit>(File.ReadAllText(manifest)); }
            catch { return false; }
            if (audit == null || audit.abi != "arm64-v8a" || audit.api_level != 26 ||
                !audit.ncnn_vulkan_symbols_verified || audit.native_sha256 == null ||
                audit.native_sha256.Length != 64) return false;
            using (var file = File.OpenRead(library))
            {
                var header = new byte[20];
                if (file.Read(header, 0, header.Length) != header.Length ||
                    header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' ||
                    header[4] != 2 || header[5] != 1 || header[18] != 183 || header[19] != 0) return false;
                file.Position = 0;
                using (var sha = SHA256.Create())
                    return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant() == audit.native_sha256;
            }
        }

        private static string ResolveAssetPath(string path)
        {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath(path);
            if (package == null) return Path.GetFullPath(path);
            string prefix = "Packages/" + package.name + "/";
            return Path.Combine(package.resolvedPath, path.Substring(prefix.Length));
        }

        private static bool HasProjectFile(string relativePath)
        {
            return File.Exists(Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime", relativePath));
        }
    }

    public sealed class HumanVisionAndroidGradleMetadataWriter : IPostGenerateGradleAndroidProject
    {
        private const string AndroidNamespace = "http://schemas.android.com/apk/res/android";
        public int callbackOrder => 0;

        public void OnPostGenerateGradleAndroidProject(string path)
        {
            var descriptor = HumanVisionAndroidRuntimeModeRegistry.Resolve(HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId);
            WriteSelectionMetadata(FindApplicationManifest(path), descriptor);
        }

        public static void WriteSelectionMetadata(string manifestPath, HumanVisionAndroidRuntimeModeDescriptor descriptor)
        {
            var document = new XmlDocument();
            document.Load(manifestPath);
            var manifest = document.DocumentElement;
            var application = manifest.SelectSingleNode("application") as XmlElement;
            if (application == null)
                throw new BuildFailedException("Generated Android application manifest has no application element.");
            WriteMetadata(document, application, global::HumanVision.HumanVisionAndroidRuntimeSelection.RuntimeModeMetadataKey, descriptor.Id);
            WriteMetadata(document, application, global::HumanVision.HumanVisionAndroidRuntimeSelection.ProfileIdMetadataKey, descriptor.ProfileId);
            document.Save(manifestPath);
        }

        private static void WriteMetadata(XmlDocument document, XmlElement application, string key, string value)
        {
            var metadata = application.ChildNodes.OfType<XmlElement>().FirstOrDefault(element =>
                element.Name == "meta-data" && element.GetAttribute("name", AndroidNamespace) == key);
            if (metadata == null)
            {
                metadata = document.CreateElement("meta-data");
                application.AppendChild(metadata);
            }
            metadata.SetAttribute("name", AndroidNamespace, key);
            metadata.SetAttribute("value", AndroidNamespace, value);
        }

        private static string FindApplicationManifest(string gradleProjectPath)
        {
            var candidates = new[]
            {
                Path.Combine(gradleProjectPath, "launcher", "src", "main", "AndroidManifest.xml"),
                Path.Combine(gradleProjectPath, "src", "main", "AndroidManifest.xml")
            };
            var manifest = candidates.FirstOrDefault(File.Exists);
            if (manifest == null)
                throw new BuildFailedException("Unable to find the generated Android application manifest for HumanVision runtime metadata.");
            return manifest;
        }
    }
}
