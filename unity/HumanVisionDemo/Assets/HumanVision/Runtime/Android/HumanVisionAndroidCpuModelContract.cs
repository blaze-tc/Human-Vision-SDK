using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using J = HumanVision.HumanVisionConfigurationJson;

namespace HumanVision
{
    // Exact CPU deployments, selected by pinned profile bytes. Read only during configuration.
    internal static class HumanVisionAndroidCpuModelContract
    {
        private static readonly Dictionary<string,string> Pinned = new Dictionary<string,string>(StringComparer.Ordinal) {
            { "profiles/android-cpu-nohands.json", "9969cf1b1a6a4400f21139b8c168188c92c39bcac2f964a19f2e0e3eb9090353" },
            { "modelpacks/precision-t-26/manifest.json", "df00e4d62ae0e276abebf5a16bbc4568824b734a2296e8ff1eb04df1960eb128" },
            { "modelpacks/precision-t-26/detector.onnx", "a6e9039b481218cc0cfcccf1d03243b68af34c736f2cb774dcca39afce2e99c7" },
            { "modelpacks/precision-t-26/body.onnx", "7d0075fbc44b2e696af21ac4555333fbb43405bf8dcc8d44c17c98d23818c710" },
            { "modelpacks/rtmo-t-416/manifest.json", "6ed2a7166bec589bb7aa5322b26c01cec2a5db3a0b691e92f503ecf6a3de0f90" },
            { "modelpacks/rtmo-t-416/body.onnx", "20aad6e2e42359cac1c5b4a0b2da00e29bfe91a72a782fdcf287d273a04c1b24" }
        };
        private static readonly Dictionary<string,string> YoloPinned = new Dictionary<string,string>(StringComparer.Ordinal) {
            { "profiles/android-cpu-nohands.json", "75351d8c1b627be12b8cfc09e2928e440849c9fe4c0386123166b2374e29b387" },
            { "modelpacks/yolov8n-pose-512x288-ort/manifest.json", "de9f7b09b51ce2eee7b48853f1345180c411fa4176a825b0e9642130c15a8765" },
            { "modelpacks/yolov8n-pose-512x288-ort/body.onnx", "6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3" }
        };
        private const string UltraPack = "yolov8n-pose-384x224-ort-experimental";
        private const string UltraProfilePath = "profiles/" + HumanVisionAndroidAccelerationSelection.CpuUltraLowProfile + ".json";
        private static readonly Dictionary<string,string> UltraPinned = new Dictionary<string,string>(StringComparer.Ordinal) {
            { UltraProfilePath, "65e5ea2ae4f96922e36cc8567c7c5e36d004b55084a9244beb1199c8b696be57" },
            { "modelpacks/" + UltraPack + "/manifest.json", "0ec9fb44f8830deaf55327026144f47b863e80bbf6df27957d6f115ce95a5797" },
            { "modelpacks/" + UltraPack + "/body.onnx", "faf6e723fc2813a6ff0a019392b97c670f9e6e6b313611a8bba17175befe16c3" }
        };
        private static Dictionary<string,string> ReadIndex(string root)
        {
            var index = J.Object(J.Parse(File.ReadAllText(Path.Combine(root,"index.json"))));
            var files = new Dictionary<string,string>(StringComparer.Ordinal);
            foreach(var value in J.Array(J.Field(index,"files"))) {
                var row=J.Object(value); string name=J.Text(row,"path");
                if(files.ContainsKey(name)) throw new InvalidDataException("Duplicate CPU runtime index path.");
                files.Add(name,J.Text(row,"sha256"));
            }
            return files;
        }
        internal static bool HasInstalledUltraLow(string root)
        {
            var files = ReadIndex(root);
            if (!files.ContainsKey(UltraProfilePath)) return false;
            Validate(root, files, HumanVisionAndroidAccelerationSelection.CpuUltraLowProfile);
            return true;
        }
        internal static void Validate(string root, string profile = HumanVisionAndroidAccelerationSelection.CpuProfile) =>
            Validate(root, ReadIndex(root), profile);
        private static void Validate(string root, Dictionary<string,string> files, string profile)
        {
            string fullRoot=Path.GetFullPath(root);
            bool ultra = profile == HumanVisionAndroidAccelerationSelection.CpuUltraLowProfile;
            bool yolo=files.TryGetValue("profiles/android-cpu-nohands.json",out string profileHash) &&
                profileHash==YoloPinned["profiles/android-cpu-nohands.json"];
            foreach(var expected in ultra ? UltraPinned : yolo ? YoloPinned : Pinned) {
                if(!files.TryGetValue(expected.Key,out string indexed) || indexed != expected.Value)
                    throw new InvalidDataException("CPU profile/model differs from the admitted deployment: "+expected.Key);
                string path=Path.Combine(fullRoot,expected.Key);
                if(!File.Exists(path)) throw new InvalidDataException("CPU runtime file is missing: "+expected.Key);
                for(string current=Path.GetFullPath(path);current.Length>fullRoot.Length;current=Path.GetDirectoryName(current))
                    if((File.Exists(current)||Directory.Exists(current)) && (File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)
                        throw new InvalidDataException("CPU runtime paths cannot use links/junctions.");
                using(var file=File.OpenRead(path)) using(var sha=SHA256.Create())
                    if(BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant()!=expected.Value)
                        throw new InvalidDataException("CPU runtime SHA-256 mismatch: "+expected.Key);
            }
            foreach(string pack in ultra ? new[] { UltraPack } : yolo ? new[] { "yolov8n-pose-512x288-ort" } : new[] { "precision-t-26","rtmo-t-416" })
                if(File.Exists(Path.Combine(fullRoot,"modelpacks",pack,"modelpack.json"))) throw new InvalidDataException("Ambiguous CPU ModelPack manifest.");
        }
    }
}
