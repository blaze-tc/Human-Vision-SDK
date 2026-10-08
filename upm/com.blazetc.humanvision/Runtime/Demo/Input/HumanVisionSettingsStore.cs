using System;
using System.IO;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision.Demo
{
    public sealed class HumanVisionSettingsStore
    {
        private readonly string directory;
        public HumanVisionSettingsStore(string directory = null)
        {
            this.directory = directory ?? Path.Combine(Application.persistentDataPath, "HumanVisionUnifiedInput");
        }
        private string SharedPath => Path.Combine(directory, "shared.json");
        private string ModePath(InputKind kind)
        {
            if (!Enum.IsDefined(typeof(InputKind), kind)) throw new ArgumentOutOfRangeException(nameof(kind));
            return Path.Combine(directory, kind + ".json");
        }
        public SharedRecognitionSettings LoadShared()
        {
            var value = Read<SharedRecognitionSettings>(SharedPath) ?? new SharedRecognitionSettings();
            value.Validate(); return value;
        }
        public void SaveShared(SharedRecognitionSettings value) { value.Validate(); Write(SharedPath, value); }
        public DemoModeSettings LoadMode(InputKind kind)
        {
            var value = Read<DemoModeSettings>(ModePath(kind)) ?? new DemoModeSettings();
            value.Validate(); return value;
        }
        public void SaveMode(InputKind kind, DemoModeSettings value) { value.Validate(); Write(ModePath(kind), value); }
        private static T Read<T>(string path) where T : class
        {
            if (!File.Exists(path)) return null;
            return JsonUtility.FromJson<T>(File.ReadAllText(path)) ?? throw new InvalidDataException("Settings file is empty: " + Path.GetFileName(path));
        }
        private void Write<T>(string path, T value)
        {
            Directory.CreateDirectory(directory);
            string temporary = path + ".tmp";
            File.WriteAllText(temporary, JsonUtility.ToJson(value, true));
            if (File.Exists(path)) File.Replace(temporary, path, path + ".bak");
            else File.Move(temporary, path);
        }
    }
}
