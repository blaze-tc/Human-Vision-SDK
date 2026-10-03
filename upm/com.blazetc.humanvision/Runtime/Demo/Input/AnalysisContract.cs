using System;
using System.IO;
using UnityEngine;

namespace HumanVision.Demo
{
    /// <summary>Read only when initializing or applying settings, never during frame processing.</summary>
    public sealed class AnalysisContract
    {
        [Serializable] private sealed class Profile { public string profile; public Capacity[] body_by_capacity; public Capacity body; public Detector detector; }
        [Serializable] private sealed class Detector { public int cadence_interval_frames; }
        [Serializable] private sealed class Capacity { public int max_people; public string modelPack; }
        [Serializable] private sealed class Pack { public string pack_id; public Model[] models; public int detection_interval; }
        [Serializable] private sealed class Model { public string role; public Geometry input_contract; }
        [Serializable] private sealed class Geometry { public int width, height; }
        public string ProfileId { get; private set; }
        public string ModelPackId { get; private set; }
        public int DetectorWidth { get; private set; }
        public int DetectorHeight { get; private set; }
        public int PoseWidth { get; private set; }
        public int PoseHeight { get; private set; }
        public int DetectionCadence { get; private set; } = 1;
        public static AnalysisContract Load(string root, string profileId, int capacity)
        {
            if (capacity < 1 || capacity > 8) throw new ArgumentOutOfRangeException(nameof(capacity));
            ValidateName(profileId);
            var profile = JsonUtility.FromJson<Profile>(File.ReadAllText(Path.Combine(root, "profiles", profileId + ".json")));
            if (profile == null || profile.profile != profileId) throw new InvalidDataException("Profile contract is invalid.");
            string selected = profile.body?.modelPack;
            if (profile.body_by_capacity != null)
                foreach (var entry in profile.body_by_capacity) if (entry != null && entry.max_people >= capacity) { selected = entry.modelPack; break; }
            if (selected == null) throw new InvalidDataException("Profile has no ModelPack for the requested people count.");
            ValidateName(selected);
            string packRoot = Path.Combine(root, "modelpacks", selected);
            string manifest = Path.Combine(packRoot, "manifest.json"), modelpack = Path.Combine(packRoot, "modelpack.json");
            bool hasManifest = File.Exists(manifest), hasModelpack = File.Exists(modelpack);
            if (hasManifest && hasModelpack)
                throw new InvalidDataException("ModelPack " + selected + " has both manifest.json and modelpack.json; keep exactly one manifest.");
            if (!hasManifest && !hasModelpack)
                throw new InvalidDataException("ModelPack " + selected + " is missing manifest.json or modelpack.json; stage the selected ModelPack.");
            // Native initialization validates schema, hashes, decoding and preprocessing.
            // The Demo reads the admitted pack's geometry without changing its model contract.
            var pack = JsonUtility.FromJson<Pack>(File.ReadAllText(hasManifest ? manifest : modelpack));
            if (pack == null || pack.pack_id != selected || pack.models == null) throw new InvalidDataException("ModelPack contract is invalid.");
            var result = new AnalysisContract { ProfileId = profileId, ModelPackId = selected, DetectionCadence = Math.Max(1, profile.detector?.cadence_interval_frames ?? 1) };
            foreach (var model in pack.models) {
                if (model?.input_contract == null) continue;
                if (model.role == "detector") { result.DetectorWidth = model.input_contract.width; result.DetectorHeight = model.input_contract.height; }
                if (model.role == "body") { result.PoseWidth = model.input_contract.width; result.PoseHeight = model.input_contract.height; }
            }
            if (result.PoseWidth <= 0 || result.PoseHeight <= 0) throw new InvalidDataException("ModelPack has no body input geometry.");
            return result;
        }
        public void ApplyTo(SharedRecognitionSettings settings)
        {
            settings.AnalysisProfileId = ProfileId; settings.ModelPackId = ModelPackId;
            settings.DetectorWidth = DetectorWidth; settings.DetectorHeight = DetectorHeight;
            settings.PoseWidth = PoseWidth; settings.PoseHeight = PoseHeight;
            settings.DetectionCadence = DetectionCadence;
        }
        public void Validate(SharedRecognitionSettings settings)
        {
            settings.Validate();
            if (settings.AnalysisProfileId != ProfileId || settings.ModelPackId != ModelPackId ||
                settings.DetectorWidth != DetectorWidth || settings.DetectorHeight != DetectorHeight ||
                settings.PoseWidth != PoseWidth || settings.PoseHeight != PoseHeight || settings.DetectionCadence != DetectionCadence)
                throw new ArgumentException("Analysis resolution must match the selected Profile/ModelPack manifest; capture requests are independent.");
        }
        private static void ValidateName(string name)
        {
            if (string.IsNullOrWhiteSpace(name) || name.Contains("..") || name.IndexOfAny(new[] {'/', '\\', ':'}) >= 0)
                throw new InvalidDataException("Invalid profile or ModelPack identifier.");
        }
    }
}
