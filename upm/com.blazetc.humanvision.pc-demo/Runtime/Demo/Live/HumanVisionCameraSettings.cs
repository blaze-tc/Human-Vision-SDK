using System;
using System.IO;
using UnityEngine;

namespace HumanVision
{
    public enum HumanVisionCameraKind { WebCamera, Rtsp }

    [Serializable]
    public sealed class HumanVisionCameraSettings
    {
        public int version = 1;
        public HumanVisionCameraKind source = HumanVisionCameraKind.WebCamera;
        public string deviceName = "";
        public string rtspUrl = "";
        public bool rtspTcp = true;
        public int width = 1280, height = 720, framesPerSecond = 30;
        public bool mirror;
        public int people = 4;
        public bool useRegions;
        // Top-left normalized image coordinates; array index is the permanent slot.
        public Rect[] regions = Array.Empty<Rect>();

        public void ResizeRegions(int count)
        {
            if (count < 1 || count > 8) throw new ArgumentOutOfRangeException(nameof(count), "Choose 1 to 8 people.");
            people = count;
            if (regions != null && regions.Length == count) return;
            regions = new Rect[count];
            for (int i = 0; i < count; i++) regions[i] = new Rect((float)i / count, 0, 1f / count, 1);
        }

        public void Validate()
        {
            if (version != 1 || people < 1 || people > 8 || width < 64 || height < 64 ||
                width > 4096 || height > 4096 || framesPerSecond < 1 || framesPerSecond > 120)
                throw new ArgumentException("Invalid camera settings, dimensions or people count.");
            if (!Enum.IsDefined(typeof(HumanVisionCameraKind), source)) throw new ArgumentException("Unknown camera source.");
            if (useRegions) {
                if (regions == null || regions.Length != people) throw new ArgumentException("One region per person is required.");
                for (int i = 0; i < regions.Length; i++) {
                    Rect r = regions[i];
                    if (float.IsNaN(r.x + r.y + r.width + r.height) || float.IsInfinity(r.x + r.y + r.width + r.height) ||
                        r.x < 0 || r.y < 0 || r.width < .01f || r.height < .01f || r.xMax > 1.000001f || r.yMax > 1.000001f)
                        throw new ArgumentException("Regions must be inside the image and at least 1% wide/high.");
                    for (int j = 0; j < i; j++) {
                        Rect q = regions[j];
                        if (Mathf.Min(r.xMax, q.xMax) - Mathf.Max(r.x, q.x) > .000001f &&
                            Mathf.Min(r.yMax, q.yMax) - Mathf.Max(r.y, q.y) > .000001f)
                            throw new ArgumentException("Regions overlap. Separate them before applying.");
                    }
                }
            }
        }

        public static string FilePath => Path.Combine(Application.persistentDataPath, "HumanVisionCamera.json");
        public void Save()
        {
            Validate();
            string temp = FilePath + ".tmp";
            File.WriteAllText(temp, JsonUtility.ToJson(this, true));
            if (File.Exists(FilePath)) File.Copy(FilePath, FilePath + ".bak", true);
            File.Copy(temp, FilePath, true);
            File.Delete(temp);
        }
        public static HumanVisionCameraSettings Load()
        {
            if (!File.Exists(FilePath)) return new HumanVisionCameraSettings();
            var settings = JsonUtility.FromJson<HumanVisionCameraSettings>(File.ReadAllText(FilePath));
            if (settings == null) throw new InvalidDataException("Camera settings are empty.");
            settings.Validate();
            return settings;
        }
    }
}
