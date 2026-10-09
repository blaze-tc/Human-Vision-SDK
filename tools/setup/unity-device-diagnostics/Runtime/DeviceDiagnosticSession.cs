using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// 设置 Demo 的实机日志文件会话。后台 Unity 回调只入有界队列；文件写入/导出由主线程低频执行。
    /// 本类不调用 Unity API，因此后台线程也能安全记录时间、线程号和堆栈。
    /// </summary>
    public sealed class DeviceDiagnosticSession : IDisposable
    {
        private sealed class PendingLog
        {
            public DateTime Utc;
            public double Seconds;
            public string Condition, Stack, Level;
            public int Thread;
        }
        private sealed class FileState
        {
            public int Index;
            public long Bytes;
            public StreamWriter Writer;
        }
        private readonly object queueLock = new object();
        private readonly Queue<PendingLog> pending = new Queue<PendingLog>();
        private readonly Dictionary<string, FileState> files = new Dictionary<string, FileState>();
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly int capacity, maximumBytes;
        private readonly string root;
        private string csvHeader;
        private bool disposed;
        private long sequence, dropped, lastReportedDrop, nativeDropped, lastReportedNativeDrop;
        private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;
        private static readonly Regex UrlCredentials = new Regex(@"(?i)\b((?:rtsps?|https?)://)[^/\s@]+@", RegexOptions.Compiled);
        private static readonly Regex QuotedCredentials = new Regex(@"(?i)(\b(?:password|passwd|pwd|(?:access[_-]|refresh[_-]|auth[_-])?token|api[_-]?key|secret)\b[""']?\s*[:=]\s*)([""'])((?:\\.|(?!\2)[^\\])*)\2", RegexOptions.Compiled);
        private static readonly Regex NamedCredentials = new Regex(@"(?i)(\b(?:password|passwd|pwd|(?:access[_-]|refresh[_-]|auth[_-])?token|api[_-]?key|secret)\b[""']?\s*[:=]\s*[""']?)([^&\s""'<>}\],]+)", RegexOptions.Compiled);

        public string SessionPath { get; private set; } = "";
        public string LastWriteError { get; private set; } = "";
        public long DroppedUnityMessages { get { lock (queueLock) return dropped; } }
        public long DroppedNativeMessages { get { lock (queueLock) return nativeDropped; } }
        public double ElapsedSeconds => clock.Elapsed.TotalSeconds;

        /// <summary>单个种类最多四个文件；生产默认每文件 8 MB，测试可使用较小容量验证轮转。</summary>
        public DeviceDiagnosticSession(string directory, int fileBytes = 8 * 1024 * 1024, int queueCapacity = 2048)
        {
            root = Path.GetFullPath(directory); maximumBytes = Math.Max(512, fileBytes); capacity = Math.Max(1, queueCapacity);
            try {
                Directory.CreateDirectory(root);
                SessionPath = Path.Combine(root, "session-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff", Invariant) + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
                Directory.CreateDirectory(SessionPath);
                Record("session.start", "format=1; timestamps=UTC; skeletons are sampled genuine SDK results; world positions are plane mappings");
            } catch (Exception e) { SetError(e); }
        }
        /// <summary>普通日志、警告、错误和异常均捕获。队列满时计数；严重错误优先保留最新证据。</summary>
        public void EnqueueUnity(string condition, string stack, string level, int thread)
        {
            lock (queueLock) {
                if (disposed) return;
                if (pending.Count >= capacity) {
                    if (level == "Log") { ++dropped; return; }
                    if (pending.Dequeue().Level == "Native") ++nativeDropped; else ++dropped;
                }
                pending.Enqueue(new PendingLog { Utc = DateTime.UtcNow, Seconds = clock.Elapsed.TotalSeconds,
                    Condition = Limit(condition, 16384), Stack = Limit(stack, 8192), Level = level, Thread = thread });
            }
        }
        /// <summary>原生计时只入有界队列；满时丢计时并计数，不挤掉 Unity 严重错误。</summary>
        public void EnqueueNative(string line, int thread)
        {
            lock (queueLock) {
                if (disposed) return;
                if (pending.Count >= capacity) { ++nativeDropped; return; }
                pending.Enqueue(new PendingLog { Utc = DateTime.UtcNow, Seconds = clock.Elapsed.TotalSeconds,
                    Condition = Limit(line, 16384), Stack = "", Level = "Native", Thread = thread });
            }
        }
        public static string Redact(string value)
        {
            // 先处理带引号的值，空格及转义引号也属于凭据；再处理普通 URL/query/key=value。
            value = QuotedCredentials.Replace(value ?? "", "$1$2***$2");
            return NamedCredentials.Replace(UrlCredentials.Replace(value, "$1***@"), "$1***");
        }
        private static string Limit(string value, int length)
        {
            value = value ?? "";
            return value.Length <= length ? value : value.Substring(0, length) + " [truncated]";
        }
        /// <summary>JSON 字符串转义，不使用 Unity JsonUtility，以免在线程回调中访问 Unity。</summary>
        private static string Quote(string value)
        {
            var output = new StringBuilder("\"");
            foreach (char c in Redact(value)) {
                switch (c) {
                    case '"': output.Append("\\\""); break;
                    case '\\': output.Append("\\\\"); break;
                    case '\n': output.Append("\\n"); break;
                    case '\r': output.Append("\\r"); break;
                    case '\t': output.Append("\\t"); break;
                    default: if (c < 32) output.Append("\\u").Append(((int)c).ToString("x4", Invariant)); else output.Append(c); break;
                }
            }
            return output.Append('"').ToString();
        }
        public void Record(string category, string message)
            => WriteEvent(DateTime.UtcNow, clock.Elapsed.TotalSeconds, category, message, "", 0);
        private void WriteEvent(DateTime utc, double seconds, string category, string message, string stack, int thread)
        {
            Write("events.jsonl", "{\"utc\":" + Quote(utc.ToString("O", Invariant)) + ",\"elapsed_s\":" + seconds.ToString("F6", Invariant) +
                ",\"record\":" + (++sequence).ToString(Invariant) + ",\"category\":" + Quote(category) + ",\"thread\":" + thread.ToString(Invariant) +
                ",\"message\":" + Quote(Limit(message, 65536)) + ",\"stack\":" + Quote(Limit(stack, 8192)) + "}\n");
        }
        /// <summary>主线程低频序列化的骨骼、硬件和阶段记录；白名单防止任意路径写入。</summary>
        public void WriteJsonLine(string file, string json)
        {
            if (file != "skeletons.jsonl" && file != "hardware.jsonl" && file != "timings.jsonl")
                throw new ArgumentException("Unknown diagnostics stream.", nameof(file));
            Write(file, Redact(json) + "\n");
        }
        private static string Csv(string value)
        {
            value = Redact(value).Replace("\r", "").Replace("\n", "\\n");
            return value.IndexOfAny(new[] { ',', '"' }) < 0 ? value : "\"" + value.Replace("\"", "\"\"") + "\"";
        }
        public void SetCsvHeader(string[] names) => csvHeader = string.Join(",", names.Select(Csv)) + "\n";
        public void WriteCsvRow(string[] values) => Write("performance.csv", string.Join(",", values.Select(Csv)) + "\n");
        /// <summary>环境摘要独立保存，轮转后仍能确定 SDK、设备、图形 API 和日志格式。</summary>
        public void WriteEnvironment(string json)
        {
            if (disposed || string.IsNullOrEmpty(SessionPath)) return;
            try { File.WriteAllText(Path.Combine(SessionPath, "session.json"), Redact(json), new UTF8Encoding(false)); }
            catch (Exception e) { SetError(e); }
        }
        private void Write(string name, string line)
        {
            if (disposed || string.IsNullOrEmpty(SessionPath)) return;
            try {
                if (!files.TryGetValue(name, out var state)) { state = new FileState(); files.Add(name, state); }
                int bytes = Encoding.UTF8.GetByteCount(line);
                if (state.Bytes > 0 && state.Bytes + bytes > maximumBytes) {
                    state.Writer?.Dispose(); state.Writer = null; state.Index = (state.Index + 1) % 4; state.Bytes = 0;
                }
                if (state.Writer == null) {
                    string path = Path.Combine(SessionPath, Path.GetFileNameWithoutExtension(name) + "-" + state.Index + Path.GetExtension(name));
                    state.Writer = new StreamWriter(new FileStream(path, state.Bytes == 0 ? FileMode.Create : FileMode.Append, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false));
                    if (state.Bytes == 0 && name == "performance.csv" && csvHeader != null) { state.Writer.Write(csvHeader); state.Bytes += Encoding.UTF8.GetByteCount(csvHeader); }
                }
                state.Writer.Write(line); state.Bytes += bytes;
            } catch (Exception e) {
                SetError(e);
                if (files.TryGetValue(name, out var state)) { try { state.Writer?.Dispose(); } catch { } state.Writer = null; }
            }
        }
        private void SetError(Exception e) => LastWriteError = Redact(e.GetType().Name + ": " + e.Message);
        /// <summary>每次最多处理指定数量，避免日志洪水拖住主线程；导出/退出时处理完整有界队列。</summary>
        public void Flush(int maximumPending = 256)
        {
            if (disposed) return;
            for (int i = 0; i < maximumPending; i++) {
                PendingLog log;
                lock (queueLock) { if (pending.Count == 0) break; log = pending.Dequeue(); }
                bool native = log.Level == "Native";
                WriteEvent(log.Utc, log.Seconds, native ? "native.android" : "unity." + log.Level, log.Condition, log.Stack, log.Thread);
                Write(native ? "native.log" : "unity.log", log.Utc.ToString("O", Invariant) + " +" + log.Seconds.ToString("F3", Invariant) + "s [" + log.Level + "] thread=" + log.Thread +
                    " " + Redact(log.Condition).Replace("\r", "").Replace("\n", "\\n") + "\n" + (string.IsNullOrEmpty(log.Stack) ? "" : "stack: " + Redact(log.Stack) + "\n"));
            }
            long count = DroppedUnityMessages;
            if (count != lastReportedDrop) { Record("unity.queue.overflow", "dropped=" + count + " capacity=" + capacity); lastReportedDrop = count; }
            count = DroppedNativeMessages;
            if (count != lastReportedNativeDrop) { Record("native.queue.overflow", "dropped=" + count + " capacity=" + capacity); lastReportedNativeDrop = count; }
            // 低频批次完成后释放文件句柄，文件管理器/ZIP/分析工具可立即读取，不与 Mono 的共享检查冲突。
            foreach (var state in files.Values) {
                try { state.Writer?.Dispose(); } catch (Exception e) { SetError(e); }
                finally { state.Writer = null; }
            }
        }
        /// <summary>导出当前诊断会话，并合并本次运行产生的 SDK 设置日志；不导出包含凭据的原始配置文件。</summary>
        public string Export(string sdkLogRoot, string[] sdkSessions)
        {
            Record("export.begin", "flush pending logs before ZIP"); Flush(capacity);
            if (string.IsNullOrEmpty(SessionPath)) throw new IOException(LastWriteError);
            string destination = Path.Combine(root, Path.GetFileName(SessionPath) + "-" + DateTime.UtcNow.ToString("HHmmssfff", Invariant) + ".zip");
            string temporary = destination + ".tmp";
            try {
                using (var zip = ZipFile.Open(temporary, ZipArchiveMode.Create)) {
                    foreach (string file in Directory.GetFiles(SessionPath)) zip.CreateEntryFromFile(file, "device/" + Path.GetFileName(file), CompressionLevel.Fastest);
                    if (!string.IsNullOrEmpty(sdkLogRoot) && sdkSessions != null) {
                        string expected = Path.GetFullPath(sdkLogRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                        foreach (string directory in sdkSessions) {
                            if (!Path.GetFullPath(directory).StartsWith(expected, StringComparison.OrdinalIgnoreCase) || !Directory.Exists(directory)) continue;
                            foreach (string file in Directory.GetFiles(directory, "diagnostics-*.log"))
                                zip.CreateEntryFromFile(file, "sdk-settings/" + Path.GetFileName(directory) + "/" + Path.GetFileName(file), CompressionLevel.Fastest);
                        }
                    }
                }
                File.Move(temporary, destination); Record("export.complete", destination); Flush(capacity); return destination;
            } catch (Exception e) { SetError(e); try { if (File.Exists(temporary)) File.Delete(temporary); } catch { } throw; }
        }
        public void Dispose()
        {
            if (disposed) return;
            Record("session.end", "Unity capture unsubscribed; final buffered records flushed"); Flush(capacity);
            lock (queueLock) disposed = true;
            foreach (var state in files.Values) { try { state.Writer?.Dispose(); } catch (Exception e) { SetError(e); } }
        }
    }
}
