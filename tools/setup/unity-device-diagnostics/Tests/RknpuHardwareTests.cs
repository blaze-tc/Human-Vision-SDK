using System;
using System.IO;
using System.Linq;
using System.Reflection;
using NUnit.Framework;

namespace HumanVision.TestProject.Tests
{
    /// <summary>验证真实 Rockchip 驱动格式、三核负载及缺失指标；不把测试文件当成设备测量。</summary>
    public sealed class RknpuHardwareTests
    {
        private static Type Find(string name) => AppDomain.CurrentDomain.GetAssemblies()
            .Select(a => a.GetType("HumanVision.TestProject.Diagnostics." + name)).First(t => t != null);
        private static T Field<T>(object value, string name)
        {
            var field = value.GetType().GetField(name);
            Assert.That(field, Is.Not.Null, "Missing hardware field: " + name);
            return (T)field.GetValue(value);
        }
        private static double[] Loads(string raw)
        {
            var method = Find("HardwareMetrics").GetMethod("RknpuCoreLoads");
            Assert.That(method, Is.Not.Null, "Missing real driver-format parser");
            return (double[])method.Invoke(null, new object[] { raw });
        }
        [Test] public void ThreeCoreDriverIntervalKeepsIndividualLoads()
            => Assert.That(Loads("NPU load:  Core0:  25%, Core1:  50%, Core2: 100%,\n"), Is.EqualTo(new[] { 25d, 50d, 100d }));
        [Test] public void SingleCoreZeroIsAValidMeasuredValue()
            => Assert.That(Loads("NPU load:  0%\n"), Is.EqualTo(new[] { 0d }));
        [Test] public void DriverBlankPrintedZeroDoesNotLookUnavailable()
            => Assert.That(Loads("NPU load: Core0:   %, Core1: 25%, Core2:   %,\n"), Is.EqualTo(new[] { 0d, 25d, 0d }));
        [TestCase("NPU load: Core0: 429496%, Core1: 0%, Core2: 0%,")]
        [TestCase("NPU load: Core0: 10%, Core0: 20%, Core2: 0%,")]
        [TestCase("NPU load: Core0: 10%, Core2: 0%,")]
        [TestCase("NPU load: Core0: -1%,")]
        [TestCase("NPU load: NaN%")]
        [TestCase("garbage 20%")]
        public void MalformedOrOutOfRangeLoadRemainsUnavailable(string raw)
            => Assert.That(Loads(raw), Is.Empty);
        [Test] public void DebugLoadAndFrequencyAreDeviceMetricsSeparateFromActiveBackend()
        {
            string root = Path.Combine(Path.GetTempPath(), "hv-rknpu-" + Guid.NewGuid().ToString("N"));
            try {
                string debug = Path.Combine(root, "debug"); Directory.CreateDirectory(debug);
                File.WriteAllText(Path.Combine(debug, "load"), "NPU load: Core0: 25%, Core1: 50%, Core2: 75%,\n");
                File.WriteAllText(Path.Combine(debug, "freq"), "1000000000\n");
                object sample = Activator.CreateInstance(Find("HardwareSample"));
                object telemetry = Activator.CreateInstance(Find("DeviceSysfsTelemetry"), root, root, root, debug, Path.Combine(root, "proc"));
                telemetry.GetType().GetMethod("Capture").Invoke(telemetry, new[] { sample });
                Assert.That(Field<double[]>(sample, "npuCorePercent"), Is.EqualTo(new[] { 25d, 50d, 75d }));
                Assert.That(Field<double>(sample, "npuPercent"), Is.EqualTo(50));
                Assert.That(Field<double>(sample, "npuFrequencyMHz"), Is.EqualTo(1000));
                Assert.That(Field<string>(sample, "npuSource"), Is.EqualTo(Path.Combine(debug, "load")));
                Assert.That(Field<string>(sample, "npuStatus"), Does.Contain("device").And.Contain("mean"));
                Assert.That(Field<bool>(sample, "npuActive"), Is.False);
            } finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
        }
        [Test] public void AbsentDriverReportsReasonAndDoesNotInventIdleLoad()
        {
            string root = Path.Combine(Path.GetTempPath(), "hv-rknpu-missing-" + Guid.NewGuid().ToString("N"));
            object sample = Activator.CreateInstance(Find("HardwareSample"));
            object telemetry = Activator.CreateInstance(Find("DeviceSysfsTelemetry"), root, root, root, root, root);
            telemetry.GetType().GetMethod("Capture").Invoke(telemetry, new[] { sample });
            Assert.That(Field<double>(sample, "npuPercent"), Is.EqualTo(-1));
            Assert.That(Field<string>(sample, "npuStatus"), Does.Contain("unavailable").And.Contain("load"));
        }
    }
}
