using System;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using UnityEngine;

namespace HumanVision.Demo
{
    /// <summary>设置 Demo 的节流诊断日志。只记录 SDK 实测值；最多四个轮转文件，超限会覆盖最旧文件。</summary>
    internal sealed class HumanVisionSettingsLogger : IDisposable
    {
        public string Directory { get; } = Path.Combine(Application.persistentDataPath, "HumanVisionSdkSettings", "logs");
        private string session;
        private int fileIndex, limit = 8 * 1024 * 1024;
        private double nextStats, nextPose;
        private long lastSequence;
        private HumanVisionSettingsData settings = new HumanVisionSettingsData();
        private readonly HumanVisionCanonicalJoint[] joints = new HumanVisionCanonicalJoint[32];
        private readonly StringBuilder text = new StringBuilder(4096);
        public void Configure(HumanVisionSettingsData value)
        {
            settings = value.Clone(); limit = settings.LogFileMegabytes * 1024 * 1024;
            System.IO.Directory.CreateDirectory(Directory);
            session = Path.Combine(Directory, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture));
            System.IO.Directory.CreateDirectory(session); fileIndex = 0; nextStats = nextPose = 0; lastSequence = 0;
            foreach (string old in System.IO.Directory.GetDirectories(Directory).OrderByDescending(Path.GetFileName).Skip(settings.RetainedLogSessions))
                System.IO.Directory.Delete(old, true);
        }
        public void Tick(HumanVisionSdk sdk)
        {
            if (session == null) return;
            double now = Time.realtimeSinceStartupAsDouble;
            text.Clear();
            if (now >= nextStats) {
                nextStats = now + settings.StatisticsInterval;
                var stats = sdk.Stats;
                text.Append(DateTime.UtcNow.ToString("O")).Append(" state=").Append(sdk.State).Append(" input=").Append(sdk.InputState)
                    .Append(" users=").Append(sdk.GetUsersCount()).Append(" sequence=").Append(sdk.ResultSequence).Append(" frame=").Append(sdk.SourceFrameId)
                    .Append(" inferenceFPS=").Append(stats.InferenceFps.ToString("F2", CultureInfo.InvariantCulture)).Append(" dropped=").Append(stats.DroppedFrames).AppendLine();
            }
            if (settings.DetailedLogs && now >= nextPose && sdk.HasFreshResult && sdk.ResultSequence != lastSequence) {
                nextPose = now + settings.SkeletonLogInterval; lastSequence = sdk.ResultSequence;
                for (int i = 0; i < sdk.GetMaxBodies(); i++) {
                    if (!sdk.CopySkeletonByIndex(i, joints, out var metadata)) continue;
                    text.Append("pose slot=").Append(i).Append(" id=").Append(metadata.StableTrackId).Append(" frame=").Append(metadata.SourceFrameId);
                    for (int j = 0; j < joints.Length; j++) if (joints[j].Position.Valid) {
                        var joint = joints[j]; text.Append(' ').Append(j).Append(':').Append(joint.Position.Normalized.x.ToString("F4", CultureInfo.InvariantCulture))
                            .Append(',').Append(joint.Position.Normalized.y.ToString("F4", CultureInfo.InvariantCulture)).Append(',').Append(joint.Position.Confidence.ToString("F3", CultureInfo.InvariantCulture));
                    }
                    text.AppendLine();
                }
            }
            if (text.Length == 0) return;
            string file = Path.Combine(session, "diagnostics-" + fileIndex + ".log");
            if (File.Exists(file) && new FileInfo(file).Length + text.Length * 4 > limit) {
                fileIndex = (fileIndex + 1) % 4; file = Path.Combine(session, "diagnostics-" + fileIndex + ".log");
                if (File.Exists(file)) File.Delete(file);
            }
            File.AppendAllText(file, text.ToString());
        }
        public string Export()
        {
            System.IO.Directory.CreateDirectory(Directory);
            string zip = Path.Combine(Path.GetDirectoryName(Directory), "logs-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".zip");
            ZipFile.CreateFromDirectory(Directory, zip); return zip;
        }
        public void Dispose() { session = null; }
    }
}
