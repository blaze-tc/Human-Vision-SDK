using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// Android 10+ 将本次日志自动同步成 Downloads 中的普通文件夹，不需要解压或访问 Android/data。
    /// 使用 MediaStore 写应用自己创建的文件；后台每五秒增量同步，保留私有原件和明确失败状态。
    /// </summary>
    public sealed class AndroidLogFolderMirror : IDisposable
    {
        private readonly string session, sdkRoot;
        private readonly HashSet<string> excluded;
        private readonly object gate = new object();
        private readonly AutoResetEvent request = new AutoResetEvent(false);
        private Thread worker;
        private volatile bool stopping;
        private string publicDirectory = "", status = "公共日志文件夹准备中";
        public string PublicDirectory { get { lock (gate) return publicDirectory; } }
        public string Status { get { lock (gate) return status; } }
        public AndroidLogFolderMirror(string sessionPath, string sdkLogRoot, IEnumerable<string> priorSessions)
        {
            session = sessionPath; sdkRoot = sdkLogRoot; excluded = new HashSet<string>(priorSessions, StringComparer.OrdinalIgnoreCase);
        }
        public void Start()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            worker = new Thread(Run) { IsBackground = true, Name = "HV Downloads log folder" }; worker.Start();
#else
            lock (gate) status = "本机直接读取日志文件夹";
#endif
        }
        public void RequestSync() => request.Set();
#if UNITY_ANDROID && !UNITY_EDITOR
        private sealed class FileCopy : IDisposable
        {
            public AndroidJavaObject Uri;
            public long Position;
            public bool NeedsRebuild;
            public byte[] Prefix = Array.Empty<byte>();
            public void Dispose() => Uri?.Dispose();
        }
        private void Run()
        {
            AndroidJNI.AttachCurrentThread();
            var entries = new Dictionary<string, FileCopy>(StringComparer.OrdinalIgnoreCase);
            try {
                using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                    if (version.GetStatic<int>("SDK_INT") < 29) { lock (gate) status = "Android 10 以下：使用应用日志目录/ZIP"; return; }
                string relative = "Download/HumanVisionLogs/" + Path.GetFileName(session);
                string visible;
                using (var environment = new AndroidJavaClass("android.os.Environment"))
                using (var directory = environment.CallStatic<AndroidJavaObject>("getExternalStoragePublicDirectory", "Download"))
                    visible = Path.Combine(directory.Call<string>("getAbsolutePath"), "HumanVisionLogs", Path.GetFileName(session));
                using (var unity = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                using (var activity = unity.GetStatic<AndroidJavaObject>("currentActivity"))
                using (var resolver = activity.Call<AndroidJavaObject>("getContentResolver"))
                using (var downloads = new AndroidJavaClass("android.provider.MediaStore$Downloads"))
                using (var collection = downloads.GetStatic<AndroidJavaObject>("EXTERNAL_CONTENT_URI")) {
                    byte[] buffer = new byte[64 * 1024];
                    // Java 的 byte 是有符号类型；Unity 使用 sbyte[]，避免每次复制制造 JNI obsolete 警告。
                    sbyte[] javaBuffer = new sbyte[buffer.Length];
                    do {
                        try {
                            int count = 0;
                            foreach (var pair in Sources()) {
                                SyncFile(pair.Key, pair.Value, relative, resolver, collection, entries, buffer, javaBuffer); ++count;
                            }
                            if (count > 0) lock (gate) { publicDirectory = visible; status = "公共文件夹已同步（约5秒刷新）"; }
                        } catch (Exception e) { lock (gate) status = "公共同步失败，私有原件保留：" + DeviceDiagnosticSession.Redact(e.GetType().Name + ": " + e.Message); }
                        if (stopping) break;
                        request.WaitOne(5000);
                    } while (true);
                }
            } catch (Exception e) { lock (gate) status = "公共文件夹不可用：" + DeviceDiagnosticSession.Redact(e.Message); }
            finally { foreach (var file in entries.Values) file.Dispose(); AndroidJNI.DetachCurrentThread(); }
        }
        private IEnumerable<KeyValuePair<string, string>> Sources()
        {
            if (Directory.Exists(session)) foreach (string file in Directory.GetFiles(session))
                yield return new KeyValuePair<string, string>(file, Path.GetFileName(file));
            // 只合并本次运行新建的 SDK 日志，绝不复制包含 RTSP 凭据的 settings.json。
            if (Directory.Exists(sdkRoot)) foreach (string directory in Directory.GetDirectories(sdkRoot)) {
                if (excluded.Contains(directory)) continue;
                foreach (string file in Directory.GetFiles(directory, "diagnostics-*.log"))
                    yield return new KeyValuePair<string, string>(file, "sdk-" + Path.GetFileName(directory) + "-" + Path.GetFileName(file));
            }
        }
        private static void SyncFile(string source, string name, string relative, AndroidJavaObject resolver,
            AndroidJavaObject collection, Dictionary<string, FileCopy> entries, byte[] buffer, sbyte[] javaBuffer)
        {
            using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) {
                long end = input.Length;
                // 第一条记录的时间/序号也纳入前缀，可识别同名轮转文件被重新写入，CSV 固定表头不会蒙混通过。
                byte[] prefix = new byte[Math.Min(1024L, end)]; int prefixRead = input.Read(prefix, 0, prefix.Length);
                if (prefixRead != prefix.Length) throw new IOException("Log prefix changed during sync.");
                if (!entries.TryGetValue(source, out var file)) {
                    using (var values = new AndroidJavaObject("android.content.ContentValues"))
                    using (var one = new AndroidJavaObject("java.lang.Integer", 1)) {
                        values.Call("put", "_display_name", name); values.Call("put", "mime_type", name.EndsWith(".csv") ? "text/csv" : "text/plain");
                        values.Call("put", "relative_path", relative); values.Call("put", "is_pending", one);
                        file = new FileCopy { Uri = resolver.Call<AndroidJavaObject>("insert", collection, values) };
                        if (file.Uri == null) throw new IOException("MediaStore refused log file creation.");
                        entries.Add(source, file);
                    }
                }
                bool same = file.Prefix.Length <= prefix.Length;
                for (int i = 0; same && i < file.Prefix.Length; i++) same = file.Prefix[i] == prefix[i];
                bool truncate = file.NeedsRebuild || file.Position > end || !same;
                long start = truncate ? 0 : file.Position;
                if (start == end && !truncate) return;
                // 若写入中途异常，下次整文件重建；不能在未知的部分写入后追加造成重复字节。
                file.NeedsRebuild = true;
                using (var output = resolver.Call<AndroidJavaObject>("openOutputStream", file.Uri, truncate || start == 0 ? "wt" : "wa")) {
                    if (output == null) throw new IOException("Cannot open Downloads log output.");
                    input.Position = start;
                    while (input.Position < end) {
                        int count = input.Read(buffer, 0, (int)Math.Min(buffer.Length, end - input.Position));
                        if (count <= 0) throw new IOException("Log rotated during sync; retry next interval.");
                        Buffer.BlockCopy(buffer, 0, javaBuffer, 0, count);
                        output.Call("write", javaBuffer, 0, count);
                    }
                    output.Call("flush"); output.Call("close");
                }
                using (var values = new AndroidJavaObject("android.content.ContentValues"))
                using (var zero = new AndroidJavaObject("java.lang.Integer", 0)) {
                    values.Call("put", "is_pending", zero); resolver.Call<int>("update", file.Uri, values, null, null);
                }
                file.Position = end; file.Prefix = prefix; file.NeedsRebuild = false;
            }
        }
#endif
        public void Dispose()
        {
            stopping = true; request.Set();
            if (worker == null || worker.Join(1500)) { request.Dispose(); worker = null; }
        }
    }
}
