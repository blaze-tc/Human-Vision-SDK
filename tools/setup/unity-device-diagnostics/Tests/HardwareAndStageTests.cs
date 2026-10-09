using System;
using System.Globalization;
using System.Linq;
using System.Reflection;
using NUnit.Framework;

namespace HumanVision.TestProject.Tests
{
    /// <summary>硬件计数/原生阶段必须有真实来源和有效位，缺失不能伪装为零负载。</summary>
    public sealed class HardwareAndStageTests
    {
        private static Type Find(string name)
        {
            var type = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.TestProject.Diagnostics." + name)).FirstOrDefault(t => t != null);
            Assert.That(type, Is.Not.Null, "Missing device hardware/stage implementation: " + name);
            return type;
        }
        private static object Static(string name, params object[] args) => Find("HardwareMetrics").GetMethod(name).Invoke(null, args);
        private static T Field<T>(object value, string field) => (T)value.GetType().GetField(field).GetValue(value);

        [Test] public void ProcessCpuIncludesWorkerThreadsAndHandlesSpacesInCommandName()
        {
            string stat = "123 (Unity player ) worker) R 0 0 0 0 0 0 0 0 0 0 41 59 0 0 0";
            Assert.That((long)Static("ProcessTicks", stat), Is.EqualTo(100));
        }
        [Test] public void SystemCpuExcludesGuestDoubleCounting()
        {
            var value = Static("SystemTicks", "cpu 20 5 10 50 5 5 5 0 9 2\ncpu0 1 2 3");
            Assert.That(Field<long>(value, "total"), Is.EqualTo(100));
            Assert.That(Field<long>(value, "idle"), Is.EqualTo(55));
        }
        [TestCase(1, 100d)] [TestCase(8, 12.5d)]
        public void AppCpuDenominatorExplicitlyUsesLogicalCoreCapacity(int cores, double expected)
            => Assert.That((double)Static("CpuPercent", 200L, 100L, 1d, 100L, cores), Is.EqualTo(expected).Within(.0001));
        [Test] public void CounterResetAndMissingIntervalAreUnavailable()
        {
            Assert.That((double)Static("CpuPercent", 50L, 100L, 1d, 100L, 8), Is.EqualTo(-1));
            Assert.That((double)Static("CpuPercent", 100L, 0L, 0d, 100L, 8), Is.EqualTo(-1));
        }
        [TestCase("25 100", 25d)] [TestCase("0 100", 0d)]
        [TestCase("1 0", -1d)] [TestCase("101 100", -1d)] [TestCase("unreadable", -1d)]
        public void VendorGpuBusyUsesReportedIntervalAndRejectsInvalidCounters(string raw, double expected)
            => Assert.That((double)Static("KgslPercent", raw), Is.EqualTo(expected));
        [Test] public void UnavailableHardwareHasStatusAndSentinelInsteadOfZero()
        {
            var type = Find("HardwareSample"); var sample = Activator.CreateInstance(type);
            Assert.That(Field<double>(sample, "systemCpuPercent"), Is.EqualTo(-1));
            Assert.That(Field<double>(sample, "gpuPercent"), Is.EqualTo(-1));
            Assert.That(Field<string>(sample, "gpuStatus"), Is.Not.Empty);
        }
        [Test] public void StageSnapshotRequiresAllSixRealStagesOfSameFrame()
        {
            var type = Find("NativeStageTracker"); var tracker = Activator.CreateInstance(type);
            foreach (string stage in new[] { "import_preprocess_record", "preprocess_submit_wait", "extract_download_elapsed", "inference_submit_wait", "dense_output_copy" })
                type.GetMethod("Accept").Invoke(tracker, new object[] { "run_raw stage=" + stage + " frame_id=42 elapsed_us=2000" });
            Assert.That(Field<bool>(type.GetMethod("Read").Invoke(tracker, null), "available"), Is.False);
            type.GetMethod("Accept").Invoke(tracker, new object[] { "run_raw stage=ownership_release_wait frame_id=42 elapsed_us=500" });
            var snapshot = type.GetMethod("Read").Invoke(tracker, null);
            Assert.That(Field<bool>(snapshot, "available"), Is.True);
            Assert.That(Field<long>(snapshot, "frameId"), Is.EqualTo(42));
            Assert.That(Field<double>(snapshot, "ownershipReleaseMs"), Is.EqualTo(.5));
            Assert.That(Field<string>(snapshot, "modelMeasurement"), Does.Contain("includes").And.Contain("wait"));
        }
        [Test] public void InterleavedOrMalformedStageRecordsDoNotFabricateCompletion()
        {
            var type = Find("NativeStageTracker"); var tracker = Activator.CreateInstance(type);
            type.GetMethod("Accept").Invoke(tracker, new object[] { "run_raw stage=extract_download_elapsed frame_id=1 elapsed_us=35000" });
            type.GetMethod("Accept").Invoke(tracker, new object[] { "run_raw stage=ownership_release_wait frame_id=2 elapsed_us=200" });
            type.GetMethod("Accept").Invoke(tracker, new object[] { "run_raw stage=preprocess_submit_wait frame_id=1 elapsed_us=-200" });
            Assert.That(Field<bool>(type.GetMethod("Read").Invoke(tracker, null), "available"), Is.False);
        }
    }
}
