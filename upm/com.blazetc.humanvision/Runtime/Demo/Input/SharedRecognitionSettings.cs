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
            if (Version != 1 || string.IsNullOrWhiteSpace(AnalysisProfileId) || string.IsNullOrWhiteSpace(ModelPackId) ||
                DetectorWidth < 0 || DetectorHeight < 0 || PoseWidth < 1 || PoseHeight < 1 || DetectionCadence < 1)
                throw new ArgumentException("Analysis settings require a Profile/ModelPack input contract.");
            new HumanVisionCameraSettings { people = MaxBodies, useRegions = UseRegions, regions = Regions }.Validate();
        }
    }
}
