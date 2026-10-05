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
            if (baseProfile == HumanVisionModelInputQualities.AdmittedRuntimeMode) {
                var catalog = HumanVisionModelInputQualities.Load(root);
                var choices = catalog.ChoicesForMode(baseProfile);
                if (choices.Length != 0) profile = catalog.ResolveQuality(baseProfile, InputQuality);
                else if (InputQuality != ModelInputQuality.Medium)
                    throw new InvalidOperationException("The selected model input quality is missing from this build. Stage the same-mode quality catalog and ModelPacks, or choose Medium for the legacy profile.");
            }
            // Unsupported PC/ORT profiles preserve their real contract and the saved Android choice.
            return AnalysisContract.Load(root, profile, MaxBodies);
        }

        public string RuntimeProfileFor(RuntimePlatform platform)
        {
            return platform == RuntimePlatform.Android ? "auto" : UseWindowsCpu ? "windows-pc-cpu" : "windows-pc-directml";
        }

        public void ResizeRegions(int count)
        {
            var existing = new HumanVisionCameraSettings { people = MaxBodies, regions = Regions };
            existing.ResizeRegions(count);
            MaxBodies = existing.people; Regions = existing.regions;
        }

        public void Validate()
        {
            if (!Enum.IsDefined(typeof(ModelInputQuality), InputQuality))
                throw new ArgumentException("Invalid saved Model Input Quality; choose High, Medium or Low.");
            if (Version != 1 || string.IsNullOrWhiteSpace(AnalysisProfileId) || string.IsNullOrWhiteSpace(ModelPackId) ||
                DetectorWidth < 0 || DetectorHeight < 0 || PoseWidth < 1 || PoseHeight < 1 || DetectionCadence < 1)
                throw new ArgumentException("Analysis settings require a Profile/ModelPack input contract.");
            new HumanVisionCameraSettings { people = MaxBodies, useRegions = UseRegions, regions = Regions }.Validate();
        }
    }
}
