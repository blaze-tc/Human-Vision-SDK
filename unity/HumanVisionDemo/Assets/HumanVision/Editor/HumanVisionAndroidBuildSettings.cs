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
                HasBridgeSymbolManifest = HasProjectFile("android-gpu-bridge-symbols.json"),
                HasProfile = HasProjectFile(Path.Combine("profiles", descriptor.ProfileId + ".json")),
                HasNcnnModelPackAssets = HasProjectFile(Path.Combine("modelpacks", "precision-t-26-ncnn-fp16", "detector", "model.param")) &&
                    HasProjectFile(Path.Combine("modelpacks", "precision-t-26-ncnn-fp16", "detector", "model.bin")) &&
                    HasProjectFile(Path.Combine("modelpacks", "precision-t-26-ncnn-fp16", "body", "model.param")) &&
                    HasProjectFile(Path.Combine("modelpacks", "precision-t-26-ncnn-fp16", "body", "model.bin")),
                HasNcnnModelPackSha256Index = HasProjectFile(Path.Combine("modelpacks", "precision-t-26-ncnn-fp16", "SHA256SUMS.txt"))
            };
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
            var root = Directory.GetParent(Application.dataPath).FullName;
            return ValidateStaticNcnnAudit(
                Path.Combine(Application.dataPath, "Plugins", "Android", "arm64-v8a", "libhumanvision.so"),
                Path.Combine(root, "android-gpu-bridge-symbols.json"));
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

        private static bool HasProjectFile(string relativePath)
        {
            var projectRoot = Directory.GetParent(Application.dataPath).FullName;
            var unityRoot = Directory.GetParent(projectRoot);
            var repositoryRoot = unityRoot == null ? null : unityRoot.Parent;
            return File.Exists(Path.Combine(projectRoot, relativePath)) ||
                (repositoryRoot != null && File.Exists(Path.Combine(repositoryRoot.FullName, relativePath))) ||
                File.Exists(Path.Combine(Application.streamingAssetsPath, "HumanVision", "RuntimeData", relativePath));
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
