using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Profiling;
using UnityEngine.UI;
using UnityEngine.Video;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// HumanVisionSettingsDemo 专用的实机诊断总控。只观测 SDK，不改变输入/识别/停止策略。
    /// 提前于 SDK 的 Start 建立日志；每秒记录链路状态，每秒最多采样一次新的原始骨骼结果。
    /// </summary>
    [DefaultExecutionOrder(-500), DisallowMultipleComponent]
    public sealed class HumanVisionSettingsDeviceDiagnostics : MonoBehaviour
    {
        [Header("设置场景引用"), SerializeField] private HumanVisionSettingsController controller;
        [SerializeField] private Text logPathLabel;
        [SerializeField] private Text hardwareLabel;
        [SerializeField] private Button copyPathButton, exportButton, copyZipButton, skeletonButton;
        [Header("实机记录"), SerializeField, Tooltip("默认采样详细骨骼，不需要在高级设置中另行开启。")] private bool recordSkeletons = true;
        [SerializeField, Range(.2f, 60)] private float statisticsSeconds = 1;
        [SerializeField, Range(.2f, 60)] private float skeletonSeconds = 1;
        [SerializeField, Range(1, 64)] private int logFileMegabytes = 8;
        [SerializeField] private string inputPackageVersion = "";
        private DeviceDiagnosticSession session;
        private AndroidNativeStageCapture nativeStages;
        private readonly NativeStageTracker stageTracker = new NativeStageTracker();
        private DeviceHardwareSampler hardware;
        private AndroidLogFolderMirror folderMirror;
        private long lastHardwareSample, lastStageSample, bodyResultEvents, previousBodyResultEvents;
        private double displayedBodyFps;
        private readonly FrameTiming[] frameTimings = new FrameTiming[1];
        private double unityCpuFrameMs = -1, unityGpuFrameMs = -1;
        private HumanVisionSdk sdk;
        private HumanVisionManager manager;
        private VideoPlayerFrameSource bridge;
        private HumanVisionInputAdapter adapter;
        private IHumanVisionFrameSource source;
        private VideoPlayer decoder;
        private readonly HashSet<string> priorSdkSessions = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        private readonly HumanVisionCanonicalJoint[] joints = new HumanVisionCanonicalJoint[32];
        private string sdkLogRoot, previousConfiguration, previousState, previousErrors, previousHealth, uiMessage = "记录已启动";
        private double nextStats, nextSkeleton, nextFlush, nextNativeDiagnostics, previousSampleTime;
        private long resultEvents, previousEventCount, lastSkeletonSequence = -1, previousPublished = -1, previousSubmitted, previousProcessed;
        private ulong previousGeneration;
        private double lastPublishedAdvance, lastSubmittedAdvance, lastProcessedAdvance, lastSdkResultAdvance;
        private int previousUnityFrame;
        private double previousUnitySampleTime, maximumUnityFrameMs;
        private double displayedUnityFps, displayedPublicationFps, displayedSkeletonFps;
        private string displayedHealth = "输入未启动";
        private bool quitting, captureClosed;
        private string previousUiText;
        private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;
        public string CurrentLogDirectory => !string.IsNullOrEmpty(folderMirror?.PublicDirectory) ? folderMirror.PublicDirectory : session?.SessionPath ?? "";
        public string LastExportPath { get; private set; } = "";
        public string LastWriteError => session?.LastWriteError ?? "";
        public bool RecordSkeletons => recordSkeletons;

        // 以下 DTO 是低频日志格式，使用可序列化字段。无结果/无坐标用显式有效位和 -1，禁止写 NaN/Infinity。
        [Serializable] private sealed class EnvironmentRecord
        {
            public int format = 2;
            public string scene, sdk, inputPackage, unity, app, identifier, buildGuid, platform, device, os, cpu, gpu, graphicsApi, graphicsVersion;
            public string persistentData, streamingAssets, unityConsolePath, logPath, utc, localTime, colorSpace;
            public string deviceBuildInfo;
            public int cpuCores, memoryMB, gpuMemoryMB, screenWidth, screenHeight, targetFrameRate, vSync;
            public bool editor, developmentBuild, supportsAsyncReadback, supportsCompute;
        }
        [Serializable] private sealed class NativeCaptureMarker { public bool native_stage_trace; }
        [Serializable] private sealed class PipelineRecord
        {
            public string utc, sdkState, sourceMode, requestedMode, activeMode, sourceType, sourceState, sourceId, generation, sourceError, adapterError, bridgeError, managerError, sdkError;
            public string runtimeProfile, modelPack, rowOrigin, colorSpace, sourceClockDomain, sourceClockId, timestampKind, health;
            public bool running, initialized, fresh, usesGpuFrames, retirementPending, hasPublishedFrame, canPresentNativeResult;
            public bool mirrorApplied, hasVideoDecoder, videoPrepared, videoPlaying, videoPaused, regionsEnabled;
            public int maxBodies, nativeBodies, sdkBodies, width, height, analysisWidth, analysisHeight, pendingCopies, appliedRotation;
            public long publishedFrame = -1, acceptedFrame = -1, resultFrame = -1, nativeSequence, sourceTimestampUs, publishedTimestampUs, ptsUs;
            public long submitted, processed, dropped, copiedFrames, droppedBeforeSubmit, readbackErrors, readbackDrops, sdkResultEvents, unityQueueDrops, nativeQueueDrops, managedBytes, unityAllocatedBytes;
            public long videoFrame = -1;
            public double elapsed_s, frameAgeMs = -1, publishedAgeMs = -1, publicationFpsEstimate, freshSdkEventsFps, nativeInputFps, inferenceFps, detectMs, poseMs, trackingMs, totalMs;
            public double videoTime, videoLength, videoFps;
            public int unityFrameCount, targetFrameRate;
            public double unityFramesFps, maximumUnityFrameMs;
            public string videoUrl, regions;
            public long freshBodyResultEvents;
            public double freshBodyResultsFps;
            public HardwareSample hardware;
            public NativeStageSample nativeStages;
            public string publicLogDirectory, folderSyncStatus;
        }
        /// <summary>流水线汇总与稀疏原生样本分开保存。它们可能属于不同帧，禁止相减制造精确耗时。</summary>
        [Serializable] private sealed class TimingRecord
        {
            public string utc, runtimeProfile, modelPack, sourceId, generation;
            public long resultFrame, acceptedFrame;
            public double publicationFps, freshBodyResultsFps, pipelineTotalMs, sdkDetectMs, sdkPoseMs, sdkTrackingMs;
            public double resultAgeMs, publishedAgeMs, unityCpuFrameMs, unityGpuFrameMs;
            public int pendingCopies;
            public NativeStageSample native;
            public string scope = "SDK current-result aggregate and latest complete sparse native frame are separate; frame IDs may differ";
            public string frameClockScope = "Unity CPU/GPU render frame duration, not utilization or SDK model GPU time; -1 means unavailable";
            public string inputScope = "publication FPS/copy queue/local result age; network/decode duration and sensor capture latency not individually instrumented";
        }
        [Serializable] private sealed class JointRecord
        {
            public string joint;
            public bool positionValid, apiValid, derived, screenValid, worldPlaneValid;
            public float confidence, predictionMs;
            public Vector2 pixel, normalized, screen;
            public Vector3 worldPlane;
            public long observedUs;
        }
        [Serializable] private sealed class SkeletonRecord
        {
            public string utc, stableTrackId;
            public long resultSequence, sourceFrame, observedUs, resultEvents;
            public int slot, apiValidJoints;
            public float bodyConfidence;
            public Rect bounds;
            public bool sampledForDiagnostics = true, worldIsPlaneMapping = true;
            public JointRecord[] joints;
        }
        /// <summary>Editor 安装器设置可见 UGUI 引用；生成后可直接在 Inspector 调整采样间隔。</summary>
        public void Configure(HumanVisionSettingsController target, Text path, Button copy, Button export, Button zip, Button skeleton, string inputVersion, Text hardwareText)
        {
            controller = target; logPathLabel = path; copyPathButton = copy; exportButton = export; copyZipButton = zip; skeletonButton = skeleton;
            inputPackageVersion = inputVersion;
            hardwareLabel = hardwareText;
        }
        private void Awake()
        {
            sdk = controller != null ? controller.Sdk : null;
            sdkLogRoot = Path.Combine(Application.persistentDataPath, "HumanVisionSdkSettings", "logs");
            BeginSession();
        }
        private void BeginSession()
        {
            priorSdkSessions.Clear();
            if (Directory.Exists(sdkLogRoot)) foreach (string path in Directory.GetDirectories(sdkLogRoot)) priorSdkSessions.Add(path);
            session = new DeviceDiagnosticSession(Path.Combine(Application.persistentDataPath, "HumanVisionSettingsDemo", "DeviceLogs"), logFileMegabytes * 1024 * 1024, 2048);
            session.SetCsvHeader(new[] { "utc", "elapsed_s", "source_mode", "sdk_state", "source_state", "published_frame", "accepted_frame", "result_frame", "native_sequence", "sdk_result_events", "submitted", "processed", "dropped", "native_bodies", "sdk_bodies", "fresh", "published_fps_estimate", "fresh_sdk_events_fps", "native_input_fps", "inference_fps", "detect_ms", "pose_ms", "total_ms", "result_age_ms", "width", "height", "source_id", "generation", "gpu", "copied_frames", "pending_copies", "source_error", "adapter_error", "bridge_error", "manager_error", "sdk_error", "health", "unity_frame_count", "unity_frames_fps", "maximum_unity_frame_ms", "target_frame_rate", "fresh_body_result_events", "fresh_body_results_fps" });
            stageTracker.Reset(); lastHardwareSample = lastStageSample = 0;
            hardware = new DeviceHardwareSampler(SystemInfo.processorCount); hardware.Start();
            folderMirror = new AndroidLogFolderMirror(session.SessionPath, sdkLogRoot, priorSdkSessions);
            session.Record("timing.contract", "Stage-only build: first 3 then every 64 native frames; no per-layer GPU query overhead. SDK totals/current result and sparse native frame IDs remain distinct. Missing metrics=-1+status.");
            WriteEnvironment();
            session.Flush(2048); folderMirror.Start();
#if UNITY_ANDROID && !UNITY_EDITOR
            var identity = Resources.Load<TextAsset>("HumanVisionDeviceBuildInfo");
            if (identity != null && JsonUtility.FromJson<NativeCaptureMarker>(identity.text).native_stage_trace) {
                session.Record("native.capture.requested", "Owned process only; sparse native stages; native layer query mode is separately identified by the build marker.");
                nativeStages = new AndroidNativeStageCapture((line, thread) => { stageTracker.Accept(line); session.EnqueueNative(line, thread); }); nativeStages.Start();
            }
#endif
            previousState = previousErrors = previousHealth = previousConfiguration = previousUiText = null;
            nextStats = nextSkeleton = nextFlush = nextNativeDiagnostics = 0; LastExportPath = ""; uiMessage = "记录已启动";
            previousSampleTime = lastPublishedAdvance = lastSubmittedAdvance = lastProcessedAdvance = lastSdkResultAdvance = Time.realtimeSinceStartupAsDouble;
            previousUnityFrame = Time.frameCount; previousUnitySampleTime = previousSampleTime; maximumUnityFrameMs = 0;
        }
        private void OnEnable()
        {
            if (captureClosed) BeginSession(); captureClosed = false; quitting = false;
            if (copyPathButton != null) copyPathButton.onClick.AddListener(CopyLogPath);
            if (exportButton != null) exportButton.onClick.AddListener(ExportLogs);
            if (copyZipButton != null) copyZipButton.onClick.AddListener(CopyExportPath);
            if (skeletonButton != null) skeletonButton.onClick.AddListener(ToggleSkeletons);
            Application.logMessageReceivedThreaded += OnUnityLog;
            // 确认普通 Unity 日志采集已启用，同时保留当前会话入口；不记录连接密码。
            Debug.Log("[HumanVisionSettingsDemo diagnostics] session=" + CurrentLogDirectory + "; targetFrameRate=" + Application.targetFrameRate);
            Application.lowMemory += OnLowMemory;
            if (sdk != null) {
                sdk.Initialized += OnInitialized; sdk.Stopped += OnStopped; sdk.ErrorOccurred += OnSdkError; sdk.ResultUpdated += OnResult;
                sdk.UserEntered += OnUserEntered; sdk.UserExited += OnUserExited; sdk.RegionOccupancyChanged += OnRegionChanged;
                if (sdk.IsInitialized) OnInitialized();
            }
        }
        private void WriteEnvironment()
        {
            session.WriteEnvironment(JsonUtility.ToJson(new EnvironmentRecord {
                scene = gameObject.scene.path, sdk = HumanVisionSdk.Version, inputPackage = inputPackageVersion, unity = Application.unityVersion,
                app = Application.version, identifier = Application.identifier, buildGuid = Application.buildGUID, platform = Application.platform.ToString(),
                device = SystemInfo.deviceModel, os = SystemInfo.operatingSystem, cpu = SystemInfo.processorType, cpuCores = SystemInfo.processorCount,
                memoryMB = SystemInfo.systemMemorySize, gpu = SystemInfo.graphicsDeviceName, graphicsApi = SystemInfo.graphicsDeviceType.ToString(),
                graphicsVersion = SystemInfo.graphicsDeviceVersion, gpuMemoryMB = SystemInfo.graphicsMemorySize,
                persistentData = Application.persistentDataPath, streamingAssets = Application.streamingAssetsPath, unityConsolePath = Application.consoleLogPath,
                logPath = CurrentLogDirectory, utc = DateTime.UtcNow.ToString("O", Invariant), localTime = DateTimeOffset.Now.ToString("O", Invariant),
                colorSpace = QualitySettings.activeColorSpace.ToString(), screenWidth = Screen.width, screenHeight = Screen.height,
                deviceBuildInfo = Resources.Load<TextAsset>("HumanVisionDeviceBuildInfo")?.text ?? "",
                targetFrameRate = Application.targetFrameRate, vSync = QualitySettings.vSyncCount, editor = Application.isEditor,
                developmentBuild = Debug.isDebugBuild, supportsAsyncReadback = SystemInfo.supportsAsyncGPUReadback, supportsCompute = SystemInfo.supportsComputeShaders
            }, true));
        }
        /// <summary>后台回调绝不读 SDK/Texture/Time，也不写文件或再次 Debug.Log。</summary>
        private void OnUnityLog(string condition, string stack, LogType type)
            => session?.EnqueueUnity(condition, stack, type.ToString(), Thread.CurrentThread.ManagedThreadId);
        private void OnInitialized()
        {
            // Runtime Host 与场景总控分开存活；只在初始化时查找，随后缓存，避免每帧全场景搜索。
            manager = FindObjectsOfType<HumanVisionManager>().FirstOrDefault(m => m.gameObject.name == sdk.name + " Runtime Host");
            bridge = manager != null ? manager.GetComponent<VideoPlayerFrameSource>() : null;
            source = null; adapter = null; decoder = null; previousGeneration = 0; previousPublished = -1;
            resultEvents = previousEventCount = previousSubmitted = previousProcessed = bodyResultEvents = previousBodyResultEvents = 0; lastSkeletonSequence = -1;
            stageTracker.Reset(); lastStageSample = 0;
            previousSampleTime = lastPublishedAdvance = lastSubmittedAdvance = lastProcessedAdvance = lastSdkResultAdvance = Time.realtimeSinceStartupAsDouble;
            session.Record("runtime.initialized", "profile=" + sdk.RuntimeProfile + " modelPack=" + sdk.ActiveModelPack + " analysis=" + sdk.AnalysisInputSize + " root=" + sdk.RuntimeRootPath);
            CaptureNativeDiagnostics("initialized"); nextStats = 0;
        }
        private void FindInputComponents()
        {
            if (manager == null) return;
            if (adapter == null) adapter = manager.GetComponent<HumanVisionInputAdapter>();
            if (source == null || (source as UnityEngine.Object) == null) source = manager.GetComponents<MonoBehaviour>().OfType<IHumanVisionFrameSource>().FirstOrDefault();
            if (decoder == null && source is VideoFrameSource)
                decoder = Resources.FindObjectsOfTypeAll<VideoPlayer>().FirstOrDefault(p => p.gameObject.scene.IsValid() && p.gameObject.name == "HumanVision video decoder");
        }
        private void OnResult(long sequence)
        {
            ++resultEvents; lastSdkResultAdvance = Time.realtimeSinceStartupAsDouble;
            // 只统计含人体的新鲜通知；无人的完成结果也会发 ResultUpdated，不能算骨骼更新。
            if (sdk.HasFreshResult && sdk.GetUsersCount() > 0) ++bodyResultEvents;
            // 计数仅来自真正的 ResultUpdated 通知，详细坐标稍后按间隔采样，不重复把渲染帧当成新识别。
            if (resultEvents == 1) session.Record("result.first", "sequence=" + sequence + " sourceFrame=" + sdk.SourceFrameId + " users=" + sdk.GetUsersCount());
        }
        private void OnSdkError(string error) { session?.Record("sdk.error", error); CaptureNativeDiagnostics("sdk.error"); nextStats = 0; }
        private void OnStopped() { session?.Record("sdk.stopped", "Runtime and input retired"); manager = null; source = null; adapter = null; bridge = null; decoder = null; nextStats = 0; }
        private void OnUserEntered(int index, long id) => session?.Record("user.entered", "slot=" + index + " stableTrackId=" + id);
        private void OnUserExited(int index, long id) => session?.Record("user.exited", "slot=" + index + " stableTrackId=" + id);
        private void OnRegionChanged(int index, HumanVisionRegionOccupancy state) => session?.Record("region.changed", "slot=" + index + " state=" + state);
        private void OnLowMemory() { session?.Record("application.lowMemory", "managedBytes=" + GC.GetTotalMemory(false)); session?.Flush(2048); }
        private void OnApplicationPause(bool pause) { session?.Record("application.pause", pause.ToString()); session?.Flush(2048); }
        private void OnApplicationFocus(bool focus) { session?.Record("application.focus", focus.ToString()); session?.Flush(2048); }
        private void OnApplicationQuit() { quitting = true; CaptureStatistics(); session?.Record("application.quit", "normal quit requested"); session?.Flush(2048); }
        private void Update()
        {
            if (session == null || quitting) return;
            double now = Time.realtimeSinceStartupAsDouble;
            // 这里只累积数值，不逐帧写文件/创建 JSON；每个统计窗口输出一次。
            maximumUnityFrameMs = Math.Max(maximumUnityFrameMs, Time.unscaledDeltaTime * 1000d);
            FrameTimingManager.CaptureFrameTimings();
            try {
                if (now >= nextStats) { nextStats = now + statisticsSeconds; CaptureStatistics(); RefreshUi(); }
                if (recordSkeletons && now >= nextSkeleton) { nextSkeleton = now + skeletonSeconds; CaptureSkeletons(); }
                if (now >= nextFlush) { nextFlush = now + .5; session.Flush(256); RefreshUi(); }
            } catch (Exception e) { session.Record("collector.error", e.ToString()); uiMessage = "诊断采样失败：" + DeviceDiagnosticSession.Redact(e.Message); RefreshUi(); }
        }
        private void CaptureNativeDiagnostics(string reason)
        {
            if (session == null || sdk == null) return;
            try { session.Record("runtime.diagnostics", "reason=" + reason + " initialized=" + sdk.IsInitialized + " " + sdk.RuntimeDiagnostics); }
            catch (Exception e) { session.Record("runtime.diagnostics.failed", reason + ": " + e); }
            nextNativeDiagnostics = Time.realtimeSinceStartupAsDouble + (sdk.HasFreshResult ? 30 : 5);
        }
        private static string Number(double value) => (double.IsNaN(value) || double.IsInfinity(value) ? -1 : value).ToString("F3", Invariant);
        private void CaptureStatistics()
        {
            if (sdk == null || session == null) return;
            FindInputComponents(); double now = Time.realtimeSinceStartupAsDouble;
            var stats = sdk.Stats; var frame = default(HumanVisionTextureFrame);
            bool published = source != null && source.TryGetLatestFrame(-1, out frame);
            var image = sdk.GetColorImageTex(); var size = sdk.AnalysisInputSize;
            var value = new PipelineRecord {
                utc = DateTime.UtcNow.ToString("O", Invariant), elapsed_s = session.ElapsedSeconds,
                sdkState = sdk.State.ToString(), sourceMode = sdk.Configuration.SourceKind.ToString(), sourceState = sdk.InputState.ToString(),
                requestedMode = controller != null ? controller.Draft.SourceKind.ToString() : sdk.Configuration.SourceKind.ToString(),
                activeMode = sdk.ActiveConfiguration != null ? sdk.ActiveConfiguration.SourceKind.ToString() : "", sourceType = source?.GetType().Name ?? "",
                initialized = sdk.IsInitialized, running = sdk.IsRunning, fresh = sdk.HasFreshResult, hasPublishedFrame = published,
                sourceId = published ? frame.SourceId.ToString(Invariant) : "", generation = published ? frame.Generation.ToString(Invariant) : "",
                publishedFrame = published ? frame.FrameId : -1, acceptedFrame = adapter != null ? adapter.LatestSubmittedFrameId : -1,
                resultFrame = sdk.SourceFrameId, nativeSequence = sdk.ResultSequence, sdkResultEvents = resultEvents,
                submitted = stats.SubmittedFrames, processed = stats.ProcessedFrames, dropped = stats.DroppedFrames,
                nativeBodies = manager != null ? manager.BodyCount : 0, sdkBodies = sdk.GetUsersCount(), maxBodies = sdk.GetMaxBodies(),
                width = image != null ? image.width : 0, height = image != null ? image.height : 0, analysisWidth = size.x, analysisHeight = size.y,
                nativeInputFps = stats.InputFps, inferenceFps = stats.InferenceFps, detectMs = stats.DetectionMs, poseMs = stats.PoseMs, trackingMs = stats.TrackingMs, totalMs = stats.TotalMs,
                frameAgeMs = sdk.HasFreshResult ? sdk.ResultAgeMilliseconds : -1, usesGpuFrames = manager != null && manager.UsesAndroidGpuFrames,
                retirementPending = adapter != null && adapter.RetirementPending, pendingCopies = adapter != null ? adapter.PendingSourceCopies : 0,
                copiedFrames = adapter != null ? adapter.CopiedFrames : 0, droppedBeforeSubmit = adapter != null ? adapter.DroppedUnsubmittedFrames : 0,
                readbackErrors = bridge != null ? bridge.ReadbackErrors : 0, readbackDrops = bridge != null ? bridge.ReadbackDrops : 0,
                sourceError = source?.LastError ?? "", adapterError = adapter != null ? adapter.LastError : "", bridgeError = bridge != null ? bridge.LastError : "",
                managerError = manager != null ? manager.LastError : "", sdkError = sdk.LastError,
                runtimeProfile = sdk.RuntimeProfile, modelPack = sdk.ActiveModelPack, regionsEnabled = sdk.RegionsEnabled,
                canPresentNativeResult = bridge != null && bridge.CanPresentResult(sdk.SourceFrameId),
                managedBytes = GC.GetTotalMemory(false), unityAllocatedBytes = Profiler.GetTotalAllocatedMemoryLong(), unityQueueDrops = session.DroppedUnityMessages, nativeQueueDrops = session.DroppedNativeMessages
            };
            if (published) {
                value.sourceTimestampUs = frame.SourceTimestampUs; value.publishedTimestampUs = frame.PublishedTimestampUs; value.ptsUs = frame.PresentationTimestampUs;
                value.publishedAgeMs = Math.Max(0, InputMonotonicClock.NowUs - frame.PublishedTimestampUs) / 1000d;
                value.sourceClockDomain = frame.SourceClockDomain.ToString(); value.sourceClockId = frame.SourceClockId.ToString(Invariant);
                value.timestampKind = frame.TimestampKind.ToString(); value.rowOrigin = frame.RowOrigin.ToString(); value.colorSpace = frame.ColorSpace.ToString();
                value.mirrorApplied = frame.AppliedMirrorX; value.appliedRotation = frame.AppliedRotationDegrees;
                if (previousGeneration == frame.Generation && previousPublished >= 0 && frame.FrameId >= previousPublished && now > previousSampleTime)
                    value.publicationFpsEstimate = (frame.FrameId - previousPublished) / (now - previousSampleTime);
                if (previousGeneration != frame.Generation || previousPublished != frame.FrameId) lastPublishedAdvance = now;
                previousGeneration = frame.Generation; previousPublished = frame.FrameId;
            }
            if (stats.SubmittedFrames != previousSubmitted) { previousSubmitted = stats.SubmittedFrames; lastSubmittedAdvance = now; }
            if (stats.ProcessedFrames != previousProcessed) { previousProcessed = stats.ProcessedFrames; lastProcessedAdvance = now; }
            value.unityFrameCount = Time.frameCount; value.targetFrameRate = Application.targetFrameRate;
            value.unityFramesFps = now > previousUnitySampleTime ? Math.Max(0, value.unityFrameCount - previousUnityFrame) / (now - previousUnitySampleTime) : 0;
            value.maximumUnityFrameMs = maximumUnityFrameMs;
            previousUnityFrame = value.unityFrameCount; previousUnitySampleTime = now; maximumUnityFrameMs = 0;
            value.freshSdkEventsFps = now > previousSampleTime ? (resultEvents - previousEventCount) / (now - previousSampleTime) : 0;
            value.freshBodyResultEvents = bodyResultEvents;
            value.freshBodyResultsFps = now > previousSampleTime ? (bodyResultEvents - previousBodyResultEvents) / (now - previousSampleTime) : 0;
            previousBodyResultEvents = bodyResultEvents; displayedBodyFps = value.freshBodyResultsFps;
            value.hardware = hardware?.Read(); value.nativeStages = stageTracker.Read();
            value.publicLogDirectory = folderMirror?.PublicDirectory ?? ""; value.folderSyncStatus = folderMirror?.Status ?? "";
            if (value.hardware != null && !string.IsNullOrEmpty(value.hardware.utc)) {
                long stamp = DateTime.Parse(value.hardware.utc, Invariant, DateTimeStyles.RoundtripKind).Ticks;
                if (stamp != lastHardwareSample) { lastHardwareSample = stamp; session.WriteJsonLine("hardware.jsonl", JsonUtility.ToJson(value.hardware)); }
            }
            uint frameCount = FrameTimingManager.GetLatestTimings(1, frameTimings);
            unityCpuFrameMs = frameCount > 0 && frameTimings[0].cpuFrameTime > 0 ? frameTimings[0].cpuFrameTime : -1;
            unityGpuFrameMs = frameCount > 0 && frameTimings[0].gpuFrameTime > 0 ? frameTimings[0].gpuFrameTime : -1;
            session.WriteJsonLine("timings.jsonl", JsonUtility.ToJson(new TimingRecord {
                utc = value.utc, sourceId = value.sourceId, generation = value.generation, runtimeProfile = value.runtimeProfile, modelPack = value.modelPack,
                resultFrame = value.resultFrame, acceptedFrame = value.acceptedFrame, publicationFps = value.publicationFpsEstimate, freshBodyResultsFps = value.freshBodyResultsFps,
                pipelineTotalMs = value.totalMs, sdkDetectMs = value.detectMs, sdkPoseMs = value.poseMs, sdkTrackingMs = value.trackingMs,
                resultAgeMs = value.frameAgeMs, publishedAgeMs = value.publishedAgeMs, pendingCopies = value.pendingCopies,
                native = value.nativeStages, unityCpuFrameMs = unityCpuFrameMs, unityGpuFrameMs = unityGpuFrameMs }));
            if (value.nativeStages.available && value.nativeStages.completedSamples != lastStageSample) {
                lastStageSample = value.nativeStages.completedSamples; session.Record("timing.native.complete", JsonUtility.ToJson(value.nativeStages));
            }
            previousEventCount = resultEvents; previousSampleTime = now;
            if (decoder != null) {
                value.hasVideoDecoder = true; value.videoPrepared = decoder.isPrepared; value.videoPlaying = decoder.isPlaying; value.videoPaused = decoder.isPaused;
                value.videoFrame = decoder.frame; value.videoTime = decoder.time; value.videoLength = decoder.length; value.videoFps = decoder.frameRate; value.videoUrl = decoder.url;
            }
            var regions = new List<string>();
            for (int i = 0; i < sdk.GetMaxBodies(); i++) regions.Add(i + ":" + (sdk.TryGetRegionOccupancy(i, out bool occupied) ? occupied ? "Occupied" : "Empty" : "Unknown"));
            value.regions = string.Join(";", regions);
            value.health = !sdk.IsRunning ? "InputNotRunning" : now - lastPublishedAdvance > 10 ? "InputPublicationStalled" :
                now - lastSubmittedAdvance > 10 ? "PublishedButNotSubmitted" : now - lastProcessedAdvance > 10 ? "SubmittedButNoNativeResult" :
                now - lastSdkResultAdvance > 10 ? "NativeResultsNotAcceptedBySdk" : !sdk.HasFreshResult ? "WaitingForFreshResult" : sdk.GetUsersCount() == 0 ? "CompletedNoBodies" : "SkeletonAvailable";
            displayedUnityFps = value.unityFramesFps; displayedPublicationFps = value.publicationFpsEstimate; displayedSkeletonFps = value.freshSdkEventsFps;
            displayedHealth = value.health == "PublishedButNotSubmitted" ? "画面有帧，推理未收到帧" :
                value.health == "SubmittedButNoNativeResult" ? "已提交，等待原生结果" :
                value.health == "NativeResultsNotAcceptedBySdk" ? "原生有结果，SDK 尚未接受" :
                value.health == "InputPublicationStalled" ? "画面发布停滞" :
                value.health == "CompletedNoBodies" ? "识别完成，当前未检测到人" :
                value.health == "SkeletonAvailable" ? "已有骨骼结果" : value.health == "InputNotRunning" ? "输入未启动" : "等待首次骨骼";
            string state = value.sdkState + "/" + value.sourceState + "/generation=" + value.generation;
            if (state != previousState) { session.Record("state.changed", state); previousState = state; CaptureNativeDiagnostics("state.changed"); }
            string errors = value.sourceError + " | " + value.adapterError + " | " + value.bridgeError + " | " + value.managerError + " | " + value.sdkError;
            if (errors != previousErrors) { session.Record("pipeline.errors.changed", errors); previousErrors = errors; CaptureNativeDiagnostics("pipeline.errors.changed"); }
            if (value.health != previousHealth) { session.Record("pipeline.health", value.health); previousHealth = value.health; CaptureNativeDiagnostics("pipeline.health"); }
            if (now >= nextNativeDiagnostics) CaptureNativeDiagnostics("periodic");
            // 草稿与实际成功应用的配置一起记录；保存/切换模式/人数/阈值/区域变化后可以还原操作上下文。
            string configuration = controller == null ? JsonUtility.ToJson(sdk.Configuration) :
                "draft=" + JsonUtility.ToJson(controller.Draft) + " active=" + JsonUtility.ToJson(controller.Active);
            if (configuration != previousConfiguration) { session.Record("configuration.changed", configuration); previousConfiguration = configuration; }
            session.Record("pipeline.snapshot", JsonUtility.ToJson(value));
            session.WriteCsvRow(new[] { value.utc, Number(value.elapsed_s), value.sourceMode, value.sdkState, value.sourceState,
                value.publishedFrame.ToString(Invariant), value.acceptedFrame.ToString(Invariant), value.resultFrame.ToString(Invariant), value.nativeSequence.ToString(Invariant), resultEvents.ToString(Invariant),
                value.submitted.ToString(Invariant), value.processed.ToString(Invariant), value.dropped.ToString(Invariant), value.nativeBodies.ToString(Invariant), value.sdkBodies.ToString(Invariant), value.fresh.ToString(),
                Number(value.publicationFpsEstimate), Number(value.freshSdkEventsFps), Number(value.nativeInputFps), Number(value.inferenceFps), Number(value.detectMs), Number(value.poseMs), Number(value.totalMs), Number(value.frameAgeMs),
                value.width.ToString(Invariant), value.height.ToString(Invariant), value.sourceId, value.generation, value.usesGpuFrames.ToString(), value.copiedFrames.ToString(Invariant), value.pendingCopies.ToString(Invariant),
                value.sourceError, value.adapterError, value.bridgeError, value.managerError, value.sdkError, value.health,
                value.unityFrameCount.ToString(Invariant), Number(value.unityFramesFps), Number(value.maximumUnityFrameMs), value.targetFrameRate.ToString(Invariant),
                bodyResultEvents.ToString(Invariant), Number(value.freshBodyResultsFps) });
        }
        private void CaptureSkeletons()
        {
            if (sdk == null || !sdk.HasFreshResult || sdk.ResultSequence == lastSkeletonSequence) return;
            lastSkeletonSequence = sdk.ResultSequence;
            session.Record("result.sample", "sequence=" + lastSkeletonSequence + " frame=" + sdk.SourceFrameId + " users=" + sdk.GetUsersCount() + " fresh=" + sdk.HasFreshResult);
            for (int i = 0; i < sdk.GetMaxBodies(); i++) {
                if (!sdk.CopySkeletonByIndex(i, joints, out var metadata) || !sdk.TryGetBodyByIndex(i, out var body)) continue;
                var value = new SkeletonRecord { utc = DateTime.UtcNow.ToString("O", Invariant), slot = i, stableTrackId = metadata.StableTrackId.ToString(Invariant),
                    resultSequence = metadata.ResultSequence, sourceFrame = metadata.SourceFrameId, observedUs = metadata.ObservationTimestampUs,
                    resultEvents = resultEvents, bodyConfidence = body.DetectionConfidence, bounds = body.BoundingBoxPixels, joints = new JointRecord[32] };
                for (int j = 0; j < 32; j++) {
                    var joint = joints[j]; var id = (HumanVisionCanonicalJointId)j;
                    bool api = sdk.TryGetJointByIndex(i, id, out _); if (api) ++value.apiValidJoints;
                    var point = new JointRecord { joint = id.ToString(), positionValid = joint.Position.Valid, apiValid = api,
                        derived = joint.Position.IsDerived, confidence = joint.Position.Confidence, pixel = joint.Position.Pixel, normalized = joint.Position.Normalized,
                        observedUs = joint.ObservationTimestampUs, predictionMs = joint.PredictionMilliseconds };
                    point.screenValid = sdk.TryGetJointScreenPosition(i, id, out point.screen);
                    point.worldPlaneValid = sdk.TryGetJointWorldPosition(i, id, out point.worldPlane); value.joints[j] = point;
                }
                session.WriteJsonLine("skeletons.jsonl", JsonUtility.ToJson(value));
            }
        }
        public void CopyLogPath()
        {
            GUIUtility.systemCopyBuffer = CurrentLogDirectory;
            session?.Flush(2048); folderMirror?.RequestSync();
            uiMessage = string.IsNullOrEmpty(CurrentLogDirectory) ? "日志目录创建失败：" + LastWriteError : "已复制本次日志目录";
            session?.Record("ui.copy.logPath", CurrentLogDirectory); RefreshUi();
        }
        public void ExportLogs()
        {
            try {
                CaptureStatistics(); CaptureSkeletons();
                var currentSdkSessions = Directory.Exists(sdkLogRoot) ? Directory.GetDirectories(sdkLogRoot).Where(p => !priorSdkSessions.Contains(p)).ToArray() : Array.Empty<string>();
                LastExportPath = session.Export(sdkLogRoot, currentSdkSessions);
                if (Application.platform == RuntimePlatform.Android) {
                    if (AndroidDiagnosticExport.TryCopyToDownloads(LastExportPath, out string publicPath, out string error)) {
                        LastExportPath = publicPath; session.Record("export.downloads", publicPath);
                    } else session.Record("export.downloads.failed", error);
                }
                uiMessage = "已导出 ZIP：" + LastExportPath; session.Flush(2048);
            } catch (Exception e) { uiMessage = "导出失败：" + DeviceDiagnosticSession.Redact(e.Message); session?.Record("export.failed", e.ToString()); }
            RefreshUi();
        }
        public void CopyExportPath()
        {
            if (string.IsNullOrEmpty(LastExportPath)) { uiMessage = "请先点击导出 ZIP"; RefreshUi(); return; }
            GUIUtility.systemCopyBuffer = LastExportPath; uiMessage = "已复制 ZIP 文件路径"; session?.Record("ui.copy.zipPath", LastExportPath); RefreshUi();
        }
        public void ToggleSkeletons()
        {
            recordSkeletons = !recordSkeletons; session?.Record("ui.skeleton.capture", recordSkeletons.ToString()); RefreshUi();
        }
        private void RefreshUi()
        {
            // 输入 FPS 来自真实发布帧推进；骨骼 FPS 仅统计新的 SDK ResultUpdated，
            // 避免把 Unity 渲染率或原生兼容统计中的 0 当成实际输入/骨骼帧率。
            string value = string.IsNullOrEmpty(LastWriteError) ? uiMessage + " | Unity " + displayedUnityFps.ToString("F1", Invariant) +
                " | 输入 " + displayedPublicationFps.ToString("F1", Invariant) + " | 含人体新结果 " + displayedBodyFps.ToString("F1", Invariant) +
                " FPS | " + displayedHealth + "\n日志：" + CurrentLogDirectory : "日志写入失败：" + LastWriteError;
            if (value != previousUiText && logPathLabel != null) { logPathLabel.text = value; previousUiText = value; }
            if (copyZipButton != null) copyZipButton.interactable = !string.IsNullOrEmpty(LastExportPath);
            if (skeletonButton != null) skeletonButton.GetComponentInChildren<Text>().text = "骨骼日志：" + (recordSkeletons ? "开启" : "关闭");
            if (hardwareLabel != null) {
                var h = hardware?.Read(); var n = stageTracker.Read();
                hardwareLabel.text = h == null ? "硬件采样准备中" :
                    "应用CPU " + Metric(h.appCpuPercent, "%全核") + " / " + Metric(h.appCpuOneCorePercent, "%一核") + " | 系统CPU " + Metric(h.systemCpuPercent, "%") +
                    " | GPU " + Metric(h.gpuPercent, "%") + "，频率 " + Metric(h.gpuFrequencyMHz, "MHz") + " | NPU：后端未使用\n" +
                    "内存PSS " + Metric(h.processPssMB, "MB") + " | 系统可用 " + Metric(h.systemAvailableMB, "MB") + " | 电池 " + Metric(h.batteryTemperatureC, "℃") + " | " + h.thermalStatus +
                    "\n" + (n.available ? "稀疏帧 " + n.frameId + "：预处理/等待 " + Number(n.importPreprocessRecordMs + n.preprocessSubmitWaitMs) + "ms，模型/内部等待 " + Number(n.extractDownloadMs) +
                        "ms，提交/下载等待 " + Number(n.inferenceSubmitWaitMs) + "ms，输出/释放 " + Number(n.denseOutputCopyMs + n.ownershipReleaseMs) + "ms" : "等待原生阶段计时；不可用原因详见硬件日志") +
                    "\n" + (folderMirror?.Status ?? "");
            }
        }
        private static string Metric(double number, string unit) => number < 0 ? "不可用" : number.ToString("F1", Invariant) + unit;
        private void OnDisable()
        {
            Application.logMessageReceivedThreaded -= OnUnityLog; Application.lowMemory -= OnLowMemory;
            if (sdk != null) {
                sdk.Initialized -= OnInitialized; sdk.Stopped -= OnStopped; sdk.ErrorOccurred -= OnSdkError; sdk.ResultUpdated -= OnResult;
                sdk.UserEntered -= OnUserEntered; sdk.UserExited -= OnUserExited; sdk.RegionOccupancyChanged -= OnRegionChanged;
            }
            nativeStages?.Dispose(); nativeStages = null;
            session?.Dispose();
            hardware?.Dispose(); hardware = null; folderMirror?.Dispose(); folderMirror = null;
            captureClosed = true;
            if (copyPathButton != null) copyPathButton.onClick.RemoveListener(CopyLogPath);
            if (exportButton != null) exportButton.onClick.RemoveListener(ExportLogs);
            if (copyZipButton != null) copyZipButton.onClick.RemoveListener(CopyExportPath);
            if (skeletonButton != null) skeletonButton.onClick.RemoveListener(ToggleSkeletons);
        }
    }
}
