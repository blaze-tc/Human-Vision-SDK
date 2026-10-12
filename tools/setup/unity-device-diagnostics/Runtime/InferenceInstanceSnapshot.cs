using System;
using System.Globalization;
namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>低频读取原生实例池快照。忙碌包含管线等待，不是 GPU/NPU 利用率。</summary>
    [Serializable]
    public sealed class InferenceInstanceSnapshot
    {
        public int busy = -1, total = -1;
        public string fallback = "";
        public string scope = "instantaneous active pipeline calls / created body inference instances; not accelerator utilization";
        public string Display => total < 0 ? "推理实例：不可用" : "推理实例：" + busy + " / " + total + "（忙碌 / 总数）";
        public static InferenceInstanceSnapshot Read(string text, bool initialized)
        {
            var result = new InferenceInstanceSnapshot();
            if (!initialized) { result.busy = result.total = 0; return result; }
            foreach (string line in (text ?? "").Split('\n')) {
                if (line.StartsWith("Inference instances=", StringComparison.Ordinal)) result.total = Count(line);
                if (line.StartsWith("Active inference instances=", StringComparison.Ordinal)) result.busy = Count(line);
                if (line.StartsWith("Instance pool fallback=", StringComparison.Ordinal)) result.fallback = line.Substring(line.IndexOf('=') + 1).Trim();
            }
            // Do not manufacture plausible counts from missing or inconsistent telemetry.
            if (result.total < 0 || result.busy < 0 || result.busy > result.total) result.busy = result.total = -1;
            return result;
        }
        private static int Count(string line) => int.TryParse(line.Substring(line.IndexOf('=') + 1).Trim(),
            NumberStyles.None, CultureInfo.InvariantCulture, out int value) && value >= 0 ? value : -1;
    }
}
