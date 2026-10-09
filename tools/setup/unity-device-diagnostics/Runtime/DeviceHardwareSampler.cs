using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Text.RegularExpressions;
using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>缺失指标以 -1 + 原因表示。CPU 应用占比按全部逻辑核容量归一；一核占比另列。</summary>
    [Serializable]
    public sealed class HardwareSample
    {
        public string utc = "", cpuSource = "", systemCpuSource = "/proc/stat", gpuSource = "", gpuStatus = "not sampled";
        public string systemCpuStatus = "not sampled", cpuStatus = "not sampled", memoryStatus = "not sampled";
        public string npuStatus = "device NPU load unavailable; current CPU/NCNN Vulkan profiles do not use NPU", npuSource = "";
        public string samplerStatus = "not sampled";
        public string gpuFrequencySource = "", gpuFrequencyStatus = "unavailable", thermalSensorsStatus = "unavailable";
        public double cpuTemperatureC = -1, gpuTemperatureC = -1;
        public ThermalSensorSample[] thermalSensors = Array.Empty<ThermalSensorSample>();
        public string temperatureSource = "Android battery (not CPU junction)", thermalStatus = "unavailable";
        public double elapsed_s, intervalSeconds, appCpuPercent = -1, appCpuOneCorePercent = -1, systemCpuPercent = -1;
        public double gpuPercent = -1, gpuFrequencyMHz = -1, npuPercent = -1, batteryTemperatureC = -1;
        public double processPssMB = -1, processRssMB = -1, systemAvailableMB = -1, systemTotalMB = -1;
        public long clockTicksPerSecond = -1;
        public int logicalCores;
        public string cpuFrequencyMHz = "unavailable";
    }
    public sealed class SystemCpuTicks { public long total = -1, idle = -1; }
    /// <summary>计数解析独立于平台，避免 comm 中空格/括号、guest 重复计数或计数重置制造假负载。</summary>
    public static class HardwareMetrics
    {
        // Rockchip devfreq.c::load_show emits percent@frequencyHz. Reading this as
        // a plain double discarded valid Mali load values in the original sampler.
        private static readonly Regex Devfreq = new Regex(@"^\s*(?<load>\d+(?:\.\d+)?)\s*(?:%|@(?<hz>\d+)Hz)?\s*$", RegexOptions.CultureInvariant);
        public static double DevfreqLoadPercent(string raw)
        {
            var match = Devfreq.Match(raw ?? "");
            return match.Success && double.TryParse(match.Groups["load"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double load) &&
                !double.IsNaN(load) && !double.IsInfinity(load) && load >= 0 && load <= 100 ? load : -1;
        }
        public static double DevfreqFrequencyMHz(string raw)
        {
            var match = Devfreq.Match(raw ?? "");
            return match.Success && long.TryParse(match.Groups["hz"].Value, out long hz) && hz > 0 ? hz / 1000000d : -1;
        }
        public static long ProcessTicks(string stat)
        {
            int end = (stat ?? "").LastIndexOf(')'); if (end < 0) return -1;
            string[] fields = stat.Substring(end + 1).Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            return fields.Length > 12 && long.TryParse(fields[11], out long u) && long.TryParse(fields[12], out long s) && u >= 0 && s >= 0 ? u + s : -1;
        }
        public static SystemCpuTicks SystemTicks(string raw)
        {
            var result = new SystemCpuTicks();
            string[] fields = (raw ?? "").Split('\n')[0].Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            if (fields.Length < 9 || fields[0] != "cpu") return result;
            long total = 0, idle = 0;
            for (int i = 1; i <= 8; i++) {
                if (!long.TryParse(fields[i], out long value) || value < 0) return result;
                total += value; if (i == 4 || i == 5) idle += value;
            }
            result.total = total; result.idle = idle; return result;
        }
        public static double CpuPercent(long current, long previous, double seconds, long ticksPerSecond, int cores)
            => previous < 0 || current < previous || seconds <= 0 || ticksPerSecond <= 0 || cores <= 0 ? -1 :
                Math.Min(100, (current - previous) * 100d / ticksPerSecond / seconds / cores);
        public static double KgslPercent(string raw)
        {
            string[] fields = (raw ?? "").Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            return fields.Length == 2 && long.TryParse(fields[0], out long busy) && long.TryParse(fields[1], out long total) &&
                busy >= 0 && total > 0 && busy <= total ? busy * 100d / total : -1;
        }
    }

    /// <summary>两秒一次后台采样；/proc/sysfs/JNI IO 不进入识别或 Unity 主线程热路径。</summary>
    public sealed class DeviceHardwareSampler : IDisposable
    {
        private readonly int cores;
        private readonly object gate = new object();
        private readonly ManualResetEvent stop = new ManualResetEvent(false);
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private Thread worker;
        private HardwareSample latest = new HardwareSample();
        private long previousProcess = -1;
        private double previousSeconds;
        private SystemCpuTicks previousSystem = new SystemCpuTicks();
        private readonly DeviceSysfsTelemetry sysfs = new DeviceSysfsTelemetry("/sys/class/devfreq", "/sys/class/thermal", "/sys/class/kgsl/kgsl-3d0");
        public DeviceHardwareSampler(int logicalCores) { cores = Math.Max(1, logicalCores); }
        public void Start() { worker = new Thread(Run) { IsBackground = true, Name = "HV hardware diagnostics" }; worker.Start(); }
        public HardwareSample Read() { lock (gate) return latest; }
        private static string ReadFile(string path, out string status)
        {
            try { string raw = File.ReadAllText(path); status = "available"; return raw; }
            catch (Exception e) { status = e.GetType().Name + ": " + e.Message; return ""; }
        }
        private void Run()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            AndroidJNI.AttachCurrentThread();
#endif
            try {
                do {
                    var value = new HardwareSample { utc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture), logicalCores = cores, elapsed_s = clock.Elapsed.TotalSeconds };
                    value.intervalSeconds = value.elapsed_s - previousSeconds;
                    try { Capture(value); value.samplerStatus = "completed"; } catch (Exception e) { value.samplerStatus = e.GetType().Name + ": " + e.Message; }
                    previousSeconds = value.elapsed_s;
                    lock (gate) latest = value;
                } while (!stop.WaitOne(2000));
            } finally {
#if UNITY_ANDROID && !UNITY_EDITOR
                AndroidJNI.DetachCurrentThread();
#endif
            }
        }
#if UNITY_ANDROID && !UNITY_EDITOR
        [DllImport("libc")] private static extern long sysconf(int name);
#endif
        private void Capture(HardwareSample v)
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            // bionic _SC_CLK_TCK = 0x0006；从设备查询，不能复用其它平台常量或假设 HZ。
            v.clockTicksPerSecond = sysconf(6); v.cpuSource = "/proc/self/stat utime+stime / sysconf(_SC_CLK_TCK=6) / logical cores";
            long ticks = HardwareMetrics.ProcessTicks(ReadFile("/proc/self/stat", out v.cpuStatus));
            v.appCpuPercent = HardwareMetrics.CpuPercent(ticks, previousProcess, v.intervalSeconds, v.clockTicksPerSecond, cores);
            v.appCpuOneCorePercent = v.appCpuPercent < 0 ? -1 : v.appCpuPercent * cores;
            if (previousProcess < 0 && ticks >= 0) v.cpuStatus = "warm-up; need next interval";
            previousProcess = ticks;
            var sys = HardwareMetrics.SystemTicks(ReadFile("/proc/stat", out v.systemCpuStatus));
            if (previousSystem.total >= 0 && sys.total > previousSystem.total && sys.idle >= previousSystem.idle)
                v.systemCpuPercent = Math.Max(0, Math.Min(100, 100d * (1 - (sys.idle - previousSystem.idle) / (double)(sys.total - previousSystem.total))));
            previousSystem = sys;
            var frequencies = new System.Text.StringBuilder();
            for (int i = 0; i < cores; i++) {
                string raw = ReadFile("/sys/devices/system/cpu/cpu" + i + "/cpufreq/scaling_cur_freq", out _);
                if (long.TryParse(raw.Trim(), out long khz)) frequencies.Append(i).Append(':').Append(khz / 1000).Append(' ');
            }
            if (frequencies.Length > 0) v.cpuFrequencyMHz = frequencies.ToString().Trim();
            sysfs.Capture(v);
            using (var debug = new AndroidJavaClass("android.os.Debug"))
            using (var info = new AndroidJavaObject("android.os.Debug$MemoryInfo")) {
                debug.CallStatic("getMemoryInfo", info); v.processPssMB = info.Call<int>("getTotalPss") / 1024d; v.memoryStatus = "available; Android Debug.MemoryInfo PSS";
            }
            string mem = ReadFile("/proc/meminfo", out _);
            foreach (string line in mem.Split('\n')) {
                var parts = line.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length < 2 || !long.TryParse(parts[1], out long kb)) continue;
                if (parts[0] == "MemAvailable:") v.systemAvailableMB = kb / 1024d;
                if (parts[0] == "MemTotal:") v.systemTotalMB = kb / 1024d;
            }
            using (var unity = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var activity = unity.GetStatic<AndroidJavaObject>("currentActivity")) {
                using (var filter = new AndroidJavaObject("android.content.IntentFilter", "android.intent.action.BATTERY_CHANGED"))
                using (var intent = activity.Call<AndroidJavaObject>("registerReceiver", null, filter))
                    if (intent != null) { int t = intent.Call<int>("getIntExtra", "temperature", -1); v.batteryTemperatureC = t < 0 ? -1 : t / 10d; }
                using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                    if (version.GetStatic<int>("SDK_INT") >= 29)
                        using (var power = activity.Call<AndroidJavaObject>("getSystemService", "power"))
                            v.thermalStatus = "PowerManager thermal status=" + power.Call<int>("getCurrentThermalStatus");
            }
#else
            using (var process = Process.GetCurrentProcess()) {
                long ticks = process.TotalProcessorTime.Ticks; v.clockTicksPerSecond = TimeSpan.TicksPerSecond;
                v.cpuSource = "Process.TotalProcessorTime / logical cores";
                v.appCpuPercent = HardwareMetrics.CpuPercent(ticks, previousProcess, v.intervalSeconds, v.clockTicksPerSecond, cores);
                v.appCpuOneCorePercent = v.appCpuPercent < 0 ? -1 : v.appCpuPercent * cores;
                v.cpuStatus = previousProcess < 0 ? "warm-up; need next interval" : "available"; previousProcess = ticks;
                long rss = process.WorkingSet64; v.processRssMB = rss > 0 ? rss / 1048576d : -1;
                v.memoryStatus = rss > 0 ? "available; process working set (not PSS)" : "process working set unavailable from this Mono runtime";
            }
            v.gpuStatus = "GPU utilization unavailable from Unity cross-platform API";
            v.systemCpuStatus = "System CPU utilization unavailable on this sampler platform";
#endif
        }
        public void Dispose()
        {
            stop.Set(); if (worker != null && worker.Join(1500)) { stop.Dispose(); worker = null; }
        }
    }
}
