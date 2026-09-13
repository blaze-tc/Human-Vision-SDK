using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace HumanVision.Editor
{
    public sealed class HumanVisionAndroidRuntimeModeDescriptor
    {
        public string Id { get; }
        public string DisplayName { get; }
        public string ProfileId { get; }
        public bool RequiresVulkan { get; }
        public bool RequiresGpuBridge { get; }
        public bool RequiresNcnn { get; }
        public IReadOnlyList<string> RequiredCapabilities { get; }

        internal HumanVisionAndroidRuntimeModeDescriptor(
            string id,
            string displayName,
            string profileId,
            bool requiresVulkan,
            bool requiresGpuBridge,
            bool requiresNcnn,
            IEnumerable<string> requiredCapabilities)
        {
            Id = id;
            DisplayName = displayName;
            ProfileId = profileId;
            RequiresVulkan = requiresVulkan;
            RequiresGpuBridge = requiresGpuBridge;
            RequiresNcnn = requiresNcnn;
            RequiredCapabilities = new ReadOnlyCollection<string>(requiredCapabilities.ToArray());
        }
    }

    public static class HumanVisionAndroidRuntimeModeRegistry
    {
        private static readonly IReadOnlyList<HumanVisionAndroidRuntimeModeDescriptor> modes =
            new ReadOnlyCollection<HumanVisionAndroidRuntimeModeDescriptor>(new[]
            {
                new HumanVisionAndroidRuntimeModeDescriptor(
                    "android-ncnn-vulkan",
                    "NCNN Vulkan",
                    "android-ncnn-vulkan",
                    requiresVulkan: true,
                    requiresGpuBridge: true,
                    requiresNcnn: true,
                    new[] { "body_pose", "multi_person", "gpu_input", "vulkan", "fp16" }),
                new HumanVisionAndroidRuntimeModeDescriptor(
                    "android-ort-xnnpack",
                    "ORT XNNPACK",
                    "android-ort-xnnpack",
                    requiresVulkan: false,
                    requiresGpuBridge: false,
                    requiresNcnn: false,
                    new[] { "body_pose", "multi_person", "cpu_input" }),
                new HumanVisionAndroidRuntimeModeDescriptor(
                    "android-ort-cpu",
                    "ORT CPU",
                    "android-ort-cpu",
                    requiresVulkan: false,
                    requiresGpuBridge: false,
                    requiresNcnn: false,
                    new[] { "body_pose", "multi_person", "cpu_input" })
            });

        private static readonly Dictionary<string, HumanVisionAndroidRuntimeModeDescriptor> byId =
            modes.ToDictionary(x => x.Id, StringComparer.Ordinal);

        public static IReadOnlyList<HumanVisionAndroidRuntimeModeDescriptor> All => modes;

        public static HumanVisionAndroidRuntimeModeDescriptor Resolve(string id)
        {
            HumanVisionAndroidRuntimeModeDescriptor descriptor;
            if (id == null || !byId.TryGetValue(id, out descriptor))
                throw new InvalidOperationException(
                    $"Unknown Android runtime mode '{id ?? "<null>"}'. Select one of: {string.Join(", ", byId.Keys)}.");
            return descriptor;
        }
    }
}
