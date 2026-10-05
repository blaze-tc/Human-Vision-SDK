using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using UnityEngine;
using UnityEngine.Networking;
namespace HumanVision
{
    // Data files are declared by the package index, never selected by model name.
    public static class HumanVisionRuntimeData
    {
        private sealed class Index { public string version; public Entry[] files; }
        private sealed class Entry { public string path; public string sha256; }
        public static IEnumerator Prepare(Action<string> ready, Action<string> failed)
        {
            string source = Application.streamingAssetsPath + "/HumanVision/Runtime/";
            Index index;
            string indexJson;
            using (var request = UnityWebRequest.Get(Url(source + "index.json"))) {
                yield return request.SendWebRequest();
                if (request.result != UnityWebRequest.Result.Success) { failed("Runtime data index missing: " + request.error); yield break; }
                try { indexJson = request.downloadHandler.text; index = ReadIndex(indexJson); }
                catch (Exception e) { failed(e.Message); yield break; }
            }
            string root = Path.Combine(Application.persistentDataPath, "HumanVisionRuntime");
            foreach (var entry in index.files) {
                string target;
                bool cached;
                try {
                    if (entry == null || string.IsNullOrEmpty(entry.path) || entry.path.StartsWith("/") || entry.path.Contains("..") || entry.path.Contains(":") || entry.path.Contains("\\") || entry.sha256 == null || entry.sha256.Length != 64)
                        throw new InvalidDataException("Unsafe runtime data entry");
                    target = Path.Combine(root, entry.path); Directory.CreateDirectory(Path.GetDirectoryName(target));
                    cached = File.Exists(target) && Hash(target) == entry.sha256.ToLowerInvariant();
                } catch (Exception e) { failed(e.Message); yield break; }
                if (cached) continue;
                using (var request = UnityWebRequest.Get(Url(source + entry.path))) {
                    request.downloadHandler = new DownloadHandlerFile(target + ".partial");
                    yield return request.SendWebRequest();
                    if (request.result != UnityWebRequest.Result.Success) { failed("Runtime data extraction failed: " + entry.path + ": " + request.error); yield break; }
                }
                try {
                    if (Hash(target + ".partial") != entry.sha256.ToLowerInvariant()) throw new InvalidDataException("Runtime data hash mismatch: " + entry.path);
                    File.Copy(target + ".partial", target, true); File.Delete(target + ".partial");
                } catch (Exception e) { failed(e.Message); yield break; }
            }
            // Publish the source index only after every declared asset has passed
            // its hash check. Configuration can then validate the extracted closure.
            try { PublishIndex(root, indexJson); }
            catch (Exception e) { failed("Runtime data index publication failed: " + e.Message); yield break; }
            ready(root);
        }
        private static Index ReadIndex(string json)
        {
            var value = HumanVisionConfigurationJson.Object(HumanVisionConfigurationJson.Parse(json));
            var version = HumanVisionConfigurationJson.Text(value, "version");
            var rows = HumanVisionConfigurationJson.Array(HumanVisionConfigurationJson.Field(value, "files"));
            if (string.IsNullOrWhiteSpace(version) || rows.Count == 0) throw new InvalidDataException("Invalid runtime data index.");
            var entries = new List<Entry>(); var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var item in rows)
            {
                var row = HumanVisionConfigurationJson.Object(item); HumanVisionConfigurationJson.Keys(row, "path", "sha256");
                var path = HumanVisionConfigurationJson.Text(row, "path"); var hash = HumanVisionConfigurationJson.Text(row, "sha256");
                if (string.IsNullOrEmpty(path) || path.StartsWith("/") || path.Contains("..") || path.Contains(":") || path.Contains("\\") || path == "index.json" || Array.Exists(path.Split('/'), part => part == "" || part == ".") ||
                    !System.Text.RegularExpressions.Regex.IsMatch(hash, "^[a-f0-9]{64}$") || !paths.Add(path)) throw new InvalidDataException("Unsafe/duplicate runtime data index entry.");
                entries.Add(new Entry { path = path, sha256 = hash });
            }
            return new Index { version = version, files = entries.ToArray() };
        }
        private static void PublishIndex(string root, string json)
        {
            Directory.CreateDirectory(root);
            string target = Path.Combine(root, "index.json"), temporary = target + ".pending-" + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllText(temporary, json, new System.Text.UTF8Encoding(false));
                if (File.Exists(target)) File.Replace(temporary, target, null);
                else File.Move(temporary, target);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }
        private static string Url(string path) => path.Contains("://") ? path : new Uri(path).AbsoluteUri;
        private static string Hash(string path)
        { using (var file = File.OpenRead(path)) using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant(); }
    }
}
