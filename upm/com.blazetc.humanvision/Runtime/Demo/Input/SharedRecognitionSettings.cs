using System;
using UnityEngine;

namespace HumanVision.Demo
{
    [Serializable]
    public sealed class SharedRecognitionSettings
    {
        public int Version = 1;
        public int MaxBodies = 4;
        public bool UseRegions;
        public Rect[] Regions = Array.Empty<Rect>();
        public string AnalysisProfileId = "android-ncnn-vulkan";
        public string ModelPackId = "precision-t-26-ncnn-fp16";
        public int DetectorWidth = 320, DetectorHeight = 320;
        public int PoseWidth = 192, PoseHeight = 256;
        public int DetectionCadence = 4;
        public bool UseWindowsCpu;
        public ModelInputQuality InputQuality = ModelInputQuality.Medium;
        public HumanVisionAccelerationMode AccelerationMode = HumanVisionAccelerationMode.Graphics;

        public SharedRecognitionSettings Clone()
        {
            var copy = (SharedRecognitionSettings)MemberwiseClone();
            copy.Regions = Regions == null ? null : (Rect[])Regions.Clone();
            return copy;
        }

        /// <summary>Validate the requested actual contract before retiring an active consumer.</summary>
        public AnalysisContract ResolveContract(string root, string baseProfile)
        {
            Validate();
            string profile = baseProfile;
            if (baseProfile == HumanVisionAndroidAccelerationSelection.CpuProfile) HumanVisionAndroidCpuModelContract.Validate(root);
            if (baseProfile == HumanVisionAndroidAccelerationSelection.NeuralProfile) {
                if (InputQuality != ModelInputQuality.Low) throw new InvalidOperationException("RK3588 NPU supports only Low 512×288.");
                HumanVisionNeuralModelContract.Validate(root);
            }
            if (baseProfile == HumanVisionModelInputQualities.AdmittedRuntimeMode) {
                var catalog = HumanVisionModelInputQualities.Load(root);
                var choices = catalog.ChoicesForMode(baseProfile);
                if (choices.Length != 0) profile = catalog.ResolveQuality(baseProfile, InputQuality);
                else if (InputQuality != ModelInputQuality.Medium)
                    throw new InvalidOperationException("The selected model input quality is missing from this build. Stage the same-mode quality catalog and ModelPacks, or choose Medium for the legacy profile.");
            }
            return AnalysisContract.Load(root, profile, MaxBodies);
        }

        public string RuntimeProfileFor(RuntimePlatform platform)
        {
            if (platform == RuntimePlatform.Android) return "auto";
            bool cpu = UseWindowsCpu || AccelerationMode == HumanVisionAccelerationMode.Cpu;
            if (platform == RuntimePlatform.WindowsEditor || platform == RuntimePlatform.WindowsPlayer)
                return DesktopProfile(cpu, InputQuality);
            return cpu ? "windows-pc-cpu" : "windows-pc-directml";
        }

        internal static string DesktopProfile(bool cpu, ModelInputQuality quality) =>
            (cpu ? "windows-yolo-cpu-" : "windows-yolo-directml-") + quality.ToString().ToLowerInvariant();
        internal static bool IsDesktopYolo(string profile) => profile.StartsWith("windows-yolo-", StringComparison.Ordinal);
        internal static bool IsDesktopCpu(string profile) => profile == "windows-pc-cpu" ||
            profile.StartsWith("windows-yolo-cpu-", StringComparison.Ordinal);

        public void ResizeRegions(int count)
        {
            var existing = new HumanVisionCameraSettings { people = MaxBodies, regions = Regions };
            existing.ResizeRegions(count);
            MaxBodies = existing.people; Regions = existing.regions;
        }

        public void Validate()
        {
            if (!Enum.IsDefined(typeof(HumanVisionAccelerationMode), AccelerationMode)) throw new ArgumentException("Invalid saved acceleration mode.");
            if (!Enum.IsDefined(typeof(ModelInputQuality), InputQuality))
                throw new ArgumentException("Invalid saved Model Input Quality; choose High, Medium or Low.");
            if (Version != 1 || string.IsNullOrWhiteSpace(AnalysisProfileId) || string.IsNullOrWhiteSpace(ModelPackId) ||
                DetectorWidth < 0 || DetectorHeight < 0 || PoseWidth < 1 || PoseHeight < 1 || DetectionCadence < 1)
                throw new ArgumentException("Analysis settings require a Profile/ModelPack input contract.");
            new HumanVisionCameraSettings { people = MaxBodies, useRegions = UseRegions, regions = Regions }.Validate();
        }
    }
}
