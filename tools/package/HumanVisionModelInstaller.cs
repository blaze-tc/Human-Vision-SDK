using System;
using System.IO;
using System.Security.Cryptography;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;

namespace HumanVision.Editor
{
    [InitializeOnLoad]
    public sealed class HumanVisionModelInstaller : IPreprocessBuildWithReport
    {
        public int callbackOrder => -100;
        static HumanVisionModelInstaller() { EditorApplication.delayCall += Prepare; }
        [MenuItem("HumanVision/Install Packaged Models")]
        public static void Prepare()
        {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.blazetc.humanvision/package.json");
            if (package == null || string.IsNullOrEmpty(package.resolvedPath)) return;
            string models = Path.Combine(package.resolvedPath, "Models");
            if (!Directory.Exists(models)) throw new BuildFailedException("HumanVision UPM model directory is missing. Reinstall the complete package.");
            string target = "Assets/StreamingAssets/HumanVision/Models";
            Directory.CreateDirectory(target);
            bool changed = false;
            foreach (string name in new [] { "rtmdet_tiny_person_640.onnx", "rtmpose_s_133.onnx" }) {
                string source = Path.Combine(models, name), destination = Path.Combine(target, name);
                if (!File.Exists(source)) throw new BuildFailedException("HumanVision model missing: " + source);
                if (File.Exists(destination) && Hash(source) == Hash(destination)) continue;
                File.Copy(source, destination + ".tmp", true);
                File.Copy(destination + ".tmp", destination, true); File.Delete(destination + ".tmp"); changed = true;
            }
            if (changed) AssetDatabase.Refresh();
        }
        private static string Hash(string path)
        {
            using (var sha = SHA256.Create()) using (var stream = File.OpenRead(path)) return BitConverter.ToString(sha.ComputeHash(stream));
        }
        public void OnPreprocessBuild(BuildReport report) { Prepare(); }
    }
}
