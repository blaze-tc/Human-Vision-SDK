using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.RegularExpressions;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>稀疏原生壁钟计时，只有同一帧的六段齐全才发布。计时含同步等待，不能冒充纯 GPU 时间。</summary>
    [Serializable]
    public sealed class NativeStageSample
    {
        public bool available;
        public long frameId = -1;
        public string utc = "", source = "HV_TOPDOWN_NCNN/run_raw/CLOCK_MONOTONIC";
        public string modelMeasurement = "Extractor wall time includes internal GPU submissions/wait and download recording; not pure CPU or pure GPU duration";
        public double importPreprocessRecordMs, preprocessSubmitWaitMs, extractDownloadMs;
        public double inferenceSubmitWaitMs, denseOutputCopyMs, ownershipReleaseMs, backendSumMs;
        public long completedSamples;
    }

    public sealed class NativeStageTracker
    {
        private readonly object gate = new object();
        private readonly Dictionary<long, double[]> pending = new Dictionary<long, double[]>();
        private NativeStageSample latest = new NativeStageSample();
        private long completed;
        private static readonly string[] Names = { "import_preprocess_record", "preprocess_submit_wait", "extract_download_elapsed", "inference_submit_wait", "dense_output_copy", "ownership_release_wait" };
        private static readonly Regex Record = new Regex(@"run_raw stage=(\w+) frame_id=(\d+) elapsed_us=(\d+)(?:\s|$)", RegexOptions.Compiled);
        public void Reset() { lock (gate) { pending.Clear(); latest = new NativeStageSample(); completed = 0; } }
        /// <summary>由 JNI 日志线程调用，不访问 Unity 对象，不写文件。最多保留八个未完成帧。</summary>
        public void Accept(string line)
        {
            var match = Record.Match(line ?? "");
            if (!match.Success || !long.TryParse(match.Groups[2].Value, out long frame) ||
                !long.TryParse(match.Groups[3].Value, out long us)) return;
            int index = Array.IndexOf(Names, match.Groups[1].Value);
            if (index < 0) return;
            lock (gate) {
                if (!pending.TryGetValue(frame, out var stages)) {
                    if (pending.Count >= 8) pending.Clear();
                    stages = new double[6]; for (int i = 0; i < stages.Length; i++) stages[i] = -1;
                    pending.Add(frame, stages);
                }
                stages[index] = us / 1000d;
                for (int i = 0; i < stages.Length; i++) if (stages[i] < 0) return;
                latest = new NativeStageSample { available = true, frameId = frame, utc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
                    importPreprocessRecordMs = stages[0], preprocessSubmitWaitMs = stages[1], extractDownloadMs = stages[2],
                    inferenceSubmitWaitMs = stages[3], denseOutputCopyMs = stages[4], ownershipReleaseMs = stages[5],
                    backendSumMs = stages[0] + stages[1] + stages[2] + stages[3] + stages[4] + stages[5], completedSamples = ++completed };
                pending.Remove(frame);
            }
        }
        // 记录发布后不再修改；读取引用的调用者也只读，避免持有锁做 JSON/UI。
        public NativeStageSample Read() { lock (gate) return latest; }
    }
}
