using System;
using System.Collections;
using System.IO;
using System.Security.Cryptography;
using UnityEngine;
using UnityEngine.Networking;
namespace HumanVision
{
    // Data files are declared by the package index, never selected by model name.
    public static class HumanVisionRuntimeData
    {
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path; public string sha256; }
        public static IEnumerator Prepare(Action<string> ready, Action<string> failed)
        {
            string source = Application.streamingAssetsPath + "/HumanVision/Runtime/";
            Index index;
            using (var request = UnityWebRequest.Get(Url(source + "index.json"))) {
                yield return request.SendWebRequest();
                if (request.result != UnityWebRequest.Result.Success) { failed("Runtime data index missing: " + request.error); yield break; }
                try { index = JsonUtility.FromJson<Index>(request.downloadHandler.text);
                    if (index == null || index.files == null || string.IsNullOrEmpty(index.version)) throw new InvalidDataException("Invalid runtime data index"); }
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
            ready(root);
        }
        private static string Url(string path) => path.Contains("://") ? path : new Uri(path).AbsoluteUri;
        private static string Hash(string path)
        { using (var file = File.OpenRead(path)) using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant(); }
    }
}
