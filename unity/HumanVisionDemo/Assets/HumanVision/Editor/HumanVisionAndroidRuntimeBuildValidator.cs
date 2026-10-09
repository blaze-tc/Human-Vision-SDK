using System;
using System.Collections.Generic;

namespace HumanVision.Editor
{
    public sealed class AndroidBuildEnvironment
    {
        public int MinimumApiLevel { get; set; }
        public bool Arm64Only { get; set; }
        public bool Il2Cpp { get; set; }
        public bool VulkanAvailable { get; set; }
        public bool VulkanFirst { get; set; }
        public bool AutomaticGraphicsApis { get; set; }
        public bool OpenGlesAvailable { get; set; }
        public bool HasHumanVisionLibrary { get; set; }
        public bool HasNcnnLibrary { get; set; }
        public bool HasBridgeSymbolManifest { get; set; }
        public bool HasProfile { get; set; }
        public bool HasNcnnModelPackAssets { get; set; }
        public bool HasNcnnModelPackSha256Index { get; set; }
        public bool HasNeuralRuntimeAudit { get; set; }
        public bool HasNeuralModelPackAssets { get; set; }
        public bool HasCpuRuntimeAudit { get; set; }
        public bool HasCpuModelPackAssets { get; set; }
    }

    public sealed class AndroidBuildValidationIssue
    {
        public string Code { get; }
        public string Message { get; }
        public bool IsError { get; }

        public AndroidBuildValidationIssue(string code, string message, bool isError)
        {
            Code = code;
            Message = message;
            IsError = isError;
        }
    }

    public static class HumanVisionAndroidRuntimeBuildValidator
    {
        public static IReadOnlyList<AndroidBuildValidationIssue> Validate(
            HumanVisionAndroidRuntimeModeDescriptor descriptor,
            AndroidBuildEnvironment environment)
        {
            var issues = new List<AndroidBuildValidationIssue>();
            if (descriptor == null)
            {
                issues.Add(Error("RuntimeMode", "HumanVision Android runtime mode is not selected."));
                return issues;
            }

            if (environment == null)
            {
                issues.Add(Error("BuildEnvironment", "HumanVision Android build environment is unavailable."));
                return issues;
            }

            if (environment.MinimumApiLevel < 26)
                issues.Add(Error("MinimumApiLevel", "HumanVision Android runtime requires minimum API level 26 or newer."));
            if (!environment.Arm64Only)
                issues.Add(Error("Arm64Only", "HumanVision Android runtime requires ARM64 as the only target architecture."));
            if (!environment.Il2Cpp)
                issues.Add(Error("Il2Cpp", "HumanVision Android runtime requires the IL2CPP scripting backend."));
            if (!environment.HasHumanVisionLibrary)
                issues.Add(Error("HasHumanVisionLibrary", "HumanVision Android runtime is missing libhumanvision.so for Android ARM64."));
            if (!environment.HasProfile)
                issues.Add(Error("HasProfile", "HumanVision Android runtime profile '" + descriptor.ProfileId + "' is missing."));

            if (descriptor.Id == "android-dual-vulkan-npu") {
                if (!environment.HasNeuralRuntimeAudit) issues.Add(Error("HasNeuralRuntimeAudit", "The dual build requires hash-audited ARM64 native and neural runtime libraries."));
                if (!environment.HasNeuralModelPackAssets) issues.Add(Error("HasNeuralModelPackAssets", "The dual build requires the admitted Low 512×288 neural ModelPack."));
                if (!environment.HasCpuRuntimeAudit) issues.Add(Error("HasCpuRuntimeAudit", "The dual build requires hash-audited ARM64 native and ONNX CPU runtime libraries."));
                if (!environment.HasCpuModelPackAssets) issues.Add(Error("HasCpuModelPackAssets", "The dual build requires both admitted Android CPU capacity ModelPacks."));
            }
            if (!descriptor.RequiresNcnn)
                return issues;

            if (environment.AutomaticGraphicsApis)
                issues.Add(Error("AutomaticGraphicsApis", "android-ncnn-vulkan requires Auto Graphics API to be disabled."));
            if (!environment.VulkanAvailable)
                issues.Add(Error("VulkanAvailable", "android-ncnn-vulkan requires Vulkan in Android Graphics APIs."));
            if (!environment.VulkanFirst)
                issues.Add(Error("VulkanFirst", "android-ncnn-vulkan requires Vulkan to be the first Android Graphics API."));
            if (!environment.HasNcnnLibrary)
                issues.Add(Error("HasNcnnLibrary", "android-ncnn-vulkan requires the Android ARM64 ncnn library."));
            if (!environment.HasBridgeSymbolManifest)
                issues.Add(Error("HasBridgeSymbolManifest", "android-ncnn-vulkan requires the GPU bridge symbol manifest."));
            if (!environment.HasNcnnModelPackAssets)
                issues.Add(Error("HasNcnnModelPackAssets", "android-ncnn-vulkan requires schema-2 NCNN model-pack assets; Milestone C has not installed them."));
            if (!environment.HasNcnnModelPackSha256Index)
                issues.Add(Error("HasNcnnModelPackSha256Index", "android-ncnn-vulkan requires the schema-2 NCNN model-pack SHA-256 index."));
            if (!environment.AutomaticGraphicsApis && environment.VulkanFirst && environment.OpenGlesAvailable)
                issues.Add(new AndroidBuildValidationIssue("OpenGlesCompatibility",
                    "android-ncnn-vulkan keeps OpenGLES as a compatibility Graphics API; remove it for a Vulkan-only production package.", false));

            return issues;
        }

        private static AndroidBuildValidationIssue Error(string code, string message)
        {
            return new AndroidBuildValidationIssue(code, message, true);
        }
    }
}
