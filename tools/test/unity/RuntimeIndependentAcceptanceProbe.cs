using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using HumanVision;
using UnityEngine;
using UnityEngine.SceneManagement;
using Debug = UnityEngine.Debug;

// Test-project instrumentation. Samples counters only; it never drives inference.
[DefaultExecutionOrder(1000)]
public sealed class RuntimeIndependentAcceptanceProbe : MonoBehaviour
{
    private static string execution = "compatible";
    private static bool preview = true;
    private static int stallMs, stallAfter = 8, gpuPasses, cpuWorkers;
    private static bool disableConsumer;
    public static bool PreviewEnabled => preview;
    private static int requestedTargetFps;
    private static MethodInfo staticProgressMethod;
    private static readonly object[] staticProgressArgs = { null };
    private readonly Stopwatch timer = Stopwatch.StartNew();
    private readonly object[] progressArgs = { null };
    private readonly Dictionary<string, MemberInfo> members = new Dictionary<string, MemberInfo>();
    private HumanVisionSdk sdk;
    private HumanVisionRuntimeView view;
    private MethodInfo progressMethod;
    private long callbacks, deliveries, renders, deliveredSequence = -1, renderedSequence = -1;
    private double nextSample, readyAt = -1;
    private bool stalled, consumerDisabled;
    private volatile bool running;
    private Thread[] loads;
    private Material gpuMaterial;
    private RenderTexture gpuA, gpuB;
    private StreamWriter progressLog;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Install()
    {
        if (SceneManager.GetActiveScene().name != "RuntimePrefabAcceptance") return;
        execution = TextExtra("hv_independent_execution", "compatible");
        preview = !BoolExtra("hv_independent_preview_off", false);
        stallMs = IntExtra("hv_independent_stall_ms", 0);
        if (stallMs != 500 && stallMs != 2000) stallMs = 0;
        stallAfter = Math.Max(1, IntExtra("hv_independent_stall_after_s", 8));
        gpuPasses = Math.Max(0, Math.Min(16, IntExtra("hv_independent_gpu_passes", 0)));
        cpuWorkers = Math.Max(0, Math.Min(4, IntExtra("hv_independent_cpu_workers", 0)));
        disableConsumer = BoolExtra("hv_independent_disable_consumer", false);
        requestedTargetFps = IntExtra("hv_independent_target_fps", 0);
        new GameObject("Independent Runtime Acceptance Probe").AddComponent<RuntimeIndependentAcceptanceProbe>();
    }
    public static void ConfigureOptions(HumanVisionSdkOptions options)
    {
        var policy = options.GetType().GetField("ExecutionPolicy");
        if (policy != null) policy.SetValue(options, Enum.Parse(policy.FieldType,
            execution == "native" ? "IndependentRequired" : "CompatibleUnityInput"));
        else if (execution == "native") Debug.LogError("Independent execution requested but this SDK has no execution policy.");
        options.GetType().GetField("PreviewEnabled")?.SetValue(options, preview);
        if (requestedTargetFps == 15 || requestedTargetFps == 30 || requestedTargetFps == 60)
            Application.targetFrameRate = requestedTargetFps;
    }
    // A native input has no Unity-owned analysis Texture. Use its actual input
    // counter for readiness/watchdog checks instead of substituting the HD copy.
    public static bool TryNativeInput(HumanVisionSdk sdk, out long frameId, out ulong sourceId, out ulong generation)
    {
        frameId = -1; sourceId = generation = 0;
        if (sdk == null) return false;
        if (staticProgressMethod == null) staticProgressMethod = sdk.GetType().GetMethod("TryGetRuntimeProgress");
        if (staticProgressMethod == null || !(bool)staticProgressMethod.Invoke(sdk, staticProgressArgs)) return false;
        var progress = staticProgressArgs[0]; var type = progress.GetType();
        var input = type.GetField("InputFrameId")?.GetValue(progress);
        var source = type.GetField("SourceId")?.GetValue(progress);
        var epoch = type.GetField("Generation")?.GetValue(progress);
        if (input == null || source == null || epoch == null) return false;
        frameId = checked((long)Convert.ToUInt64(input)); sourceId = Convert.ToUInt64(source); generation = Convert.ToUInt64(epoch);
        return frameId > 0 && sourceId != 0 && generation != 0;
    }
    public static bool NativeInputReady(HumanVisionSdk sdk) => TryNativeInput(sdk, out _, out _, out _);
    private void Start()
    {
        var directory = Path.Combine(Application.persistentDataPath, "RuntimePrefabDiagnostics");
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "independent-progress-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".log");
        progressLog = new StreamWriter(path, false, new UTF8Encoding(false));
        Debug.Log("HV_INDEPENDENT_DIAGNOSTICS session=" + path);
        running = true;
        loads = new Thread[cpuWorkers];
        for (int i = 0; i < loads.Length; i++) {
            loads[i] = new Thread(() => { double value = 1; while (running) {
                var end = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 100;
                while (running && Stopwatch.GetTimestamp() < end) value = Math.Sin(value) + 1.00001;
                Thread.Sleep(1);
            } }) { IsBackground = true, Name = "Acceptance CPU load" };
            loads[i].Start();
        }
        if (gpuPasses > 0) {
            var shader = Shader.Find("Hidden/HumanVision/IndependentAcceptanceLoad");
            if (shader == null) Debug.LogError("Acceptance GPU load shader missing; GPU load was not applied.");
            else {
                gpuMaterial = new Material(shader);
                gpuA = new RenderTexture(768, 768, 0); gpuB = new RenderTexture(768, 768, 0);
                gpuA.Create(); gpuB.Create();
            }
        }
    }
    private void OnResult(long sequence) { callbacks++; if (sequence != deliveredSequence) { deliveredSequence = sequence; deliveries++; } }
    private void OnPresented(HumanVisionPresentedFrame frame) { if (frame.ResultSequence != renderedSequence) { renderedSequence = frame.ResultSequence; renders++; } }
    private void Update()
    {
        if (sdk == null) {
            sdk = FindObjectOfType<HumanVisionSdk>();
            if (sdk != null) { sdk.ResultUpdated += OnResult; progressMethod = sdk.GetType().GetMethod("TryGetRuntimeProgress"); }
        }
        if (view == null) { view = FindObjectOfType<HumanVisionRuntimeView>(); if (view != null) view.FramePresented += OnPresented; }
        if (sdk == null) return;
        double now = timer.Elapsed.TotalSeconds;
        if (sdk.IsRunning && readyAt < 0) readyAt = now;
        if (readyAt >= 0 && disableConsumer && !consumerDisabled) {
            var manager = sdk.GetType().GetField("manager", BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(sdk) as Behaviour;
            if (manager != null) { manager.enabled = false; if (view != null) view.enabled = false; consumerDisabled = true; }
        }
        if (gpuMaterial != null) for (int i = 0; i < gpuPasses; i++) { Graphics.Blit(gpuA, gpuB, gpuMaterial); var swap = gpuA; gpuA = gpuB; gpuB = swap; }
        if (readyAt >= 0 && stallMs > 0 && !stalled && now - readyAt >= stallAfter) {
            stalled = true;
            Emit("stall_begin"); // Fresh native query immediately before the sleep.
            Thread.Sleep(stallMs);
            Emit("stall_end"); // Fresh native query before any Unity consumer can resume.
        }
        if (now >= nextSample) { nextSample = now + 1; Emit("sample"); }
    }
    private object Progress()
    {
        if (progressMethod == null) return null;
        try {
            if (!(bool)progressMethod.Invoke(sdk, progressArgs)) return null;
            return progressArgs[0];
        } catch (Exception e) { Debug.LogError("Independent progress query failed: " + e.GetBaseException().Message); return null; }
    }
    private object Value(object progress, string name)
    {
        if (progress == null) return null;
        if (!members.TryGetValue(name, out var member)) {
            member = (MemberInfo)progress.GetType().GetField(name) ?? progress.GetType().GetProperty(name);
            members[name] = member;
        }
        return member is FieldInfo f ? f.GetValue(progress) : member is PropertyInfo p ? p.GetValue(progress, null) : null;
    }
    private void Emit(string kind)
    {
        var progress = Progress();
        var text = new StringBuilder("{\"kind\":\"").Append(kind).Append("\",\"elapsed_us\":").Append(timer.ElapsedTicks * 1000000L / Stopwatch.Frequency);
        Add(text, "source_id", Value(progress, "SourceId")); Add(text, "generation", Value(progress, "Generation"));
        Add(text, "frame_id", Value(progress, "FrameId")); Add(text, "config_epoch", Value(progress, "ConfigEpoch"));
        Add(text, "region_revision", Value(progress, "RegionRevision"));
        HumanVisionSourceProgress sourceProgress = default;
        bool sourceAvailable = sdk != null && sdk.TryGetSourceProgress(out sourceProgress);
        Add(text, "source_progress_available", sourceAvailable);
        Add(text, "source_timestamp_us", sourceAvailable ? (object)sourceProgress.TimestampUs : null);
        Add(text, "source_generation", sourceAvailable ? (object)sourceProgress.Generation : null);
        Add(text, "source_decoded_frames", sourceAvailable ? (object)sourceProgress.DecodedFrames : null);
        Add(text, "source_acquired_frames", sourceAvailable ? (object)sourceProgress.AcquiredFrames : null);
        Add(text, "source_live_frames", sourceAvailable ? (object)sourceProgress.LivePublishedFrames : null);
        Add(text, "source_analysis_frames", sourceAvailable ? (object)sourceProgress.AnalysisPublishedFrames : null);
        Add(text, "source_claimed_frames", sourceAvailable ? (object)sourceProgress.AnalysisClaimedFrames : null);
        Add(text, "source_live_dropped_frames", sourceAvailable ? (object)sourceProgress.LiveDroppedFrames : null);
        Add(text, "source_live_width", sourceAvailable ? (object)sourceProgress.LiveWidth : null);
        Add(text, "source_live_height", sourceAvailable ? (object)sourceProgress.LiveHeight : null);
        Add(text, "native_input_frames", Value(progress, "NativeInputFrames")); Add(text, "native_fresh_frames", Value(progress, "NativeFreshFrames"));
        Add(text, "input_errors", Value(progress, "InputErrors")); Add(text, "runtime_errors", Value(progress, "RuntimeErrors"));
        var age = Value(progress, "ResultAgeMicroseconds");
        if (age != null && Convert.ToDouble(age, CultureInfo.InvariantCulture) < 0) age = null;
        Add(text, "result_age_us", age);
        Add(text, "actual_source", Value(progress, "ActualSource")); Add(text, "actual_image_service", Value(progress, "ActualImageService"));
        Add(text, "actual_backend", Value(progress, "ActualBackend")); Add(text, "execution_mode", Value(progress, "ExecutionMode"));
        Add(text, "capability_level", Value(progress, "CapabilityLevel")?.ToString());
        Add(text, "unity_callbacks", callbacks); Add(text, "unity_deliveries", deliveries); Add(text, "unity_renders", renders);
        Add(text, "streaming", sdk.IsRunning); Add(text, "preview_enabled", preview); Add(text, "target_fps", Application.targetFrameRate);
        Add(text, "stall_requested_ms", stallMs); Add(text, "consumer_disabled", consumerDisabled);
        Add(text, "gpu_load_passes", gpuMaterial == null ? 0 : gpuPasses); Add(text, "cpu_load_workers", cpuWorkers);
        string line = "HV_INDEPENDENT_PROGRESS " + text.Append('}');
        progressLog?.WriteLine(line); progressLog?.Flush();
        Debug.Log(line);
    }
    private static void Add(StringBuilder text, string key, object value)
    {
        text.Append(",\"").Append(key).Append("\":");
        if (value == null) text.Append("null");
        else if (value is bool flag) text.Append(flag ? "true" : "false");
        else if (value is string str) text.Append('"').Append(str.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n").Replace("\r", "\\r")).Append('"');
        else text.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
    }
    private static string TextExtra(string key, string fallback)
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
        using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
        using (var intent = activity.Call<AndroidJavaObject>("getIntent")) return intent.Call<string>("getStringExtra", key) ?? fallback;
#else
        var config = Path.GetFullPath(Path.Combine(Application.dataPath, "../Library/HumanVisionIndependentProbe.txt"));
        if (File.Exists(config)) foreach (var line in File.ReadAllLines(config))
            if (line.StartsWith(key + "=", StringComparison.Ordinal)) return line.Substring(key.Length + 1).Trim();
        return fallback;
#endif
    }
    private static int IntExtra(string key, int fallback)
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        return PlatformInt(key, fallback);
#else
        int value; return int.TryParse(TextExtra(key, ""), out value) ? value : fallback;
#endif
    }
    private static int PlatformInt(string key, int fallback)
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
        using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
        using (var intent = activity.Call<AndroidJavaObject>("getIntent")) return intent.Call<int>("getIntExtra", key, fallback);
#else
        return fallback;
#endif
    }
    private static bool BoolExtra(string key, bool fallback)
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
        using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
        using (var intent = activity.Call<AndroidJavaObject>("getIntent")) return intent.Call<bool>("getBooleanExtra", key, fallback);
#else
        bool value; return bool.TryParse(TextExtra(key, ""), out value) ? value : fallback;
#endif
    }
    private void OnDestroy()
    {
        running = false;
        progressLog?.Dispose(); progressLog = null;
        if (sdk != null) sdk.ResultUpdated -= OnResult;
        if (view != null) view.FramePresented -= OnPresented;
        if (gpuA != null) Destroy(gpuA); if (gpuB != null) Destroy(gpuB); if (gpuMaterial != null) Destroy(gpuMaterial);
    }
}
