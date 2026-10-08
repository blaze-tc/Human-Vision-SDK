using System;
using System.IO;
using UnityEngine;

namespace HumanVision.Demo
{
    /// <summary>单文件原子保存三个模式。验证失败不写磁盘；损坏配置不自动覆盖。</summary>
    public static class HumanVisionSdkSettingsStore
    {
        public static string DefaultPath => Path.Combine(Application.persistentDataPath, "HumanVisionSdkSettings", "settings.json");
        /// <summary>不存在时返回默认草稿；损坏时抛异常，由界面提示并提供显式备份恢复。</summary>
        public static HumanVisionSettingsData Load(string path)
        {
            if (!File.Exists(path)) return new HumanVisionSettingsData();
            string json = File.ReadAllText(path);
            if (!json.Contains("\"Version\"") || !json.Contains("\"Recognition\"")) throw new InvalidDataException("缺少设置版本或识别配置。");
            var value = JsonUtility.FromJson<HumanVisionSettingsData>(json);
            if (value == null) throw new InvalidDataException("配置为空。"); value.Validate(); return value;
        }
        /// <summary>先验证再写临时文件；替换原件时保留 .bak。</summary>
        public static void Save(string path, HumanVisionSettingsData value)
        {
            if (value == null) throw new ArgumentNullException(nameof(value)); value.Validate();
            string absolute = Path.GetFullPath(path); Directory.CreateDirectory(Path.GetDirectoryName(absolute));
            string temporary = absolute + ".tmp";
            using (var stream = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
            using (var writer = new StreamWriter(stream)) { writer.Write(JsonUtility.ToJson(value, true)); writer.Flush(); stream.Flush(true); }
            if (!File.Exists(absolute)) { File.Move(temporary, absolute); return; }
            try { File.Replace(temporary, absolute, absolute + ".bak"); }
            catch (PlatformNotSupportedException) {
                File.Copy(absolute, absolute + ".bak", true);
                File.Delete(absolute);
                try { File.Move(temporary, absolute); }
                catch { File.Copy(absolute + ".bak", absolute, true); throw; }
            }
        }
    }
}
