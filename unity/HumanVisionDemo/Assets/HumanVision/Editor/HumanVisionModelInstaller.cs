using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace HumanVision.Editor
{
    [InitializeOnLoad]
    public sealed class HumanVisionModelInstaller : IPreprocessBuildWithReport
    {
        public int callbackOrder => -100;
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path; public string sha256; }
        [Serializable] private sealed class StageReceipt { public int schema_version; public string stage_id; public bool local_evaluation_only; public string index_sha256; }
        static HumanVisionModelInstaller() { EditorApplication.delayCall += Prepare; }
        [MenuItem("HumanVision/Install Packaged Models")]
        public static void Prepare()
        {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.blazetc.humanvision/package.json");
            if (package == null || string.IsNullOrEmpty(package.resolvedPath)) return;
            string models = Path.Combine(package.resolvedPath, "RuntimeData");
            if (!Directory.Exists(models)) throw new BuildFailedException("HumanVision UPM model directory is missing. Reinstall the complete package.");
            string target = "Assets/StreamingAssets/HumanVision/Runtime";
            if (PreserveExplicitStage(target)) return;
            Directory.CreateDirectory(target);
            bool changed = false;
            string indexPath = Path.Combine(models, "index.json");
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(indexPath));
            if (index == null || index.files == null) throw new BuildFailedException("Invalid HumanVision runtime index");
            foreach (var entry in index.files) {
                string name = entry.path;
                if (string.IsNullOrEmpty(name) || name.Contains("..") || name.StartsWith("/") || name.Contains(":") || name.Contains("\\"))
                    throw new BuildFailedException("Invalid HumanVision runtime data path");
                string source = Path.Combine(models, name), destination = Path.Combine(target, name);
                if (!File.Exists(source)) throw new BuildFailedException("HumanVision model missing: " + source);
                byte[] verifiedText = null;
                if (!string.Equals(Hash(source), entry.sha256, StringComparison.OrdinalIgnoreCase)) verifiedText = ReadVerifiedText(source, entry.sha256);
                if (File.Exists(destination) && string.Equals(Hash(destination), entry.sha256, StringComparison.OrdinalIgnoreCase)) continue;
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                if (verifiedText == null) File.Copy(source, destination + ".tmp", true);
                else File.WriteAllBytes(destination + ".tmp", verifiedText);
                File.Copy(destination + ".tmp", destination, true); File.Delete(destination + ".tmp"); changed = true;
            }
            string targetIndex = Path.Combine(target, "index.json");
            if (!File.Exists(targetIndex) || Hash(targetIndex) != Hash(indexPath)) { File.Copy(indexPath, targetIndex, true); changed = true; }
            if (changed) AssetDatabase.Refresh();
        }
        private static bool PreserveExplicitStage(string target)
        {
            string receiptPath = Path.Combine(target, "staged-runtime.json");
            if (!File.Exists(receiptPath)) return false;
            try {
                var receipt = JsonUtility.FromJson<StageReceipt>(File.ReadAllText(receiptPath));
                if (receipt == null || receipt.schema_version != 1 || receipt.stage_id != "local-qualified-runtime" ||
                    !receipt.local_evaluation_only || !ValidHash(receipt.index_sha256))
                    throw new InvalidDataException("Unsupported explicit local runtime stage receipt.");
                string indexPath = Path.Combine(target, "index.json");
                if (!File.Exists(indexPath) || !string.Equals(Hash(indexPath), receipt.index_sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Explicit local runtime index is missing or differs from its receipt hash.");
                var index = JsonUtility.FromJson<Index>(File.ReadAllText(indexPath));
                if (index == null || string.IsNullOrEmpty(index.version) || index.files == null || index.files.Length == 0)
                    throw new InvalidDataException("Explicit local runtime index is empty or invalid.");
                string root = Path.GetFullPath(target) + Path.DirectorySeparatorChar;
                var names = new System.Collections.Generic.HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var entry in index.files) {
                    if (entry == null || string.IsNullOrEmpty(entry.path) || Path.IsPathRooted(entry.path) || entry.path.Contains("..") ||
                        entry.path.Contains(":") || entry.path.Contains("\\") || !ValidHash(entry.sha256) || !names.Add(entry.path))
                        throw new InvalidDataException("Explicit local runtime contains an unsafe or duplicate indexed path.");
                    string file = Path.GetFullPath(Path.Combine(target, entry.path));
                    if (!file.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(file) ||
                        !string.Equals(Hash(file), entry.sha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Explicit local runtime file is missing or differs from its index: " + entry.path);
                }
                return true;
            } catch (Exception error) {
                throw new BuildFailedException("Explicit local runtime stage validation failed: " + error.Message + " Re-stage the verified runtime. Existing files were preserved.");
            }
        }
        private static bool ValidHash(string value)
        {
            if (value == null || value.Length != 64) return false;
            foreach (char c in value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
            return true;
        }
        // Git can rewrite text newlines in a subfolder package. Accept only an
        // exact indexed hash after newline conversion; weights stay byte-strict.
        private static byte[] ReadVerifiedText(string path, string expected)
        {
            string extension = Path.GetExtension(path).ToLowerInvariant();
            if (extension == ".json" || extension == ".md") {
                var utf8 = new UTF8Encoding(false, true);
                string normalized = utf8.GetString(File.ReadAllBytes(path)).Replace("\r\n", "\n");
                foreach (string candidate in new[] { normalized, normalized.Replace("\n", "\r\n") }) {
                    byte[] bytes = utf8.GetBytes(candidate);
                    using (var sha = SHA256.Create()) {
                        string digest = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
                        if (string.Equals(digest, expected, StringComparison.OrdinalIgnoreCase)) return bytes;
                    }
                }
            }
            throw new BuildFailedException("HumanVision runtime hash mismatch: " + path + ". Reinstall the package; content differs from its index.");
        }
        private static string Hash(string path)
        {
            using (var sha = SHA256.Create()) using (var stream = File.OpenRead(path)) return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        }
        public void OnPreprocessBuild(BuildReport report) { Prepare(); }
    }
}
