using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using J = HumanVision.HumanVisionConfigurationJson;

namespace HumanVision
{
    // Exact existing CPU deployment, including both capacity branches. Read only during configuration.
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
        internal static void Validate(string root)
        {
            var index = J.Object(J.Parse(File.ReadAllText(Path.Combine(root,"index.json"))));
            var files = new Dictionary<string,string>(StringComparer.Ordinal);
            foreach(var value in J.Array(J.Field(index,"files"))) {
                var row=J.Object(value); string name=J.Text(row,"path");
                if(files.ContainsKey(name)) throw new InvalidDataException("Duplicate CPU runtime index path.");
                files.Add(name,J.Text(row,"sha256"));
            }
            string fullRoot=Path.GetFullPath(root);
            foreach(var expected in Pinned) {
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
            foreach(string pack in new[] { "precision-t-26","rtmo-t-416" })
                if(File.Exists(Path.Combine(fullRoot,"modelpacks",pack,"modelpack.json"))) throw new InvalidDataException("Ambiguous CPU ModelPack manifest.");
        }
    }
}
