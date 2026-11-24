using System;
using System.Collections;
using System.IO;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVisionAcceptance
{
    [DefaultExecutionOrder(300)]
    public sealed partial class RuntimePrefabAcceptance : MonoBehaviour
    {
        [SerializeField] private string rtspUrl = "";
        [SerializeField, Range(1, 8)] private int maxBodies = 4;
        [SerializeField] private bool autoConnect;
#if UNITY_EDITOR
        [SerializeField, Tooltip("Optional local video played when entering Play Mode in the Editor.")]
        private string editorVideoPath = "";
#endif
        [SerializeField] private HumanVisionSdkPreset startupPreset;
        [SerializeField] private RuntimePrefabRedrawRig redrawRig;

        private const int MaxCapacity = 8;

        private HumanVisionRuntimeView view;
        private HumanVisionSdkOptions startup;
        private HumanVisionSdkConfiguration recognition;
        private HumanVisionTextureFrame input;
        private Texture live;
        private bool hasInput, operating;
        private bool showSkeleton = true;
        private int visibleJoints, visibleBones;
        private string status = "打开输入连接，选择 RTSP 或本地相机";
        private string sdkLine = "SDK 未创建";
        private string inputLine = "等待实机图像";
        private float nextStatusAt;
        private float nextBackendAt;
        private string diagnosticProfile = "";
        private string actualBackend = "—";
        private readonly RuntimeAcceptanceTelemetry telemetry = new RuntimeAcceptanceTelemetry();

        private const int MeasurementSeconds = 180;
        private readonly RuntimePrefabAcceptanceDiagnostics diagnostics = new RuntimePrefabAcceptanceDiagnostics();
        private Vector2Int displayedDecodedSize;
        private Vector2Int lastLoggedDecoderSize, lastLoggedPreviewSize;
        private ulong lastLoggedSource, lastLoggedGeneration, lastLoggedNativeGeneration;
        private bool campaignRunning, campaignCancelled;
        private string diagnosticPhase = "idle";
        private bool inputOpened;

        private void Start()
        {
            if (sceneRuntimeView == null || sceneRuntimeView.Sdk == null || HumanVisionRuntimeView.Instance != sceneRuntimeView)
            {
                Debug.LogError("RuntimePrefabAcceptance 需要在 Inspector 引用此场景唯一且已启用的 HumanVisionRuntime。", this);
                enabled = false;
                return;
            }
            if (!BindAcceptanceUi())
            {
                enabled = false;
                return;
            }

            ApplySceneFont();
            Application.targetFrameRate = 60;
            telemetry.Reset();
            if (redrawRig != null) redrawRig.gameObject.SetActive(false);
            LoadStartup();
            Run(PrepareManualChoices());
        }

        private void OnDestroy()
        {
            DisposeAcceptanceUi();
            if (previewColorMaterial != null) Destroy(previewColorMaterial);
            if (view != null) view.FramePresented -= CountPresented;
            DisposeSceneFont();
            diagnostics.Dispose();
        }

        private void LoadStartup()
        {
            var preset = startupPreset != null ? startupPreset : HumanVisionSdkPreset.LoadProjectDefault();
            if (preset != null) {
                startup = preset.CreateOptions();
                maxBodies = startup.Recognition.MaxBodies;
                recognition = startup.Recognition.Clone();
                if (!recognition.UseRegions)
                    recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(maxBodies);
                recognition.UseRegions = true;
                rtspUrl = InputAddress(startup);
                status = "已载入 SDK 默认配置";
                return;
            }

            startup = new HumanVisionSdkOptions {
                SourceKind = InputKind.Rtsp,
                AccelerationMode = IsAndroid ? HumanVisionAccelerationMode.Graphics : HumanVisionAccelerationMode.Cpu,
                UseWindowsCpu = !IsAndroid,
                InputQuality = ModelInputQuality.Low,
                UseModelSizedAnalysisInput = true
            };
            recognition = new HumanVisionSdkConfiguration {
                MaxBodies = maxBodies, UseRegions = true, Regions = HumanVisionSdkConfiguration.CreateEqualRegions(maxBodies)
            };
            string path = HumanVisionSdkSettingsStore.DefaultPath;
            if (!File.Exists(path)) return;
            try {
                var saved = HumanVisionSdkSettingsStore.Load(path).Recognition;
                if (!saved.UseRegions) return;
                recognition = saved.Clone();
                maxBodies = recognition.MaxBodies;
                status = "已载入 Settings 的识别区域，请连接 RTSP";
            } catch (Exception e) {
                status = "读取识别区域失败，使用均分区域";
                Debug.LogWarning("Runtime Prefab could not load saved Settings regions: " + e.Message);
            }
        }

        private void CountPresented(HumanVisionPresentedFrame frame) =>
            telemetry.ObservePresented(in frame, diagnostics.ObservePresented(in frame));

        private void EnsureRuntimeView()
        {
            if (view == null)
            {
                view = sceneRuntimeView;
                view.FramePresented += CountPresented;
            }
            view.RetainLastOverlayDuringPairGaps = true;
            view.UseAnalysisFrameForMatchedPreview = false;
            RefreshPresentationMode();
        }

        private IEnumerator Initialize(HumanVisionSdkOptions options)
        {
            bool saveInput = rememberConnectedInput;
            rememberConnectedInput = false;
            inputOpened = false;
            diagnostics.SetInputAddress(options.SourceKind == InputKind.Rtsp ? options.Input.RtspUrl : null);
            try {
                ConfigureInteractiveOptions(options);
                options.Validate();
            }
            catch (ArgumentException error) { status = error.Message; yield break; }
            EnsureRuntimeView();
            diagnostics.BindSdk(view.Sdk);

            if (string.IsNullOrWhiteSpace(options.RuntimeRoot) &&
                (Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer)) {
                string staged = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
                if (File.Exists(Path.Combine(staged, "index.json"))) options.RuntimeRoot = staged;
            }
            status = "正在启动 SDK…";
            yield return view.Sdk.Initialize(options);
            if (!view.Sdk.IsRunning || !string.IsNullOrEmpty(view.Sdk.LastError)) {
                status = view.Sdk.LastError;
                status = diagnostics.Redact(status);
                if (saveInput && options.SourceKind == InputKind.Rtsp &&
                    Uri.TryCreate(options.Input.RtspUrl, UriKind.Absolute, out var address) && address.IsLoopback)
                {
                    int port = address.Port > 0 ? address.Port : 554;
                    status += " · 本机 RTSP 端口 " + port + "，请检查样本流是否运行。";
                }
                yield break;
            }
            startup = view.Sdk.Configuration;
            recognition = startup.Recognition.Clone();
            maxBodies = recognition.MaxBodies;
            RememberInteractiveConfiguration();
            nextBackendAt = 0;
            telemetry.Reset();
            status = "输入已连接";
            inputOpened = true;
            displayedDecodedSize = default;
            lastLoggedDecoderSize = lastLoggedPreviewSize = default;
            lastLoggedSource = lastLoggedGeneration = lastLoggedNativeGeneration = 0;
            if (startup.SourceKind == InputKind.Rtsp)
                diagnostics.RecordStreamSelection(diagnosticPhase, view.Sdk.SelectedRtspStreamLabel, view.Sdk.SelectedRtspStreamSize,
                    startup.AutoSelectRtspStream ? "automatic" : "specified", RequestedInputSize);
            if (saveInput)
                SaveConnectedInput();
        }

        private static bool IsAndroid => Application.platform == RuntimePlatform.Android;

        private Vector2Int RequestedInputSize =>
            new Vector2Int(startup.Input.RequestedWidth, startup.Input.RequestedHeight);

        private IEnumerator MeasureAcceptance()
        {
            campaignRunning = true;
            campaignCancelled = false;
            var bounds = RequestedInputSize;
            string outcome = "failed";
            try {
                yield return ConnectInteractive();
                diagnosticPhase = "measurement";
                var sdk = view?.Sdk;
                double deadline = Time.realtimeSinceStartupAsDouble + 20;
                while (!campaignCancelled && inputOpened && sdk != null && sdk.IsRunning && !sdk.HasFreshResult &&
                    Time.realtimeSinceStartupAsDouble < deadline) yield return null;
                if (campaignCancelled) yield break;
                if (!inputOpened || sdk == null || !sdk.IsRunning || !sdk.HasFreshResult) {
                    status = "测试未取得新结果，" + status;
                    yield break;
                }

                long firstResult = diagnostics.AcceptedResults;
                long firstSkeleton = diagnostics.ValidSkeletonResults;
                long latest = firstResult;
                double now = Time.realtimeSinceStartupAsDouble;
                double finish = now + MeasurementSeconds;
                double lastResultAt = now;
                diagnostics.Mark(diagnosticPhase, "ready", bounds, sdk: sdk);
                while (!campaignCancelled && sdk.IsRunning && now < finish) {
                    if (diagnostics.AcceptedResults != latest) {
                        latest = diagnostics.AcceptedResults;
                        lastResultAt = now;
                    }
                    if (now - lastResultAt >= 5) break;
                    status = $"测试剩余 {Mathf.CeilToInt((float)(finish - now))} 秒";
                    yield return null;
                    now = Time.realtimeSinceStartupAsDouble;
                }

                long skeletons = diagnostics.ValidSkeletonResults - firstSkeleton;
                if (!campaignCancelled && sdk.IsRunning && now >= finish)
                    outcome = skeletons > 0 ? "complete" : "partial";
                diagnostics.Mark(diagnosticPhase, "result", bounds,
                    $"accepted_results={diagnostics.AcceptedResults - firstResult} valid_skeleton_results={skeletons}", sdk);
                status = outcome == "complete" ? "测试已完成，继续显示当前输入" :
                    outcome == "partial" ? "测试已结束，有效骨骼证据不足" : "测试中止，结果停止更新";
            } finally {
                if (campaignCancelled) status = "已取消一键测试";
                diagnostics.Mark("measurement", campaignCancelled ? "cancelled" : outcome, bounds, sdk: view?.Sdk);
                diagnostics.Export();
                status += " · " + diagnostics.Status;
                diagnosticPhase = "idle";
                campaignRunning = false;
            }
        }

        internal static string NormalizeBackend(string value, string profile) =>
            value == "CPU" && (profile == "windows-yolo-cpu-low" || profile == "android-cpu-nohands") ? "backend.ort.cpu" : value;

        private static string ReadActualBackend(string diagnostics)
        {
            const string key = "Actual backend=";
            int start = diagnostics?.IndexOf(key, StringComparison.Ordinal) ?? -1;
            if (start < 0 || (start > 0 && diagnostics[start - 1] != '\n')) return "—";
            start += key.Length;
            int end = diagnostics.IndexOf('\n', start);
            string value = diagnostics.Substring(start, (end < 0 ? diagnostics.Length : end) - start).Trim();
            return value.Length == 0 ? "—" : value;
        }

        private void UpdateActualBackend(HumanVisionSdk sdk)
        {
            string profile = sdk != null && sdk.IsInitialized ? sdk.RuntimeProfile : "";
            if (profile == diagnosticProfile && Time.unscaledTime < nextBackendAt) return;
            diagnosticProfile = profile;
            nextBackendAt = Time.unscaledTime + 1f;
            actualBackend = profile.Length == 0 ? "—" : NormalizeBackend(ReadActualBackend(sdk.RuntimeDiagnostics), profile);
        }

        private static string InputAddress(HumanVisionSdkOptions options)
        {
            switch (options.SourceKind) {
                case InputKind.WebCamera: return options.Input.CameraDevice ?? "";
                case InputKind.Video: return options.Input.VideoPath ?? "";
                default: return options.Input.RtspUrl ?? "";
            }
        }

        private void SelectCompute(HumanVisionAccelerationMode mode, ModelInputQuality quality)
        {
            if (!HasManualQuality(mode, quality) ||
                startup.AccelerationMode == mode && startup.InputQuality == quality) return;
            bool running = view != null && view.Sdk.IsRunning;
            var options = running ? view.Sdk.Configuration : startup.Clone();
            options.AccelerationMode = mode;
            options.UseWindowsCpu = mode == HumanVisionAccelerationMode.Cpu;
            options.InputQuality = quality;
            options.UseModelSizedAnalysisInput = true;
            if (running) Run(Initialize(options));
            else startup = options;
        }

        private IEnumerator Restart()
        {
            yield return ConnectInteractive();
        }

        private IEnumerator StopRuntime()
        {
            status = "正在停止 SDK…";
            yield return view.Sdk.StopSdk();
            status = "已停止，Prefab 仍保留";
        }

        private IEnumerator Unload()
        {
            status = "正在卸载 SDK…";
            view.FramePresented -= CountPresented;
            yield return view.Sdk.Shutdown();
            view = null;
            status = "SDK 已卸载，场景对象保留。";
        }

        private void Run(IEnumerator action)
        {
            if (operating) return;
            operating = true;
            StartCoroutine(Finish(action));
        }

        private IEnumerator Finish(IEnumerator action)
        {
            try { yield return action; }
            finally { operating = false; nextStatusAt = 0; }
        }

        private void SetCapacity(int requested)
        {
            if (operating || requested == maxBodies) return;
            var requestedRecognition = recognition.Clone();
            requestedRecognition.MaxBodies = requested;
            requestedRecognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(requested);
            if (view != null && view.Sdk.IsInitialized) {
                Run(ConnectInteractive(requestedRecognition));
                return;
            }
            recognition = requestedRecognition;
            maxBodies = requested;
            status = "已在本次 Demo 中均分为 " + maxBodies + " 个区域";
            nextStatusAt = 0;
        }

        private void Update()
        {
            long updateStart = System.Diagnostics.Stopwatch.GetTimestamp();
            var activeSdk = view != null ? view.Sdk : null;
            RefreshLivePreview(activeSdk);
            TrackVisiblePreview();
            int capacity = activeSdk != null ? activeSdk.GetMaxBodies() : maxBodies;
            visibleJoints = liveSkeleton.VisibleJoints;
            visibleBones = liveSkeleton.VisibleBones;
            UpdateActualBackend(activeSdk);
            if (hasInput) diagnostics.ObservePreview(in input);
            diagnostics.Sample(activeSdk, diagnosticPhase);
            HumanVisionPresentedFrame matched = default;
            bool paired = photoCaptureActive && view != null && view.TryGetFrame(out matched);
            telemetry.Observe(updateStart, activeSdk, actualBackend, paired, matched, visibleJoints, visibleBones,
                diagnostics.ValidSkeletonFps, previewFps);
            ObserveDisplayedStream(activeSdk);
            if (Time.unscaledTime < nextStatusAt) return;
            nextStatusAt = Time.unscaledTime + .15f;
            if (view == null) {
                sdkLine = "SDK 未创建";
            } else {
                var sdk = view.Sdk;
                var size = sdk.AnalysisInputSize;
                string modelSize = size.x > 0 ? $"{size.x}×{size.y}" : "—";
                var submitted = sdk.SubmittedAnalysisInputSize;
                string analysisSize = submitted.x > 0 ? $"{submitted.x}×{submitted.y}" : "等待";
                sdkLine = $"{sdk.State}  |  实际送入 {analysisSize} / {modelSize}  |  SDK取帧观察 {sdk.AnalysisInputPublicationFps:F1} FPS  |  {actualBackend}  |  区域 {capacity}";
                if (!view.SupportsMatchedFrames) sdkLine += "  |  当前图形设备不支持同帧纹理";
            }
            inputLine = hasInput && live != null
                ? $"{startup.SourceKind} · 预览 {input.Width}×{input.Height} · 输入 #{input.FrameId} · 解码 {FormatResolution(displayedDecodedSize)}"
                : startup.PreviewEnabled ? "等待当前输入" : "预览已关闭，识别可继续";
        }

        private void ObserveDisplayedStream(HumanVisionSdk sdk)
        {
            if (!hasInput || live == null) {
                displayedDecodedSize = default;
                return;
            }
            bool hasDecoded = sdk.TryGetInputDecodedGeometry(in input, out var decoded, out ulong nativeGeneration);
            displayedDecodedSize = decoded;
            // Android reports decoder geometry separately from the published texture size.
            if (IsAndroid && !hasDecoded) return;
            var preview = new Vector2Int(input.Width, input.Height);
            if (decoded == lastLoggedDecoderSize && preview == lastLoggedPreviewSize && input.SourceId == lastLoggedSource &&
                input.Generation == lastLoggedGeneration && nativeGeneration == lastLoggedNativeGeneration) return;
            lastLoggedDecoderSize = decoded;
            lastLoggedPreviewSize = preview;
            lastLoggedSource = input.SourceId;
            lastLoggedGeneration = input.Generation;
            lastLoggedNativeGeneration = nativeGeneration;
            diagnostics.RecordDisplayedStream(diagnosticPhase, in input, decoded, nativeGeneration);
            nextStatusAt = 0;
        }

        private static string FormatResolution(Vector2Int size) => size.x > 0 && size.y > 0 ? $"{size.x}×{size.y}" : "未报告";

        private void LateUpdate() => RenderAcceptanceUi();
    }
}
