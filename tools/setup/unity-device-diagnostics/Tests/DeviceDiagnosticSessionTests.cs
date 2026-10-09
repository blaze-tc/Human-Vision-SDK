using System;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Threading;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.TestProject.Tests
{
    // 通过反射启动测试，缺少实现时应产生明确的断言失败，而不是编译失败。
    public sealed class DeviceDiagnosticSessionTests
    {
        private string root;
        private object session;
        private Type type;

        [SetUp] public void Setup()
        {
            root = Path.Combine(Path.GetTempPath(), "hv-settings-diagnostics-test-" + Guid.NewGuid().ToString("N"));
            type = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.TestProject.Diagnostics.DeviceDiagnosticSession"))
                .FirstOrDefault(t => t != null);
            Assert.That(type, Is.Not.Null, "HumanVisionSettingsDemo 缺少实机诊断会话实现。");
            session = Activator.CreateInstance(type, root, 2048, 64);
        }
        [TearDown] public void Cleanup()
        {
            (session as IDisposable)?.Dispose();
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
        private object Call(string name, params object[] args) => type.GetMethod(name).Invoke(session, args);
        private T Property<T>(string name) => (T)type.GetProperty(name).GetValue(session);
        private string Read(string pattern) => string.Join("\n", Directory.GetFiles(Property<string>("SessionPath"), pattern).Select(File.ReadAllText));

        [Test] public void HardwareAndStageEvidenceIsAvailableAsPlainFilesInTheSessionFolder()
        {
            Assert.DoesNotThrow(() => Call("WriteJsonLine", "hardware.jsonl", "{\"gpuAvailable\":false,\"gpuPercent\":-1}"));
            Assert.DoesNotThrow(() => Call("WriteJsonLine", "timings.jsonl", "{\"modelIncludesGpuWait\":true,\"frameId\":42}"));
            Call("Flush", 64);
            Assert.That(Read("hardware-*.jsonl"), Does.Contain("gpuAvailable"));
            Assert.That(Read("timings-*.jsonl"), Does.Contain("modelIncludesGpuWait"));
        }

        [Test] public void BundledVideoUrlUsesApkSchemeAndEscapesEachPathSegment()
        {
            var resolver = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.TestProject.Diagnostics.HumanVisionSettingsBundledVideos"))
                .First(t => t != null).GetMethod("ResolvePath");
            Assert.That((string)resolver.Invoke(null, new object[] { "jar:file:///test.apk!/assets", "clips/video one.mp4" }),
                Is.EqualTo("jar:file:///test.apk!/assets/clips/video%20one.mp4"));
            Assert.That((string)resolver.Invoke(null, new object[] { root, "clips/video one.mp4" }),
                Is.EqualTo(Path.Combine(root, "clips", "video one.mp4")));
        }
        [Test] public void BundledVideoCatalogRejectsPathsOutsideStreamingAssets()
        {
            var resolver = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType("HumanVision.TestProject.Diagnostics.HumanVisionSettingsBundledVideos"))
                .First(t => t != null).GetMethod("ResolvePath");
            foreach (string path in new[] { "../outside.mp4", "/absolute.mp4", "C:/outside.mp4", "clips/../outside.mp4" }) {
                var failure = Assert.Throws<TargetInvocationException>(() => resolver.Invoke(null, new object[] { "jar:file:///test.apk!/assets", path }));
                Assert.That(failure.InnerException, Is.TypeOf<ArgumentException>());
            }
        }

        [Test] public void ThreadedUnityLogsCaptureAllLevelsStackAndRedactCredentials()
        {
            Exception failure = null;
            var worker = new Thread(() => {
                try { Call("EnqueueUnity", "普通日志 rtsp://operator:privatepass@camera/live?token=privatetoken", "stack: worker\nline 42", "Log", 17); }
                catch (Exception e) { failure = e; }
            });
            worker.Start(); Assert.That(worker.Join(5000), Is.True); Assert.That(failure, Is.Null);
            Call("Flush", 64);
            string logs = Read("unity-*.log");
            Assert.That(logs, Does.Contain("普通日志").And.Contain("thread=17").And.Contain("line 42"));
            Assert.That(logs, Does.Not.Contain("privatepass").And.Not.Contain("privatetoken"));
            Assert.That(Read("events-*.jsonl"), Does.Contain("unity.Log"));
        }
        [Test] public void NativeStagesAreExportedSeparatelyWithRedaction()
        {
            Assert.That(type.GetMethod("EnqueueNative"), Is.Not.Null, "设备 ZIP 必须包含 Android 原生分段计时，不能只依赖电脑 adb。");
            Call("EnqueueNative", "run_raw stage=preprocess_submit_wait frame_id=42 elapsed_us=1500 rtsp://user:privatepass@camera/live?token=privatetoken", 19);
            string zip = (string)Call("Export", "", Array.Empty<string>());
            string logs = Read("native-*.log");
            Assert.That(logs, Does.Contain("preprocess_submit_wait").And.Contain("elapsed_us=1500").And.Contain("thread=19"));
            Assert.That(logs, Does.Not.Contain("privatepass").And.Not.Contain("privatetoken"));
            Assert.That(Read("events-*.jsonl"), Does.Contain("native.android"));
            using (var archive = ZipFile.OpenRead(zip))
                Assert.That(archive.Entries.Any(e => e.Name.StartsWith("native-")), Is.True);
        }
        [Test] public void NativeStageFloodIsBoundedAndDoesNotDisplaceCriticalUnityError()
        {
            Assert.That(type.GetMethod("EnqueueNative"), Is.Not.Null);
            Call("EnqueueUnity", "critical runtime error", "actual stack", "Error", 3);
            for (int i = 0; i < 100; ++i) Call("EnqueueNative", "GPU layer " + i, 19);
            Call("Flush", 256);
            Assert.That(Read("unity-*.log"), Does.Contain("critical runtime error"));
            Assert.That(Read("native-*.log"), Does.Contain("GPU layer 0"));
            Assert.That(Property<long>("DroppedNativeMessages"), Is.EqualTo(37));
            Assert.That(Property<long>("DroppedUnityMessages"), Is.Zero);
        }
        [Test] public void QueueOverflowIsCountedAndRotationKeepsBoundedFiles()
        {
            for (int i = 0; i < 100; i++) Call("EnqueueUnity", "ordinary " + i, "", "Log", 1);
            Assert.That(Property<long>("DroppedUnityMessages"), Is.EqualTo(36));
            Call("Flush", 64);
            for (int i = 0; i < 60; i++) Call("Record", "test", i + ":" + new string('x', 180));
            Call("Flush", 64);
            Assert.That(Directory.GetFiles(Property<string>("SessionPath"), "events-*.jsonl").Length, Is.InRange(2, 4));
            Assert.That(Read("events-*.jsonl"), Does.Contain("59:"));
        }
        [Test] public void QuotedJsonCredentialsAreFullyRedactedIncludingSpacesAndEscapedQuotes()
        {
            Call("Record", "configuration", "{\"password\":\"private words\",\"access_token\":\"encoded\\\"credential\"}");
            Call("Flush", 64);
            string logs = Read("events-*.jsonl");
            foreach (string secret in new[] { "private", "words", "encoded", "credential" }) Assert.That(logs, Does.Not.Contain(secret));
        }
        [Test] public void SevereUnityErrorIsRetainedWhenOrdinaryLogQueueIsFull()
        {
            for (int i = 0; i < 64; i++) Call("EnqueueUnity", "ordinary " + i, "", "Log", 1);
            Call("EnqueueUnity", "critical diagnostic", "actual stack", "Error", 3);
            Call("Flush", 64);
            Assert.That(Read("unity-*.log"), Does.Contain("critical diagnostic").And.Contain("actual stack"));
            Assert.That(Property<long>("DroppedUnityMessages"), Is.EqualTo(1));
        }
        [Test] public void CsvUsesInvariantNumbersAndEscapesNewlinesAndCommas()
        {
            var previous = CultureInfo.CurrentCulture;
            try {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
                Call("SetCsvHeader", new object[] { new[] { "seconds", "error" } });
                Call("WriteCsvRow", new object[] { new[] { 1.25.ToString(CultureInfo.InvariantCulture), "codec, failure\nnext line" } });
                Call("Flush", 64);
                Assert.That(Read("performance-*.csv"), Does.Contain("seconds,error").And.Contain("1.25,\"codec, failure\\nnext line\""));
            } finally { CultureInfo.CurrentCulture = previous; }
        }
        [Test] public void ExportFlushesPendingLogsAndContainsSdkDiagnostics()
        {
            string sdkRoot = Path.Combine(root, "sdk-logs"), sdkSession = Path.Combine(sdkRoot, "session-a");
            Directory.CreateDirectory(sdkSession); File.WriteAllText(Path.Combine(sdkSession, "diagnostics-0.log"), "actual SDK error");
            Call("EnqueueUnity", "pending error", "stack", "Error", 3);
            string zip = (string)Call("Export", sdkRoot, new[] { sdkSession });
            using (var archive = ZipFile.OpenRead(zip)) {
                Assert.That(archive.Entries.Any(e => e.FullName.StartsWith("sdk-settings/session-a/")), Is.True);
                var events = archive.Entries.Where(e => e.Name.StartsWith("events-")).ToArray();
                Assert.That(events.Length, Is.GreaterThan(0));
                using (var reader = new StreamReader(events[0].Open())) Assert.That(reader.ReadToEnd(), Does.Contain("pending error"));
            }
            Assert.That(Property<string>("LastWriteError"), Is.Empty);
        }
        [Test] public void WriteFailureRemainsActionableWithoutThrowingInUnityCallback()
        {
            ((IDisposable)session).Dispose();
            string blocked = Path.Combine(root, "blocked"); File.WriteAllText(blocked, "file");
            session = Activator.CreateInstance(type, blocked, 2048, 64);
            Assert.That(Property<string>("LastWriteError"), Is.Not.Empty);
            Assert.DoesNotThrow(() => Call("EnqueueUnity", "still reporting", "", "Error", 1));
            Assert.DoesNotThrow(() => Call("Flush", 64));
        }
    }
}
