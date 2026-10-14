using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// 本轮封装的按需实机验证工具，不属于公共 SDK。只有 Android 的持久化目录中存在
    /// MainCpuVerification/request.json 时运行；不改写用户保存设置，不启动录屏。
    /// 同一 APK 的旧 CPU 与 YOLO CPU 使用相同视频、人数、置信度和输入链路。
    /// 原始 ResultUpdated 的新来源帧才计数；派生、预测及重复骨骼不计入完整 COCO-17 FPS。
    /// </summary>
    public sealed class HumanVisionMainCpuVerificationProbe : MonoBehaviour
    {
        [Serializable] private sealed class Request { public string id; public float seconds = 30, warmup = 8; }
        [Serializable] private sealed class RuntimeIndex { public string version; public Row[] files; }
        [Serializable] private sealed class Row { public string path, sha256; }
        [Serializable] private sealed class Seal { public int schema_version = 1; public string stage_id = "cpu-baseline-comparison", index_sha256; public bool local_evaluation_only = true; }
        [Serializable] private sealed class Receipt {
            public string id, utc, sdk, phase, state, profile, model, error, diagnosticsDirectory, nativeDiagnostics, runtimeRoot;
            public int width, height, maxBodies, imageWidth, imageHeight;
            public double seconds, rawFreshResultFps, newBodyResultFps, nativeInferenceFps, totalMs, poseMs, trackingMs;
            public long newSourceResults, bodyResults, invalidCoordinates;
            public FreshSkeletonCounter.Sample[] persons;
        }
        private HumanVisionSettingsController controller;
        private HumanVisionSettingsDeviceDiagnostics diagnostics;
        private readonly FreshSkeletonCounter persons = new FreshSkeletonCounter();
        private readonly HumanVisionCanonicalJoint[] joints = new HumanVisionCanonicalJoint[32];
        private static readonly int[] Coco = { 27, 28, 30, 29, 31, 5, 12, 6, 13, 7, 14, 18, 22, 19, 23, 20, 24 };
        private string directory, phase, baselineRoot;
        private HumanVisionSettingsData originalDraft;
        private HumanVisionSdkOptions originalRuntime;
        private CanvasGroup controls;
        private bool oldInteractable, controlsLocked, comparisonEnabled, failed;
        private HumanVisionCombinedBenchmark comparison;
        private string firstError;
        private bool measuring;
        private long previousFrame = -1, previousSequence = -1, resultCount, bodyCount, badCoordinates;
        private int previousSleep;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void StartIfRequested()
        {
            if (Application.platform != RuntimePlatform.Android) return;
            string path = Path.Combine(Application.persistentDataPath, "MainCpuVerification/request.json");
            if (File.Exists(path)) new GameObject("Main CPU verification (requested)").AddComponent<HumanVisionMainCpuVerificationProbe>();
        }
        private IEnumerator Start()
        {
            directory = Path.Combine(Application.persistentDataPath, "MainCpuVerification");
            Request request = JsonUtility.FromJson<Request>(File.ReadAllText(Path.Combine(directory, "request.json")));
            if (request == null || string.IsNullOrEmpty(request.id) || request.seconds < 10 || request.seconds > 120 || request.warmup < 3 || request.warmup > 30)
                throw new InvalidDataException("Invalid bounded verification request.");
            File.Move(Path.Combine(directory, "request.json"), Path.Combine(directory, "request-consumed-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff") + ".json"));
            previousSleep = Screen.sleepTimeout; Screen.sleepTimeout = SleepTimeout.NeverSleep;
            try {
                yield return Drive(Suite(request), request);
                if (failed && controller != null) {
                    yield return Drive(Recover(), request);
                    Write(new Receipt { id = request.id, phase = phase, state = "FAILED", error = firstError });
                }
            } finally {
                measuring = false;
                if (controller != null) { controller.Sdk.ResultUpdated -= OnResult; controller.OperationNotice = ""; }
                if (controlsLocked && controls != null) controls.interactable = oldInteractable;
                if (comparison != null) comparison.enabled = comparisonEnabled;
                diagnostics?.RecordBenchmark("interactive", failed ? "CPU comparison failed; settings unlocked; see verification receipt" : "CPU comparison completed; settings unlocked");
                Screen.sleepTimeout = previousSleep;
            }
        }
        // 捕获嵌套协程的错误，留下失败收据并释放所有迭代器；不把打开失败计作性能通过。
        private IEnumerator Drive(IEnumerator first, Request request)
        {
            var stack = new Stack<IEnumerator>(); stack.Push(first);
            try {
                while (stack.Count != 0) {
                    bool more = false; object current = null; Exception failure = null;
                    try { more = stack.Peek().MoveNext(); if (more) current = stack.Peek().Current; }
                    catch (Exception e) { failure = e; }
                    if (failure != null) { failed = true; measuring = false; firstError = (firstError ?? "") + failure + "\n"; Write(new Receipt { id = request.id, phase = phase, state = "FAILED", error = firstError }); yield break; }
                    if (!more) { (stack.Pop() as IDisposable)?.Dispose(); continue; }
                    if (current is IEnumerator nested) { stack.Push(nested); continue; }
                    yield return current;
                }
            } finally { while (stack.Count != 0) (stack.Pop() as IDisposable)?.Dispose(); }
        }
        private IEnumerator Suite(Request request)
        {
            double until = Time.realtimeSinceStartupAsDouble + 60;
            while (controller == null || controller.Busy || string.IsNullOrEmpty(controller.Sdk.RuntimeRootPath)) {
                controller = FindObjectOfType<HumanVisionSettingsController>();
                if (Time.realtimeSinceStartupAsDouble > until) throw new TimeoutException("Settings runtime preparation timed out.");
                yield return null;
            }
            diagnostics = FindObjectOfType<HumanVisionSettingsDeviceDiagnostics>();
            comparison = FindObjectOfType<HumanVisionCombinedBenchmark>();
            if (comparison != null) { comparisonEnabled = comparison.enabled; comparison.enabled = false; }
            originalDraft = controller.View.ReadDraft();
            originalRuntime = controller.Sdk.IsRunning ? controller.Sdk.ActiveConfiguration : null;
            controls = controller.View.GetComponent<CanvasGroup>() ?? controller.View.gameObject.AddComponent<CanvasGroup>();
            oldInteractable = controls.interactable; controlsLocked = true; controls.interactable = false;
            string normalRoot = controller.Sdk.RuntimeRootPath;
            yield return controller.Sdk.StopSdk();
            phase = "prepare-baseline"; Write(new Receipt { id = request.id, phase = phase, state = "PREPARING" });
            yield return PrepareBaseline(normalRoot);
            controller.Sdk.ResultUpdated += OnResult;
            var settings = originalDraft.Clone();
            settings.SourceKind = InputKind.Video; settings.Video.VideoPath = Path.Combine(Application.streamingAssetsPath, "video-1.mp4");
            settings.Video.Mirror = false; settings.AutoStart = false; settings.DetailedLogs = false;
            settings.Recognition.MaxBodies = 4; settings.Recognition.UseRegions = false;
            settings.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(4);
            settings.Recognition.MinimumJointConfidence = .35f;
            yield return Measure(request, settings, "legacy-cpu", HumanVisionAccelerationMode.Cpu, baselineRoot, "rtmo-t-416");
            yield return Measure(request, settings, "yolo-cpu", HumanVisionAccelerationMode.Cpu, normalRoot, "yolov8n-pose-512x288-ort");
            yield return Measure(request, settings, "vulkan-low", HumanVisionAccelerationMode.Graphics, normalRoot, "yolov8n-pose-rectangle512x288-fp32-quality-low-local");
            settings.Video.Mirror = true;
            yield return Measure(request, settings, "yolo-cpu-mirror", HumanVisionAccelerationMode.Cpu, normalRoot, "yolov8n-pose-512x288-ort");
            // 收据已保存，恢复界面为当前运行的 YOLO CPU；不调用 Save，不覆盖用户设置文件。
            settings.Video.Mirror = false; settings.RuntimeRoot = normalRoot;
            controller.Edit(() => settings); yield return controller.Apply(false);
            if (!controller.Sdk.IsRunning || !string.IsNullOrEmpty(controller.Sdk.LastError) ||
                controller.Sdk.ActiveModelPack != "yolov8n-pose-512x288-ort")
                throw new InvalidOperationException("Final YOLO session did not reopen: " + controller.Sdk.LastError);
            Write(new Receipt { id = request.id, phase = "complete", state = "COMPLETE", diagnosticsDirectory = diagnostics != null ? diagnostics.CurrentLogDirectory : "" });
        }
        private IEnumerator Recover()
        {
            yield return controller.Sdk.StopSdk();
            if (originalDraft != null) controller.Edit(() => originalDraft);
            if (originalRuntime != null) {
                yield return controller.Sdk.Initialize(originalRuntime);
                if (!controller.Sdk.IsRunning || !string.IsNullOrEmpty(controller.Sdk.LastError))
                    throw new InvalidOperationException("Previous session recovery failed: " + controller.Sdk.LastError);
            }
        }
        private IEnumerator PrepareBaseline(string source)
        {
            var text = Resources.Load<TextAsset>("HumanVisionMainCpuBaselineProfile");
            if (text == null || Hash(text.bytes) != "9969cf1b1a6a4400f21139b8c168188c92c39bcac2f964a19f2e0e3eb9090353")
                throw new InvalidDataException("Exact restored CPU profile is missing or changed.");
            baselineRoot = Path.Combine(directory, "baseline-runtime"); Directory.CreateDirectory(baselineRoot);
            var index = JsonUtility.FromJson<RuntimeIndex>(File.ReadAllText(Path.Combine(source, "index.json")));
            // 所有字节来自已校验的当前 Runtime，只替换旧 CPU 配置。没有额外模型或假结果。
            foreach (var row in index.files) {
                if (string.IsNullOrEmpty(row.path) || row.path.Contains("..") || Path.IsPathRooted(row.path) || row.path.Contains(":"))
                    throw new InvalidDataException("Invalid runtime index path.");
                string target = Path.Combine(baselineRoot, row.path); Directory.CreateDirectory(Path.GetDirectoryName(target));
                File.Copy(Path.Combine(source, row.path), target, true); yield return null;
            }
            File.WriteAllBytes(Path.Combine(baselineRoot, "profiles/android-cpu-nohands.json"), text.bytes);
            foreach (var row in index.files) if (row.path == "profiles/android-cpu-nohands.json") row.sha256 = Hash(text.bytes);
            byte[] data = System.Text.Encoding.UTF8.GetBytes(JsonUtility.ToJson(index, true));
            File.WriteAllBytes(Path.Combine(baselineRoot, "index.json"), data);
            File.WriteAllText(Path.Combine(baselineRoot, "staged-runtime.json"), JsonUtility.ToJson(new Seal { index_sha256 = Hash(data) }, true));
        }
        private IEnumerator Measure(Request request, HumanVisionSettingsData settings, string label, HumanVisionAccelerationMode mode, string root, string expectedModel)
        {
            phase = label; settings.RuntimeRoot = root; settings.SelectAcceleration(mode); settings.InputQuality = ModelInputQuality.Low;
            controller.OperationNotice = "CPU 封装对照：" + label + "，完成后自动解锁设置";
            controller.Edit(() => settings);
            diagnostics?.RecordBenchmark(label, "opening; identical video and 4 people; saved preferences unchanged");
            Write(new Receipt { id = request.id, phase = label, state = "OPENING" });
            yield return controller.Apply(false);
            var sdk = controller.Sdk;
            if (!sdk.IsRunning || !string.IsNullOrEmpty(sdk.LastError)) throw new InvalidOperationException(sdk.LastError);
            var expectedSize = label == "legacy-cpu" ? new Vector2Int(416, 416) : new Vector2Int(512, 288);
            if (sdk.ActiveModelPack != expectedModel || sdk.ActiveAccelerationMode != mode || sdk.AnalysisInputSize != expectedSize ||
                !string.Equals(Path.GetFullPath(sdk.RuntimeRootPath), Path.GetFullPath(root), StringComparison.Ordinal))
                throw new InvalidOperationException("Actual backend/model differs: " + sdk.RuntimeDiagnostics);
            yield return Wait(request.warmup);
            if (!Application.isFocused) throw new InvalidOperationException("Foreground unlocked app required for valid measurements.");
            // The ORT plugin reports its provider name (CPU), whereas the NCNN
            // host reports the registered backend ID. Both are actual native values.
            string backend = mode == HumanVisionAccelerationMode.Graphics ? "backend.ncnn.vulkan" : "CPU";
            string pipeline = mode == HumanVisionAccelerationMode.Graphics ? "pipeline.yolo.pose" : label == "legacy-cpu" ? "pipeline.rtmo" : "pipeline.yolo.cpu";
            string native = sdk.RuntimeDiagnostics;
            if (Array.IndexOf(native.Split('\n'), "Actual backend=" + backend) < 0 ||
                Array.IndexOf(native.Split('\n'), "Pipeline=" + pipeline) < 0)
                throw new InvalidOperationException("Native backend/pipeline differs: " + native);
            persons.Reset(); previousFrame = previousSequence = -1; resultCount = bodyCount = badCoordinates = 0;
            diagnostics?.RecordBenchmark(label, "MEASURE BEGIN; " + sdk.RuntimeDiagnostics);
            double start = Time.realtimeSinceStartupAsDouble, next = start;
            measuring = true;
            while (Time.realtimeSinceStartupAsDouble - start < request.seconds) {
                if (!Application.isFocused) throw new InvalidOperationException("App lost foreground; this measurement is invalid.");
                if (!sdk.IsRunning || !string.IsNullOrEmpty(sdk.LastError)) throw new InvalidOperationException("Runtime stopped while measuring: " + sdk.LastError);
                if (Time.realtimeSinceStartupAsDouble >= next) { next += 2; Write(Snapshot(request.id, "MEASURING", Time.realtimeSinceStartupAsDouble - start)); }
                yield return null;
            }
            measuring = false;
            var receipt = Snapshot(request.id, "MEASURED", Time.realtimeSinceStartupAsDouble - start);
            var rows = new List<FreshSkeletonCounter.Sample>(); persons.Drain(receipt.seconds, rows.Add); receipt.persons = rows.ToArray();
            Write(receipt); File.WriteAllText(Path.Combine(directory, label + ".json"), JsonUtility.ToJson(receipt, true));
            diagnostics?.RecordBenchmark(label, "MEASURE END; " + sdk.RuntimeDiagnostics);
            yield return Wait(2); // 截图观察窗口，绝不启动录屏。
        }
        private void OnResult(long sequence)
        {
            if (!measuring) return;
            var sdk = controller.Sdk;
            if (!sdk.HasFreshResult || sequence <= previousSequence || sdk.SourceFrameId <= previousFrame) return;
            previousSequence = sequence; previousFrame = sdk.SourceFrameId; ++resultCount;
            bool hasBody = false;
            for (int i = 0; i < sdk.GetMaxBodies(); ++i) {
                if (!sdk.CopySkeletonByIndex(i, joints, out var metadata)) continue;
                int observed = 0;
                foreach (int id in Coco) {
                    var joint = joints[id]; var p = joint.Position;
                    if (!p.Valid || p.IsDerived || joint.PredictionMilliseconds != 0 || joint.ObservationTimestampUs != metadata.ObservationTimestampUs) continue;
                    if (float.IsNaN(p.Pixel.x) || float.IsNaN(p.Pixel.y) || float.IsInfinity(p.Pixel.x) || float.IsInfinity(p.Pixel.y)) { ++badCoordinates; continue; }
                    ++observed;
                }
                if (observed > 0) { hasBody = true; persons.Observe(metadata.StableTrackId, sequence, metadata.SourceFrameId, metadata.ObservationTimestampUs, observed == 17, sdk.ResultAgeMilliseconds); }
            }
            if (hasBody) ++bodyCount;
        }
        private Receipt Snapshot(string id, string state, double seconds)
        {
            var sdk = controller.Sdk; var stats = sdk.Stats; var size = sdk.AnalysisInputSize; var image = sdk.GetColorImageTex();
            return new Receipt { id = id, phase = phase, state = state, profile = sdk.RuntimeProfile, model = sdk.ActiveModelPack,
                width = size.x, height = size.y, imageWidth = image != null ? image.width : 0, imageHeight = image != null ? image.height : 0,
                maxBodies = sdk.GetMaxBodies(), seconds = seconds, newSourceResults = resultCount, bodyResults = bodyCount,
                rawFreshResultFps = seconds > 0 ? resultCount / seconds : 0, newBodyResultFps = seconds > 0 ? bodyCount / seconds : 0,
                nativeInferenceFps = stats.InferenceFps, poseMs = stats.PoseMs, totalMs = stats.TotalMs, trackingMs = stats.TrackingMs, invalidCoordinates = badCoordinates,
                nativeDiagnostics = sdk.RuntimeDiagnostics, runtimeRoot = sdk.RuntimeRootPath };
        }
        private void Write(Receipt value)
        { value.utc = DateTime.UtcNow.ToString("O"); value.sdk = HumanVisionSdk.Version; File.WriteAllText(Path.Combine(directory, "progress.json"), JsonUtility.ToJson(value, true)); }
        private static IEnumerator Wait(float seconds) { double end = Time.realtimeSinceStartupAsDouble + seconds; while (Time.realtimeSinceStartupAsDouble < end) yield return null; }
        private static string Hash(byte[] bytes) { using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }
    }
}
