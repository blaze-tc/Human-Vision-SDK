using System;
using System.IO;
using System.Linq;
using System.Reflection;
using NUnit.Framework;

namespace HumanVision.TestProject.Tests
{
    /// <summary>覆盖现场 RK3588 的负载格式、节点名称和重复落盘问题；测试不依赖真实 sysfs 权限。</summary>
    public sealed class Rk3588DiagnosticsTests
    {
        private static Type Find(string name)
        {
            var type = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.TestProject.Diagnostics." + name)).FirstOrDefault(t => t != null);
            Assert.That(type, Is.Not.Null, "Missing RK3588 diagnostics implementation: " + name);
            return type;
        }
        private static object Static(string method, params object[] args)
        {
            var function = Find("HardwareMetrics").GetMethod(method);
            Assert.That(function, Is.Not.Null, "Missing vendor-format parser: " + method);
            return function.Invoke(null, args);
        }
        private static T Field<T>(object value, string name) => (T)value.GetType().GetField(name).GetValue(value);

        [TestCase("72@800000000Hz\n", 72d)]
        [TestCase("0@200000000Hz", 0d)]
        [TestCase("100%", 100d)]
        [TestCase("26", 26d)]
        [TestCase("101@800000000Hz", -1d)]
        [TestCase("NaN", -1d)]
        [TestCase("72@badHz", -1d)]
        public void RockchipDevfreqLoadUsesPercentBeforeFrequency(string raw, double expected)
            => Assert.That((double)Static("DevfreqLoadPercent", raw), Is.EqualTo(expected));

        [TestCase("72@800000000Hz", 800d)]
        [TestCase("72", -1d)]
        [TestCase("72@badHz", -1d)]
        public void EmbeddedGpuClockHasExplicitHertzUnits(string raw, double expected)
            => Assert.That((double)Static("DevfreqFrequencyMHz", raw), Is.EqualTo(expected));

        [Test] public void DiscoversMaliNamedNodeAndKeepsThermalSensorsSeparateFromBattery()
        {
            string root = Path.Combine(Path.GetTempPath(), "hv-rk-sysfs-" + Guid.NewGuid().ToString("N"));
            try {
                string gpu = Path.Combine(root, "devfreq", "fb000000.gpu-mali"), npu = Path.Combine(root, "devfreq", "fdab0000.npu");
                string gpuThermal = Path.Combine(root, "thermal", "thermal_zone1"), cpuThermal = Path.Combine(root, "thermal", "thermal_zone2");
                foreach (string directory in new[] { gpu, npu, gpuThermal, cpuThermal }) Directory.CreateDirectory(directory);
                File.WriteAllText(Path.Combine(gpu, "load"), "72@800000000Hz\n");
                File.WriteAllText(Path.Combine(gpu, "cur_freq"), "800000000\n");
                File.WriteAllText(Path.Combine(npu, "load"), "99@1000000000Hz");
                File.WriteAllText(Path.Combine(gpuThermal, "type"), "gpu-thermal"); File.WriteAllText(Path.Combine(gpuThermal, "temp"), "64000");
                File.WriteAllText(Path.Combine(cpuThermal, "type"), "bigcore0-thermal"); File.WriteAllText(Path.Combine(cpuThermal, "temp"), "71000");
                object sample = Activator.CreateInstance(Find("HardwareSample"));
                object sampler = Activator.CreateInstance(Find("DeviceSysfsTelemetry"), Path.Combine(root, "devfreq"), Path.Combine(root, "thermal"), Path.Combine(root, "missing-kgsl"));
                sampler.GetType().GetMethod("Capture").Invoke(sampler, new[] { sample });
                Assert.That(Field<double>(sample, "gpuPercent"), Is.EqualTo(72));
                Assert.That(Field<double>(sample, "gpuFrequencyMHz"), Is.EqualTo(800));
                Assert.That(Field<string>(sample, "gpuSource"), Is.EqualTo(Path.Combine(gpu, "load")));
                Assert.That(Field<double>(sample, "gpuTemperatureC"), Is.EqualTo(64));
                Assert.That(Field<double>(sample, "cpuTemperatureC"), Is.EqualTo(71));
                Assert.That(Field<double>(sample, "batteryTemperatureC"), Is.EqualTo(-1));
            } finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
        }

        [Test] public void ReadableClockIsReportedEvenWhenGpuLoadIsUnavailable()
        {
            string root = Path.Combine(Path.GetTempPath(), "hv-rk-clock-" + Guid.NewGuid().ToString("N"));
            try {
                string gpu = Path.Combine(root, "devfreq", "fb000000.gpu-mali");
                Directory.CreateDirectory(gpu);
                File.WriteAllText(Path.Combine(gpu, "cur_freq"), "800000000\n");
                object sample = Activator.CreateInstance(Find("HardwareSample"));
                object sampler = Activator.CreateInstance(Find("DeviceSysfsTelemetry"), Path.Combine(root, "devfreq"), root, root);
                sampler.GetType().GetMethod("Capture").Invoke(sampler, new[] { sample });
                Assert.That(Field<double>(sample, "gpuPercent"), Is.EqualTo(-1));
                Assert.That(Field<double>(sample, "gpuFrequencyMHz"), Is.EqualTo(800));
                Assert.That(Field<string>(sample, "gpuFrequencySource"), Is.EqualTo(Path.Combine(gpu, "cur_freq")));
                Assert.That(Field<string>(sample, "gpuStatus"), Does.Contain("unavailable"));
            } finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
        }

        [Test] public void UnavailableMaliNodesReportProbeFailuresWithoutInventingZeroLoad()
        {
            string root = Path.Combine(Path.GetTempPath(), "hv-missing-" + Guid.NewGuid().ToString("N"));
            object sample = Activator.CreateInstance(Find("HardwareSample"));
            object sampler = Activator.CreateInstance(Find("DeviceSysfsTelemetry"), root, root, root);
            sampler.GetType().GetMethod("Capture").Invoke(sampler, new[] { sample });
            Assert.That(Field<double>(sample, "gpuPercent"), Is.EqualTo(-1));
            Assert.That(Field<double>(sample, "gpuFrequencyMHz"), Is.EqualTo(-1));
            Assert.That(Field<double>(sample, "gpuTemperatureC"), Is.EqualTo(-1));
            Assert.That(Field<string>(sample, "gpuStatus"), Does.Contain("unavailable"));
        }

        [Test] public void NativeDetailsAreStoredOnceWithBatchIndexAndMeasuredFlushCost()
        {
            string root = Path.Combine(Path.GetTempPath(), "hv-native-batch-" + Guid.NewGuid().ToString("N"));
            object session = Activator.CreateInstance(Find("DeviceDiagnosticSession"), root, 8192, 64);
            var type = session.GetType();
            try {
                for (int i = 0; i < 32; i++) type.GetMethod("EnqueueNative").Invoke(session, new object[] { "HVInputGate: gpu_color_completed sequence=" + i + " received_us=1000 decoded_us=2500", 9 });
                type.GetMethod("EnqueueUnity").Invoke(session, new object[] { "critical diagnostic", "actual stack", "Error", 1 });
                type.GetMethod("Flush").Invoke(session, new object[] { 64 });
                string path = (string)type.GetProperty("SessionPath").GetValue(session);
                string events = string.Join("\n", Directory.GetFiles(path, "events-*.jsonl").Select(File.ReadAllText));
                string native = string.Join("\n", Directory.GetFiles(path, "native-*.log").Select(File.ReadAllText));
                Assert.That(native, Does.Contain("sequence=31"));
                Assert.That(events, Does.Not.Contain("gpu_color_completed"));
                Assert.That(events, Does.Contain("native.batch").And.Contain("lines=32").And.Contain("critical diagnostic"));
                var cost = type.GetProperty("LastFlushMilliseconds"); Assert.That(cost, Is.Not.Null);
                Assert.That((double)cost.GetValue(session), Is.GreaterThanOrEqualTo(0));
            } finally { (session as IDisposable)?.Dispose(); if (Directory.Exists(root)) Directory.Delete(root, true); }
        }
    }
}
