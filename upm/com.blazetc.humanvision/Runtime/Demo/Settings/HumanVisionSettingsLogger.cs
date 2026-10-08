using System;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using UnityEngine;

namespace HumanVision.Demo
{
    /// <summary>实机诊断：初始化前即建会话，记录环境、操作、状态、错误堆栈和节流统计；最多四个轮转文件。</summary>
    internal sealed class HumanVisionSettingsLogger : IDisposable
    {
        public string Directory { get; }
        public string LastWriteError { get; private set; } = "";
        public string SessionPath => session ?? "";
        private string session;
        private string sessionHeader = "";
        private int fileIndex, limit = 8 * 1024 * 1024;
        private double nextStats, nextPose;
        private long lastSequence;
        private long observedSequence = -1;
        private double lastAdvance;
        private string previousState;
        private bool subscribed, writing, resultStalled;
        private HumanVisionSettingsData settings = new HumanVisionSettingsData();
        private readonly HumanVisionCanonicalJoint[] joints = new HumanVisionCanonicalJoint[32];
        private readonly StringBuilder text = new StringBuilder(4096);
        /// <summary>默认写入 persistentDataPath；测试/宿主可指定独立目录，不依赖 Editor 日志文件。</summary>
        public HumanVisionSettingsLogger(string directory = null)
        { Directory = Path.GetFullPath(directory ?? Path.Combine(Application.persistentDataPath, "HumanVisionSdkSettings", "logs")); }
        public void Configure(HumanVisionSettingsData value)
            => ConfigureSession(value, true);
        /// <summary>先记录启动异常，尚未读取保存配置时不清理任何旧会话。</summary>
        public void BeginStartup() => ConfigureSession(new HumanVisionSettingsData(), false);
        /// <summary>加载有效设置后采用用户的间隔/容量/保留策略；损坏配置可禁止清理旧证据。</summary>
        public void ApplySettings(HumanVisionSettingsData value, bool prune)
        {
            value.Validate(); settings = value.Clone(); limit = settings.LogFileMegabytes * 1024 * 1024;
            nextStats = nextPose = 0;
            if (prune && session != null) {
                try {
                    // 保留当前会话，即使两次会话的时间戳碰巧处于同一毫秒。
                    foreach (string old in System.IO.Directory.GetDirectories(Directory)
                        .Where(p => !string.Equals(p, session, StringComparison.OrdinalIgnoreCase) &&
                            System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileName(p), @"^\d{8}-\d{6}-\d{3}(-[a-f0-9]{8})?$"))
                        .OrderByDescending(Path.GetFileName).Skip(settings.RetainedLogSessions - 1))
                        System.IO.Directory.Delete(old, true);
                } catch (Exception e) { Record("retention.warning", e.Message, e.StackTrace); }
            }
        }
        private void ConfigureSession(HumanVisionSettingsData value, bool prune)
        {
            ApplySettings(value, false); session = null; sessionHeader = "";
            if (!subscribed) { Application.logMessageReceived += OnUnityLog; subscribed = true; }
            try {
                System.IO.Directory.CreateDirectory(Directory);
                string candidate = Path.Combine(Directory, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture) + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
                System.IO.Directory.CreateDirectory(candidate); session = candidate;
            } catch (Exception e) { LastWriteError = HumanVisionSettingsController.Redact(e.Message); return; }
            fileIndex = 0; nextStats = nextPose = 0; lastSequence = 0; observedSequence = -1;
            lastAdvance = Time.realtimeSinceStartupAsDouble; previousState = null; resultStalled = false;
            Record("session.start", "sdk=" + HumanVisionSdk.Version + " unity=" + Application.unityVersion + " app=" + Application.version + " platform=" + Application.platform +
                " device=" + SystemInfo.deviceModel + " os=" + SystemInfo.operatingSystem + " cpu=" + SystemInfo.processorType +
                " cores=" + SystemInfo.processorCount + " memoryMB=" + SystemInfo.systemMemorySize + " gpu=" + SystemInfo.graphicsDeviceName +
                " graphicsAPI=" + SystemInfo.graphicsDeviceType + " graphicsVersion=" + SystemInfo.graphicsDeviceVersion +
                " gpuMemoryMB=" + SystemInfo.graphicsMemorySize + " compute=" + SystemInfo.supportsComputeShaders +
                " colorSpace=" + QualitySettings.activeColorSpace + " persistentData=" + Application.persistentDataPath + " streamingAssets=" + Application.streamingAssetsPath);
            Record("configuration", JsonUtility.ToJson(settings));
            if (prune) ApplySettings(value, true);
        }
        /// <summary>异常和操作立即落盘；长消息截断，RTSP 认证和常见 query 凭据统一去除。禁止逐帧调用。</summary>
        public void Record(string category, string message, string stackTrace = null)
        {
            if (session == null || writing) return;
            string entry = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture) + " +" + Time.realtimeSinceStartupAsDouble.ToString("F3", CultureInfo.InvariantCulture) +
                "s [" + category + "] " + Clean(message, 16384) + "\n";
            if (!string.IsNullOrEmpty(stackTrace)) entry += "  stack: " + Clean(stackTrace, 8192) + "\n";
            // 长期运行覆盖旧文件后，新文件仍包含对应 SDK/设备/配置及实际 Runtime 信息。
            if ((category == "session.start" || category == "configuration" || category == "runtime.initialized") && sessionHeader.Length + entry.Length <= 65536)
                sessionHeader += entry;
            writing = true;
            try {
                string file = Path.Combine(session, "diagnostics-" + fileIndex + ".log");
                if (File.Exists(file) && new FileInfo(file).Length + Encoding.UTF8.GetByteCount(entry) > limit) {
                    fileIndex = (fileIndex + 1) % 4; file = Path.Combine(session, "diagnostics-" + fileIndex + ".log");
                    if (File.Exists(file)) File.Delete(file);
                    File.AppendAllText(file, sessionHeader, new UTF8Encoding(false));
                }
                File.AppendAllText(file, entry, new UTF8Encoding(false)); LastWriteError = "";
            } catch (Exception e) {
                // 不 Debug.Log，避免日志写入错误再次进入 Unity 日志回调造成递归。
                LastWriteError = HumanVisionSettingsController.Redact(e.Message);
            } finally { writing = false; }
        }
        private static string Clean(string value, int maximum)
        {
            value = HumanVisionSettingsController.Redact(value).Replace("\r", "").Replace("\n", "\\n");
            return value.Length <= maximum ? value : value.Substring(0, maximum) + " [truncated]";
        }
        private void OnUnityLog(string condition, string stack, LogType type)
        {
            // 普通 Debug.Log 可高频，默认只捕获警告/错误/异常；SDK 的重要操作由 Record 明确记录。
            if (type != LogType.Log) Record("unity." + type, condition, stack);
        }
        public void Tick(HumanVisionSdk sdk)
        {
            if (session == null || sdk == null) return;
            double now = Time.realtimeSinceStartupAsDouble;
            string state = "sdk=" + sdk.State + " input=" + sdk.InputState + " running=" + sdk.IsRunning + " fresh=" + sdk.HasFreshResult;
            if (state != previousState) { Record("state.changed", state + " lastError=" + sdk.LastError); previousState = state; }
            if (sdk.ResultSequence != observedSequence) {
                observedSequence = sdk.ResultSequence; lastAdvance = now;
                if (resultStalled) Record("results.recovered", "sequence=" + observedSequence);
                resultStalled = false;
            } else if (sdk.IsRunning && now - lastAdvance > 10 && !resultStalled) {
                resultStalled = true; Record("results.timeout", "Streaming input has no new completed result for " + (now-lastAdvance).ToString("F1", CultureInfo.InvariantCulture) +
                    "s; inspect input frames, inference counters, Runtime diagnostics and error records.");
            }
            text.Clear();
            if (now >= nextStats) {
                nextStats = now + settings.StatisticsInterval;
                var stats = sdk.Stats;
                text.Append(state).Append(" users=").Append(sdk.GetUsersCount()).Append('/').Append(sdk.GetMaxBodies())
                    .Append(" sequence=").Append(sdk.ResultSequence).Append(" frame=").Append(sdk.SourceFrameId).Append(" ageMs=").Append(Number(sdk.ResultAgeMilliseconds))
                    .Append(" inputFPS=").Append(Number(stats.InputFps)).Append(" inferenceFPS=").Append(Number(stats.InferenceFps))
                    .Append(" detectMs=").Append(Number(stats.DetectionMs)).Append(" poseMs=").Append(Number(stats.PoseMs))
                    .Append(" trackingMs=").Append(Number(stats.TrackingMs)).Append(" totalMs=").Append(Number(stats.TotalMs))
                    .Append(" submitted=").Append(stats.SubmittedFrames).Append(" processed=").Append(stats.ProcessedFrames).Append(" dropped=").Append(stats.DroppedFrames)
                    .Append(" image=").Append(sdk.GetColorImageWidth()).Append('x').Append(sdk.GetColorImageHeight()).Append(" profile=").Append(sdk.RuntimeProfile)
                    .Append(" modelPack=").Append(sdk.ActiveModelPack).Append(" analysis=").Append(sdk.AnalysisInputSize).Append(" lastError=").Append(sdk.LastError);
                for (int i = 0; i < sdk.GetMaxBodies(); i++) {
                    text.Append(" region[").Append(i).Append("]=").Append(sdk.TryGetRegionOccupancy(i, out bool occupied) ? occupied ? "Occupied" : "Empty" : "Unknown");
                    if (sdk.TryGetBodyByIndex(i, out var body)) text.Append(" body[").Append(i).Append("]=id:").Append(body.StableTrackId)
                        .Append(",confidence:").Append(Number(body.DetectionConfidence)).Append(",box:").Append(body.BoundingBoxPixels);
                }
                Record("statistics", text.ToString()); text.Clear();
            }
            if (settings.DetailedLogs && now >= nextPose && sdk.HasFreshResult && sdk.ResultSequence != lastSequence) {
                nextPose = now + settings.SkeletonLogInterval; lastSequence = sdk.ResultSequence;
                for (int i = 0; i < sdk.GetMaxBodies(); i++) {
                    if (!sdk.CopySkeletonByIndex(i, joints, out var metadata)) continue;
                    text.Clear(); text.Append("slot=").Append(i).Append(" id=").Append(metadata.StableTrackId).Append(" frame=").Append(metadata.SourceFrameId)
                        .Append(" sequence=").Append(metadata.ResultSequence).Append(" observationUs=").Append(metadata.ObservationTimestampUs);
                    for (int j = 0; j < joints.Length; j++) {
                        var joint = joints[j]; text.Append(" | ").Append((HumanVisionCanonicalJointId)j).Append(" valid=").Append(joint.Position.Valid)
                            .Append(" apiValid=").Append(sdk.TryGetJointByIndex(i, (HumanVisionCanonicalJointId)j, out _))
                            .Append(" derived=").Append(joint.Position.IsDerived).Append(" confidence=").Append(Number(joint.Position.Confidence))
                            .Append(" pixel=").Append(joint.Position.Pixel).Append(" normalized=").Append(joint.Position.Normalized)
                            .Append(" observedUs=").Append(joint.ObservationTimestampUs).Append(" predictionMs=").Append(Number(joint.PredictionMilliseconds));
                    }
                    Record("skeleton", text.ToString());
                }
            }
        }
        private static string Number(double value) => value.ToString("F3", CultureInfo.InvariantCulture);
        public string Export()
        {
            System.IO.Directory.CreateDirectory(Directory);
            string zip = Path.Combine(Path.GetDirectoryName(Directory), "logs-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".zip");
            ZipFile.CreateFromDirectory(Directory, zip); return zip;
        }
        public void Dispose() {
            Record("session.end", "diagnostics disposed");
            if (subscribed) { Application.logMessageReceived -= OnUnityLog; subscribed = false; }
            session = null;
        }
    }
}
