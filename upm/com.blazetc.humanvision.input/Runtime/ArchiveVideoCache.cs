using System;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEngine.Networking;

namespace HumanVision.Input
{
    /// <summary>
    /// Android APK assets are archive URLs, which some device VideoPlayer backends cannot open.
    /// Stream them to an app-private file before decoding; never load the whole video into RAM.
    /// A length receipt is written only after a successful transfer and atomic file rename.
    /// </summary>
    internal static class ArchiveVideoCache
    {
        internal static bool RequiresExtraction(string url) =>
            url != null && url.StartsWith("jar:file:", StringComparison.OrdinalIgnoreCase);

        internal static string Destination(string root, string url)
        {
            using (var hash = SHA256.Create())
            {
                byte[] digest = hash.ComputeHash(Encoding.UTF8.GetBytes(url));
                var name = new StringBuilder(digest.Length * 2);
                foreach (byte value in digest) name.Append(value.ToString("x2", CultureInfo.InvariantCulture));
                // The archive URL, including APK identity, is hashed; no caller path enters the filename.
                return Path.Combine(Path.GetFullPath(root), name + ".mp4");
            }
        }

        internal static bool IsComplete(string path)
        {
            try
            {
                return File.Exists(path) && File.Exists(path + ".length") &&
                    long.TryParse(File.ReadAllText(path + ".length"), NumberStyles.None,
                        CultureInfo.InvariantCulture, out long expected) && expected > 0 &&
                    new FileInfo(path).Length == expected;
            }
            catch (IOException) { return false; }
            catch (UnauthorizedAccessException) { return false; }
        }

        /// <summary>Owns exactly one pending request and its unique partial file. Close explicitly aborts it.</summary>
        internal sealed class Transfer : IDisposable
        {
            private UnityWebRequest request;
            private readonly string part;
            internal readonly string DestinationPath;
            internal bool IsDone => request == null || request.isDone;

            internal Transfer(string url, string destination)
            {
                DestinationPath = destination;
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                part = destination + "." + Guid.NewGuid().ToString("N") + ".part";
                try
                {
                    request = UnityWebRequest.Get(url);
                    request.downloadHandler = new DownloadHandlerFile(part) { removeFileOnAbort = true };
                    request.SendWebRequest();
                }
                catch { Dispose(); throw; }
            }

            internal string Commit()
            {
                if (request == null || !request.isDone || request.result != UnityWebRequest.Result.Success)
                    throw new IOException("Cannot extract bundled video: " + (request?.error ?? "transfer cancelled"));
                long length = new FileInfo(part).Length;
                if (length <= 0 || request.downloadedBytes != (ulong)length)
                    throw new IOException("Bundled video extraction is incomplete; bytes=" + length);
                // A crash before the receipt leaves an invalid cache, which is replaced on the next open.
                File.Delete(DestinationPath + ".length");
                File.Delete(DestinationPath);
                File.Move(part, DestinationPath);
                File.WriteAllText(DestinationPath + ".length", length.ToString(CultureInfo.InvariantCulture));
                return DestinationPath;
            }

            public void Dispose()
            {
                if (request != null)
                {
                    if (!request.isDone) request.Abort();
                    request.Dispose();
                    request = null;
                }
                try { File.Delete(part); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }
}
