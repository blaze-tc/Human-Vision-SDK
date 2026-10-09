using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace HumanVision.TestProject.Diagnostics
{
    [Serializable] public sealed class ThermalSensorSample
    {
        public string type = "", source = "", status = "unavailable";
        public double temperatureC = -1;
    }

    /// <summary>
    /// 后台只读 sysfs。发现 Mali 的实际 devfreq 名称并解析 Rockchip 的 load@Hz 格式；
    /// 保存每个 thermal_zone 的原始类型与路径，CPU/GPU 温度和 Android 电池温度分别标注。
    /// 不申请 root、不修改 governor/频率/温控；无权限或数据无效时保留 -1 与逐节点原因。
    /// </summary>
    public sealed class DeviceSysfsTelemetry
    {
        private readonly string devfreqRoot, thermalRoot, kgslRoot;
        private string[] gpuNodes = Array.Empty<string>(), thermalNodes = Array.Empty<string>();
        private string gpuDiscovery = "not scanned", thermalDiscovery = "not scanned";
        private DateTime nextDiscovery = DateTime.MinValue;
        public DeviceSysfsTelemetry(string devfreqRoot, string thermalRoot, string kgslRoot)
        { this.devfreqRoot = devfreqRoot; this.thermalRoot = thermalRoot; this.kgslRoot = kgslRoot; }

        private static string Read(string path, out string status)
        {
            try { string raw = File.ReadAllText(path).Trim(); status = "available"; return raw; }
            catch (Exception e) { status = e.GetType().Name + ": " + e.Message; return ""; }
        }
        private static string[] Discover(string root, Func<string, bool> predicate, out string status)
        {
            try {
                var nodes = Directory.GetDirectories(root).Where(predicate).OrderBy(p => p, StringComparer.Ordinal).Take(32).ToArray();
                status = "discovered " + nodes.Length + " readable directory names"; return nodes;
            } catch (Exception e) { status = e.GetType().Name + ": " + e.Message; return Array.Empty<string>(); }
        }
        public void Capture(HardwareSample sample)
        {
            if (DateTime.UtcNow >= nextDiscovery) {
                gpuNodes = Discover(devfreqRoot, p => { string n = Path.GetFileName(p).ToLowerInvariant(); return n.Contains("gpu") || n.Contains("mali"); }, out gpuDiscovery);
                thermalNodes = Discover(thermalRoot, p => Path.GetFileName(p).StartsWith("thermal_zone", StringComparison.Ordinal), out thermalDiscovery);
                nextDiscovery = DateTime.UtcNow.AddSeconds(30); // 低频重新发现；不逐帧扫描设备文件树。
            }
            CaptureGpu(sample); CaptureThermal(sample);
        }
        private void CaptureGpu(HardwareSample sample)
        {
            var failures = new List<string>();
            string kgslBusy = Path.Combine(kgslRoot, "gpubusy");
            // 频率和负载是独立节点；负载未开放时也应保留可读时钟。
            string kgslClock = Path.Combine(kgslRoot, "gpuclk");
            if (File.Exists(kgslClock)) CaptureClock(sample, kgslClock, "");
            // 不对明显缺失的 Adreno 路径每两秒制造异常；实际存在但权限不足的节点保留读错误。
            if (File.Exists(kgslBusy)) {
                string raw = Read(kgslBusy, out string status); double load = HardwareMetrics.KgslPercent(raw);
                if (load >= 0) {
                    sample.gpuSource = kgslBusy; sample.gpuPercent = load; sample.gpuStatus = "available; KGSL driver interval";
                    CaptureClock(sample, Path.Combine(kgslRoot, "gpuclk"), ""); return;
                }
                failures.Add(kgslBusy + ": " + (status == "available" ? "invalid busy/total interval: " + raw : status));
            }
            // 不依赖 fb000000.gpu 是否带 -mali 后缀，也不把 NPU/DMC 的 load 当成 GPU。
            var known = new[] { Path.Combine(devfreqRoot, "fb000000.gpu"), Path.Combine(devfreqRoot, "fb000000.gpu-mali"), Path.Combine(devfreqRoot, "ff9a0000.gpu") };
            foreach (string node in gpuNodes.Concat(known).Distinct()) {
                if (Directory.Exists(node) && sample.gpuFrequencyMHz < 0)
                    CaptureClock(sample, Path.Combine(node, "cur_freq"), "");
                string path = Path.Combine(node, "load");
                if (!File.Exists(path)) { failures.Add(path + ": absent or inaccessible"); continue; }
                string raw = Read(path, out string status); double load = HardwareMetrics.DevfreqLoadPercent(raw);
                if (load < 0) { failures.Add(path + ": " + (status == "available" ? "invalid load format: " + raw : status)); continue; }
                sample.gpuPercent = load; sample.gpuSource = path; sample.gpuStatus = "available; devfreq driver interval; raw=" + raw;
                CaptureClock(sample, Path.Combine(node, "cur_freq"), raw); return;
            }
            // 某些旧 Mali BSP 使用 misc 节点，标准 devfreq 不可用时才尝试。
            const string legacy = "/sys/class/misc/mali0/device/utilization";
            if (File.Exists(legacy)) {
                string raw = Read(legacy, out string status); double load = HardwareMetrics.DevfreqLoadPercent(raw);
                if (load >= 0) { sample.gpuPercent = load; sample.gpuSource = legacy; sample.gpuStatus = "available; vendor interval; raw=" + raw; return; }
                failures.Add(legacy + ": " + status + "; raw=" + raw);
            }
            sample.gpuSource = devfreqRoot;
            sample.gpuStatus = "unavailable; " + gpuDiscovery + "; " + string.Join("; ", failures);
        }
        private static void CaptureClock(HardwareSample sample, string path, string loadRaw)
        {
            string raw = Read(path, out string status);
            if (long.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out long hz) && hz > 0) {
                sample.gpuFrequencyMHz = hz / 1000000d; sample.gpuFrequencySource = path; sample.gpuFrequencyStatus = "available; Hz"; return;
            }
            double embedded = HardwareMetrics.DevfreqFrequencyMHz(loadRaw);
            if (embedded >= 0) { sample.gpuFrequencyMHz = embedded; sample.gpuFrequencySource = sample.gpuSource; sample.gpuFrequencyStatus = "available; Hz embedded in load"; }
            else { sample.gpuFrequencySource = path; sample.gpuFrequencyStatus = "unavailable; " + status + "; raw=" + raw; }
        }
        private void CaptureThermal(HardwareSample sample)
        {
            var values = new List<ThermalSensorSample>();
            foreach (string node in thermalNodes) {
                string type = Read(Path.Combine(node, "type"), out string typeStatus);
                string path = Path.Combine(node, "temp"), raw = Read(path, out string status);
                var value = new ThermalSensorSample { type = type, source = path, status = status };
                // Linux thermal sysfs 的单位是毫摄氏度；异常格式不能推测单位或填零。
                if (typeStatus == "available" && long.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out long milli) && milli >= 0 && milli <= 200000) {
                    value.temperatureC = milli / 1000d; value.status = "available; millidegrees Celsius";
                    string lower = type.ToLowerInvariant();
                    if (lower.Contains("gpu")) sample.gpuTemperatureC = Math.Max(sample.gpuTemperatureC, value.temperatureC);
                    if (lower.Contains("cpu") || lower.Contains("bigcore") || lower.Contains("littlecore") || lower.Contains("soc"))
                        sample.cpuTemperatureC = Math.Max(sample.cpuTemperatureC, value.temperatureC);
                } else if (status == "available") value.status = "invalid type/temp: " + typeStatus + "; raw=" + raw;
                values.Add(value);
            }
            sample.thermalSensors = values.ToArray(); sample.thermalSensorsStatus = thermalDiscovery;
        }
    }
}
