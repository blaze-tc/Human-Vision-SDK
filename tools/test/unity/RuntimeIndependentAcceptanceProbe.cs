using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
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
    private static string npuStartupCase = "", npuRtspUrl = "";
    private static bool npuTcp = true, npuMirror, startupDiagnostics, hashLoadedModules;
    private static int npuRunSeconds;
    private static RuntimeIndependentAcceptanceProbe instance;
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
    private StreamWriter startupLog;
    private string diagnosticsDirectory, sessionStamp, observedState;
    private int startupEvent;
    private bool firstInput, firstTexture, firstResult;
    private readonly Dictionary<string, string> moduleHashes = new Dictionary<string, string>();

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Install()
    {
        if (SceneManager.GetActiveScene().name != "RuntimePrefabAcceptance") return;
        execution = TextExtra("hv_independent_execution", "compatible");
        preview = !BoolExtra("hv_independent_preview_off", false);
        npuStartupCase = TextExtra("hv_npu_startup_case", "").Trim().ToUpperInvariant();
        if (npuStartupCase == "A" || npuStartupCase == "B" || npuStartupCase == "C") {
            execution = npuStartupCase == "C" ? "compatible" : "native";
            preview = npuStartupCase != "B";
        }
        npuRtspUrl = string.IsNullOrEmpty(npuStartupCase) ? "" : TextExtra("hv_acceptance_fixture", "");
        npuTcp = BoolExtra("hv_npu_startup_tcp", true);
        npuMirror = BoolExtra("hv_npu_startup_mirror", false);
        startupDiagnostics = !string.IsNullOrEmpty(npuStartupCase) || BoolExtra("hv_independent_startup_diagnostics", false);
        hashLoadedModules = BoolExtra("hv_independent_hash_loaded_modules", false);
        npuRunSeconds = Math.Max(1, Math.Min(3600, IntExtra("hv_acceptance_stage_seconds", 60)));
        stallMs = IntExtra("hv_independent_stall_ms", 0);
        if (stallMs != 500 && stallMs != 2000) stallMs = 0;
        stallAfter = Math.Max(1, IntExtra("hv_independent_stall_after_s", 8));
        gpuPasses = Math.Max(0, Math.Min(16, IntExtra("hv_independent_gpu_passes", 0)));
        cpuWorkers = Math.Max(0, Math.Min(4, IntExtra("hv_independent_cpu_workers", 0)));
        disableConsumer = BoolExtra("hv_independent_disable_consumer", false);
        requestedTargetFps = IntExtra("hv_independent_target_fps", 0);
        if (!string.IsNullOrEmpty(npuStartupCase)) {
            // The explicit startup diagnostic bypasses the UI's unrelated CPU/
            // GPU provider preflight, not the production SDK initialization.
            foreach (var component in FindObjectsOfType<MonoBehaviour>(true))
                if (component.GetType().FullName == "HumanVisionAcceptance.RuntimePrefabAcceptance") component.enabled = false;
            foreach (var existing in FindObjectsOfType<HumanVisionSdk>(true)) existing.InitializeOnStart = false;
        }
        new GameObject("Independent Runtime Acceptance Probe").AddComponent<RuntimeIndependentAcceptanceProbe>();
    }
    private void Awake()
    {
        instance = this;
        diagnosticsDirectory = Path.Combine(Application.persistentDataPath, "RuntimePrefabDiagnostics");
        sessionStamp = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff");
        var path = Path.Combine(diagnosticsDirectory, "independent-progress-" + sessionStamp + ".log");
        try { Directory.CreateDirectory(diagnosticsDirectory); progressLog = new StreamWriter(path, false, new UTF8Encoding(false)); }
        catch (Exception error) { Debug.LogWarning("HV_NPU_STARTUP_FILE_UNAVAILABLE type=" + error.GetType().Name); }
        Debug.Log("HV_INDEPENDENT_DIAGNOSTICS session=" + path);
        if (startupDiagnostics) {
            path = Path.Combine(diagnosticsDirectory, "npu-startup-" + sessionStamp + ".jsonl");
            try { startupLog = new StreamWriter(path, false, new UTF8Encoding(false)); }
            catch (Exception error) { Debug.LogWarning("HV_NPU_STARTUP_FILE_UNAVAILABLE type=" + error.GetType().Name); }
            Debug.Log("HV_NPU_STARTUP_DIAGNOSTICS session=" + path);
            EmitStartup("probe_awake");
        }
    }
    internal static void ApplyNpuStartupOptions(HumanVisionSdkOptions options, string selectedCase, string url, bool tcp, bool mirror)
    {
        if (options == null || options.Input == null) throw new ArgumentException("Startup options/input are missing.");
        if (selectedCase != "A" && selectedCase != "B" && selectedCase != "C") throw new ArgumentException("NPU startup case must be A, B or C.");
        if (!Uri.TryCreate(url, UriKind.Absolute, out var address) || address.Scheme != "rtsp" || string.IsNullOrEmpty(address.Host))
            throw new ArgumentException("NPU startup requires an explicit rtsp:// source; no camera or URL fallback is allowed.");
        options.SourceKind = InputKind.Rtsp;
        options.Input.RtspUrl = url;
        options.Input.Mirror = mirror;
        options.RtspTcp = tcp;
        options.AccelerationMode = HumanVisionAccelerationMode.Neural;
        options.InputQuality = ModelInputQuality.Low;
        options.UseWindowsCpu = false;
        options.ExecutionPolicy = selectedCase == "C" ? HumanVisionExecutionPolicy.CompatibleUnityInput : HumanVisionExecutionPolicy.IndependentRequired;
        options.PreviewEnabled = selectedCase != "B";
        options.OpenInputOnInitialize = true;
        options.UseModelSizedAnalysisInput = true;
        options.AutoSelectRtspStream = false;
    }
    public static void ConfigureOptions(HumanVisionSdkOptions options)
    {
        if (!string.IsNullOrEmpty(npuStartupCase)) {
            ApplyNpuStartupOptions(options, npuStartupCase, npuRtspUrl, npuTcp, npuMirror);
            instance?.EmitStartup("options_configured", options);
            return;
        }
        var policy = options.GetType().GetField("ExecutionPolicy");
        if (policy != null) policy.SetValue(options, Enum.Parse(policy.FieldType,
            execution == "native" ? "IndependentRequired" : "CompatibleUnityInput"));
        else if (execution == "native") Debug.LogError("Independent execution requested but this SDK has no execution policy.");
        options.GetType().GetField("PreviewEnabled")?.SetValue(options, preview);
        if (requestedTargetFps == 15 || requestedTargetFps == 30 || requestedTargetFps == 60)
            Application.targetFrameRate = requestedTargetFps;
        instance?.EmitStartup("options_configured", options);
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
        if (!string.IsNullOrEmpty(npuStartupCase)) StartCoroutine(RunNpuStartup());
    }
    private IEnumerator RunNpuStartup()
    {
        EmitStartup("runtime_view_create_begin");
        var existing = FindObjectsOfType<HumanVisionSdk>(true);
        var existingView = FindObjectOfType<HumanVisionRuntimeView>();
        if (existing.Length > 1 || existing.Length == 1 && (existingView == null || existingView.Sdk != existing[0] ||
            existing[0].Busy || existing[0].IsInitialized || existing[0].IsRunning)) {
            EmitStartup("startup_rejected_existing_owner"); yield break;
        }
        HumanVisionSdkOptions options;
        try {
            view = HumanVisionRuntimeView.Create(); sdk = view.Sdk;
            sdk.InitializeOnStart = false;
            view.PresentationMode = HumanVisionRuntimePresentationMode.Disabled;
            view.UseAnalysisFrameForMatchedPreview = false;
            options = sdk.Configuration;
            ConfigureOptions(options);
            options.Validate();
        } catch (Exception error) {
            EmitStartup("startup_rejected", null, error.Message); yield break;
        }
        sdk.ResultUpdated += OnResult;
        progressMethod = sdk.GetType().GetMethod("TryGetRuntimeProgress");
        view.FramePresented += OnPresented;
        if (requestedTargetFps == 15 || requestedTargetFps == 30 || requestedTargetFps == 60) Application.targetFrameRate = requestedTargetFps;
        EmitStartup("initialize_begin", options);
        yield return sdk.Initialize(options);
        EmitStartup("initialize_returned");
        if (sdk.IsRunning) {
            double until = Time.realtimeSinceStartupAsDouble + npuRunSeconds;
            while (sdk != null && sdk.IsRunning && Time.realtimeSinceStartupAsDouble < until) yield return null;
            if (sdk != null) {
                EmitStartup("shutdown_begin");
                yield return sdk.Shutdown();
                EmitStartup("shutdown_returned");
            }
        }
    }
    private void OnResult(long sequence) {
        callbacks++; if (sequence != deliveredSequence) { deliveredSequence = sequence; deliveries++; }
        if (!firstResult) { firstResult = true; EmitStartup("first_result_callback"); }
    }
    private void OnPresented(HumanVisionPresentedFrame frame) { if (frame.ResultSequence != renderedSequence) { renderedSequence = frame.ResultSequence; renders++; } }
    private void Update()
    {
        if (sdk == null) {
            sdk = FindObjectOfType<HumanVisionSdk>();
            if (sdk != null) { sdk.ResultUpdated += OnResult; progressMethod = sdk.GetType().GetMethod("TryGetRuntimeProgress"); }
        }
        if (view == null) { view = FindObjectOfType<HumanVisionRuntimeView>(); if (view != null) view.FramePresented += OnPresented; }
        if (sdk == null) return;
        if (startupDiagnostics) {
            string state = sdk.State + "/" + sdk.InputState + "/" + sdk.Busy;
            if (state != observedState) { observedState = state; EmitStartup("sdk_state_observed"); }
        }
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
        Add(text, "npu_startup_case", npuStartupCase);
        string line = "HV_INDEPENDENT_PROGRESS " + text.Append('}');
        progressLog?.WriteLine(line); progressLog?.Flush();
        Debug.Log(line);
        if (startupDiagnostics && !firstInput && sourceAvailable && sourceProgress.AcquiredFrames > 0) { firstInput = true; EmitStartup("first_source_acquired_observed"); }
        if (startupDiagnostics && !firstTexture && sdk.TryGetInputFrame(-1, out var texture) && texture.Texture != null) { firstTexture = true; EmitStartup("first_live_texture_observed"); }
    }
    private void EmitStartup(string stage, HumanVisionSdkOptions requested = null, string failure = null)
    {
        if (!startupDiagnostics) return;
        try { EmitStartupCore(stage, requested, failure); }
        catch (Exception error) { Debug.LogWarning("HV_NPU_STARTUP_DIAGNOSTIC_FAILURE stage=" + stage + " type=" + error.GetType().Name); }
    }
    private void EmitStartupCore(string stage, HumanVisionSdkOptions requested, string failure)
    {
        if (!startupDiagnostics) return;
        var text = new StringBuilder("{\"kind\":\"startup\"");
        Add(text, "stage", stage); Add(text, "case", npuStartupCase); Add(text, "event", ++startupEvent);
        Add(text, "requested_stage_seconds", npuRunSeconds);
        Add(text, "elapsed_us", timer.ElapsedTicks * 1000000L / Stopwatch.Frequency);
        Add(text, "utc", DateTime.UtcNow.ToString("O"));
        Add(text, "sdk_state", sdk != null ? sdk.State.ToString() : null);
        Add(text, "input_state", sdk != null ? sdk.InputState.ToString() : null);
        Add(text, "busy", sdk != null ? (object)sdk.Busy : null);
        var options = requested ?? sdk?.ActiveConfiguration;
        Add(text, "configuration_available", options != null);
        Add(text, "configuration_kind", requested != null ? "requested" : "active");
        Add(text, "policy", options?.ExecutionPolicy.ToString()); Add(text, "preview_enabled", options != null ? (object)options.PreviewEnabled : null);
        Add(text, "open_input", options != null ? (object)options.OpenInputOnInitialize : null);
        Add(text, "model_sized_analysis", options != null ? (object)options.UseModelSizedAnalysisInput : null);
        Add(text, "requested_backend", options?.AccelerationMode.ToString()); Add(text, "requested_quality", options?.InputQuality.ToString());
        Add(text, "tcp", options != null ? (object)options.RtspTcp : null); Add(text, "mirror", options != null ? (object)options.Input.Mirror : null);
        Add(text, "url_sha256", options != null && options.SourceKind == InputKind.Rtsp ? DigestText(options.Input.RtspUrl) : null);
        Add(text, "profile", sdk != null ? sdk.RuntimeProfile : null);
        Add(text, "runtime_diagnostics", sdk != null ? DiagnosticText(sdk.RuntimeDiagnostics) : null);
        HumanVisionTextureFrame liveFrame = default;
        bool hasTexture = sdk != null && sdk.TryGetInputFrame(-1, out liveFrame);
        Add(text, "live_frame", hasTexture ? (object)liveFrame.FrameId : null);
        Add(text, "live_width", hasTexture ? (object)liveFrame.Width : null); Add(text, "live_height", hasTexture ? (object)liveFrame.Height : null);
        Add(text, "error", DiagnosticText(failure ?? sdk?.LastError ?? ""));
        Add(text, "preview_error", DiagnosticText(sdk?.PreviewError ?? ""));
        Add(text, "graphics_api", SystemInfo.graphicsDeviceType.ToString()); Add(text, "graphics_device", SystemInfo.graphicsDeviceName);
        Add(text, "graphics_fence", SystemInfo.supportsGraphicsFence); Add(text, "async_compute", SystemInfo.supportsAsyncCompute);
        Add(text, "unity_version", Application.unityVersion);
        string line = text.Append('}').ToString();
        startupLog?.WriteLine(line); startupLog?.Flush();
        Debug.Log("HV_NPU_STARTUP " + line);
        CaptureLoadedModules(stage);
    }
    private static string DigestText(string value)
    {
        using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(value ?? ""))).Replace("-", "").ToLowerInvariant();
    }
    private static string DiagnosticText(string value) => Regex.Replace(HumanVisionSettingsController.Redact(value),
        @"rtsps?://[^\s""'<>]+", "[rtsp-url-redacted]", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private void CaptureLoadedModules(string stage)
    {
#if UNITY_ANDROID && !UNITY_EDITOR
        try {
            string maps = File.ReadAllText("/proc/self/maps");
            string path = Path.Combine(diagnosticsDirectory, "loaded-maps-" + sessionStamp + "-" + startupEvent.ToString("D3") + ".txt");
            File.WriteAllText(path, maps, new UTF8Encoding(false));
            foreach (string row in maps.Split('\n')) {
                int slash = row.IndexOf('/'); if (slash < 0) continue;
                string module = row.Substring(slash).Trim();
                if (!module.EndsWith(".so", StringComparison.Ordinal) ||
                    !(module.Contains("humanvision") || module.Contains("rknn") || module.Contains("onnxruntime") || module.Contains("ncnn") || module.Contains("vulkan") || module.Contains("mali"))) continue;
                if (moduleHashes.ContainsKey(module)) continue;
                string hash = null, error = null;
                if (hashLoadedModules) {
                    try { using (var sha = SHA256.Create()) using (var stream = File.OpenRead(module)) hash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant(); }
                    catch (Exception e) { error = e.GetType().Name; }
                }
                moduleHashes[module] = hash;
                var item = new StringBuilder("{\"kind\":\"loaded_module\"");
                Add(item, "elapsed_us", timer.ElapsedTicks * 1000000L / Stopwatch.Frequency); Add(item, "stage", stage);
                Add(item, "mapped_path", module); Add(item, "sha256", hash); Add(item, "hash_error", error);
                Add(item, "hash_requested", hashLoadedModules); Add(item, "build_id", null);
                startupLog?.WriteLine(item.Append('}').ToString()); startupLog?.Flush();
            }
        } catch (Exception e) {
            startupLog?.WriteLine("{\"kind\":\"loaded_maps_unavailable\",\"error\":\"" + e.GetType().Name + "\"}"); startupLog?.Flush();
        }
#endif
    }
    private static void Add(StringBuilder text, string key, object value)
    {
        text.Append(",\"").Append(key).Append("\":");
        if (value == null) text.Append("null");
        else if (value is bool flag) text.Append(flag ? "true" : "false");
        else if (value is string str) {
            text.Append('"');
            foreach (char character in str) {
                if (character == '\\' || character == '"') text.Append('\\').Append(character);
                else if (character < 32) text.Append("\\u").Append(((int)character).ToString("x4"));
                else text.Append(character);
            }
            text.Append('"');
        }
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
        EmitStartup("probe_destroyed");
        startupLog?.Dispose(); startupLog = null;
        progressLog?.Dispose(); progressLog = null;
        if (instance == this) instance = null;
        if (sdk != null) sdk.ResultUpdated -= OnResult;
        if (view != null) view.FramePresented -= OnPresented;
        if (gpuA != null) Destroy(gpuA); if (gpuB != null) Destroy(gpuB); if (gpuMaterial != null) Destroy(gpuMaterial);
    }
}
