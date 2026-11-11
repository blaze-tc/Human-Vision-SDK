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
    public sealed class RuntimePrefabAcceptance : MonoBehaviour
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

        private static readonly Color Highlight = new Color(.22f,.91f,1);
        private static readonly Color Background = new Color(.048f,.071f,.105f);
        private static readonly Color Panel = new Color(.088f,.123f,.173f);
        private static readonly Color Inset = new Color(.025f,.042f,.063f);
        private static readonly Color Dim = new Color(.67f,.75f,.85f);
        private const int MaxCapacity = 8;
        private const float CapacityRowHeight = 32;
        private static readonly Rect CapacityButton = new Rect(181, 674, 205, 33);
        private static readonly Rect CapacityMenu = new Rect(CapacityButton.x, CapacityButton.y - MaxCapacity * CapacityRowHeight - 10,
            CapacityButton.width, MaxCapacity * CapacityRowHeight + 6);
        private static readonly Rect LiveCard = new Rect(28, 152, 680, 460);
        private static readonly Rect PoseCard = new Rect(732, 152, 680, 460);

        private readonly RuntimeSkeletonReader skeletonReader = new RuntimeSkeletonReader();
        private readonly Vector2[,] viewportJoints = new Vector2[8, 32];
        private readonly bool[,] valid = new bool[8, 32];
        private readonly long[] ids = new long[8];
        private HumanVisionRuntimeView view;
        private HumanVisionSdkOptions startup;
        private HumanVisionSdkConfiguration recognition;
        private HumanVisionPresentedFrame pair;
        private HumanVisionTextureFrame input;
        private Texture live;
        private Font uiFont;
        private GUIStyle heading, title, body, small, field, button, selected, badge;
        private bool paired, hasInput, showSdkTexture, operating, capacityMenuOpen;
        private int presented, visibleJoints, visibleBones;
        private string status = "填入设备可访问的 RTSP 地址，然后点击连接";
        private string sdkLine = "SDK 未创建";
        private string poseLine = "等待同帧结果";
        private string inputLine = "等待实机图像";
        private float nextStatusAt;
        private float nextBackendAt;
        private string diagnosticProfile = "";
        private string actualBackend = "—";
        private readonly RuntimeAcceptanceTelemetry telemetry = new RuntimeAcceptanceTelemetry();

        private void Start()
        {
            Application.targetFrameRate = 60;
            telemetry.Reset();
            LoadStartup();
#if UNITY_EDITOR
            if (!string.IsNullOrWhiteSpace(editorVideoPath)) {
                startup.SourceKind = InputKind.Video;
                startup.Input.VideoPath = editorVideoPath.Trim();
                rtspUrl = startup.Input.VideoPath;
                Run(Connect());
                return;
            }
#endif
            if (autoConnect) Run(Connect());
        }

        private void OnDestroy()
        {
            if (view != null) view.FramePresented -= CountPresented;
            if (uiFont != null) Destroy(uiFont);
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
                AccelerationMode = IsAndroid ? HumanVisionAccelerationMode.Neural : HumanVisionAccelerationMode.Cpu,
                UseWindowsCpu = !IsAndroid,
                InputQuality = IsAndroid ? ModelInputQuality.Medium : ModelInputQuality.Low
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

        private void CountPresented(HumanVisionPresentedFrame frame) => presented++;

        private IEnumerator Connect()
        {
            var options = startup.Clone();
            string address = rtspUrl.Trim();
            switch (options.SourceKind) {
                case InputKind.WebCamera: options.Input.CameraDevice = address; break;
                case InputKind.Video: options.Input.VideoPath = address; break;
                case InputKind.Rtsp: options.Input.RtspUrl = address; break;
            }
            options.Recognition = recognition.Clone();
            options.OpenInputOnInitialize = true;
            yield return Initialize(options);
        }

        private IEnumerator Initialize(HumanVisionSdkOptions options)
        {
            try { options.Validate(); }
            catch (ArgumentException error) { status = error.Message; yield break; }
            if (view == null)
            {
                view = HumanVisionRuntimeView.Create();
                view.FramePresented += CountPresented;
            }
            if (string.IsNullOrWhiteSpace(options.RuntimeRoot) &&
                (Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer)) {
                string staged = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
                if (File.Exists(Path.Combine(staged, "index.json"))) options.RuntimeRoot = staged;
            }
            status = "正在启动 SDK…";
            presented = 0;
            yield return view.Sdk.Initialize(options);
            if (!view.Sdk.IsRunning || !string.IsNullOrEmpty(view.Sdk.LastError)) {
                status = view.Sdk.LastError;
                yield break;
            }
            startup = view.Sdk.Configuration;
            recognition = startup.Recognition.Clone();
            maxBodies = recognition.MaxBodies;
            nextBackendAt = 0;
            telemetry.Reset();
            status = "输入已连接，等待 / 接收同帧结果";
        }

        private static bool IsAndroid => Application.platform == RuntimePlatform.Android;
        private bool FixedQuality => IsAndroid && startup.AccelerationMode == HumanVisionAccelerationMode.Cpu;
        private static ModelInputQuality QualityForCompute(bool android, HumanVisionAccelerationMode mode, ModelInputQuality quality) =>
            android && mode == HumanVisionAccelerationMode.Cpu ? ModelInputQuality.Low : quality;

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
            actualBackend = profile.Length == 0 ? "—" : ReadActualBackend(sdk.RuntimeDiagnostics);
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
            bool running = view != null && view.Sdk.IsRunning;
            var options = running ? view.Sdk.Configuration : startup.Clone();
            options.AccelerationMode = mode;
            options.UseWindowsCpu = mode == HumanVisionAccelerationMode.Cpu;
            options.InputQuality = QualityForCompute(IsAndroid, mode, quality);
            if (running) Run(Initialize(options));
            else startup = options;
        }

        private void DrawModelButton(Rect rect, ModelInputQuality quality, string label)
        {
            if (GUI.Button(rect, label, startup.InputQuality == quality ? selected : button))
                SelectCompute(startup.AccelerationMode, quality);
        }

        private void DrawBackendButton(Rect rect, HumanVisionAccelerationMode mode, string label)
        {
            if (GUI.Button(rect, label, startup.AccelerationMode == mode ? selected : button))
                SelectCompute(mode, startup.InputQuality);
        }

        private IEnumerator Restart()
        {
            status = "正在重启 SDK…";
            presented = 0;
            yield return view.Sdk.RestartSdk();
            status = view.Sdk.IsRunning ? "SDK 已重启" : view.Sdk.LastError;
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
            var previous = view;
            previous.FramePresented -= CountPresented;
            yield return previous.Unload();
            view = null;
            status = "已卸载；点击连接重新创建";
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
            if (view != null && view.Sdk.IsRunning && view.Sdk.ActiveAccelerationMode == HumanVisionAccelerationMode.Neural) {
                var options = view.Sdk.Configuration;
                options.Recognition = requestedRecognition;
                Run(Initialize(options));
                return;
            }
            if (view != null && view.Sdk.IsInitialized && !view.Sdk.TryApplyConfiguration(requestedRecognition)) {
                status = view.Sdk.LastError;
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
            // Typical gameplay loop: acquire the public frame, then consume
            // world-space joint positions from the same published observation.
            live = view != null ? view.GetLiveTexture() : null;
            hasInput = view != null && view.Sdk.TryGetInputFrame(-1, out input);
            paired = skeletonReader.TryRead(view, out pair);
            visibleJoints = visibleBones = 0;
            int capacity = skeletonReader.RegionCount;
            int jointCount = skeletonReader.GetJointCount();
            for (int regionIndex = 0; regionIndex < ids.Length; regionIndex++)
            {
                ids[regionIndex] = 0;
                if (!paired || regionIndex >= capacity) continue;
                long userId = skeletonReader.GetUserIdByRegionIndex(regionIndex);
                if (userId == 0) continue;
                ids[regionIndex] = userId;
                for (int j = 0; j < jointCount; j++)
                {
                    valid[regionIndex,j] = skeletonReader.TryGetJointPositionByRegionIndex(regionIndex, (HumanVisionCanonicalJointId)j, out var position);
                    if (!valid[regionIndex,j]) continue;
                    var viewport = view.OffscreenCamera.WorldToViewportPoint(position);
                    viewportJoints[regionIndex,j] = new Vector2(viewport.x, viewport.y);
                    visibleJoints++;
                }
            }

            if (redrawRig != null) {
                redrawRig.Draw(paired ? pair.ColorTexture : null, ids, viewportJoints, valid);
                visibleBones = redrawRig.VisibleLines;
            }

            var activeSdk = view != null ? view.Sdk : null;
            UpdateActualBackend(activeSdk);
            telemetry.Observe(updateStart, activeSdk, actualBackend, paired, pair, visibleJoints, visibleBones);
            if (Time.unscaledTime < nextStatusAt) return;
            nextStatusAt = Time.unscaledTime + .15f;
            if (view == null) {
                sdkLine = "SDK 未创建";
            } else {
                var sdk = view.Sdk;
                var size = sdk.AnalysisInputSize;
                string modelSize = size.x > 0 ? $"{size.x}×{size.y}" : "—";
                sdkLine = $"{sdk.State}  |  模型 {modelSize}  |  实际后端 {actualBackend}  |  区域 {capacity}";
                if (!view.SupportsMatchedFrames) sdkLine += "  |  当前图形设备不支持同帧纹理";
            }
            inputLine = hasInput && live != null
                ? $"{live.width} × {live.height}  ·  输入 #{input.FrameId}  ·  {view.Sdk.InputPublicationFps:F1} FPS"
                : "等待实机图像";
            poseLine = paired
                ? $"输入 #{pair.InputFrameId}  ·  提交 #{pair.SourceFrameId}  ·  结果 #{pair.ResultSequence}  ·  已配对 {presented}"
                : "等待同帧结果";
        }

        private void OnGUI()
        {
            if (startup == null) return;
            InitStyles();
            var previousMatrix = GUI.matrix;
            float scale = Mathf.Min(Screen.width / 1440f, Screen.height / 866f);
            if (scale <= 0) return;
            GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 1440f * scale) / 2f, (Screen.height - 866f * scale) / 2f, 0),
                Quaternion.identity, Vector3.one * scale);
            try {
                DismissCapacityMenu();
                Box(new Rect(0,0,1440,866), Background);
                GUI.Label(new Rect(28, 12, 900, 52), "Human Vision  /  Runtime Prefab 验收", heading);
                GUI.Label(new Rect(30, 65, 135, 30), FixedQuality ? "固定模型等级" : "模型等级", small);
                GUI.enabled = !operating && !FixedQuality;
                DrawModelButton(new Rect(177, 64, 80, 32), ModelInputQuality.Low, "低");
                DrawModelButton(new Rect(265, 64, 80, 32), ModelInputQuality.Medium, "中");
                DrawModelButton(new Rect(353, 64, 80, 32), ModelInputQuality.High, "高");
                GUI.enabled = !operating;
                GUI.Label(new Rect(894, 65, 140, 30), "分析设备", small);
                DrawBackendButton(new Rect(1020, 64, 105, 32), HumanVisionAccelerationMode.Cpu, "CPU");
                DrawBackendButton(new Rect(1133, 64, 105, 32), HumanVisionAccelerationMode.Graphics, "GPU");
                GUI.enabled = !operating && IsAndroid;
                DrawBackendButton(new Rect(1246, 64, 166, 32), HumanVisionAccelerationMode.Neural, "NPU · RK3588");
                GUI.enabled = true;
                Box(new Rect(1188, 24, 224, 34), Inset);
                GUI.Label(new Rect(1188, 24, 224, 34), $"SDK  {HumanVisionSdk.Version}", badge);

                string addressLabel = startup.SourceKind == InputKind.WebCamera ? "设备名称" :
                    startup.SourceKind == InputKind.Video ? "视频路径" : "RTSP 地址";
                GUI.Label(new Rect(28, 104, 118, 34), addressLabel, body);
                rtspUrl = GUI.TextField(new Rect(146, 104, 1060, 34), rtspUrl, field);
                GUI.enabled = !operating;
                if (GUI.Button(new Rect(1224, 104, 188, 34), "连接 / 应用地址", selected)) Run(Connect());
                GUI.enabled = true;

                DrawCard(LiveCard, "实时输入 · 识别区域", live, inputLine, showRegions: true);
                DrawCard(PoseCard, showSdkTexture ? "Prefab 合成画面" : "公共接口重绘 · Update()",
                    paired ? (showSdkTexture ? pair.OverlayTexture : redrawRig?.Output) : null, poseLine);

                Box(new Rect(28, 624, 1384, 205), Panel);
                GUI.Label(new Rect(45, 633, 870, 32), sdkLine, body);
                GUI.Label(new Rect(930, 633, 466, 32),
                    paired ? $"{pair.BodyCount} 人  ·  {visibleJoints} 个点  ·  {visibleBones} 条线" : "没有可绘制的同帧骨骼", small);
                GUI.Label(new Rect(45, 674, 145, 33), "区域数量", body);
                GUI.enabled = !operating;
                if (GUI.Button(CapacityButton, $"{maxBodies} 人  ▾", capacityMenuOpen ? selected : button))
                    capacityMenuOpen = !capacityMenuOpen;
                GUI.enabled = view != null && !operating;
                if (GUI.Button(new Rect(437, 674, 128, 33), "重启 SDK", button)) Run(Restart());
                if (GUI.Button(new Rect(578, 674, 128, 33), "停止 SDK", button)) Run(StopRuntime());
                if (GUI.Button(new Rect(719, 674, 128, 33), "卸载 Prefab", button)) Run(Unload());
                GUI.enabled = true;
                if (GUI.Button(new Rect(935, 674, 205, 33), "公共接口重绘", !showSdkTexture ? selected : button)) showSdkTexture = false;
                if (GUI.Button(new Rect(1153, 674, 243, 33), "Prefab 合成", showSdkTexture ? selected : button)) showSdkTexture = true;
                GUI.Label(new Rect(45, 720, 1349, 32), status ?? "", small);
                GUI.Label(new Rect(45, 754, 1349, 31), telemetry.PerformanceLine, small);
                GUI.Label(new Rect(45, 785, 1349, 31), telemetry.ReadLine, small);
                GUI.Label(new Rect(29, 833, 1376, 29), telemetry.StageLine, small);
                if (capacityMenuOpen) DrawCapacityMenu();
            } finally {
                GUI.matrix = previousMatrix;
                GUI.enabled = true;
            }
        }

        private void DismissCapacityMenu()
        {
            if (!capacityMenuOpen) return;
            if (operating) {
                capacityMenuOpen = false;
                return;
            }

            Event current = Event.current;
            bool outsideClick = current.type == EventType.MouseDown &&
                !CapacityButton.Contains(current.mousePosition) && !CapacityMenu.Contains(current.mousePosition);
            if (outsideClick || (current.type == EventType.KeyDown && current.keyCode == KeyCode.Escape)) {
                capacityMenuOpen = false;
                current.Use();
            }
        }

        private void DrawCapacityMenu()
        {
            Box(CapacityMenu, Inset);
            for (int count = 1; count <= MaxCapacity; count++) {
                var row = new Rect(CapacityMenu.x + 3, CapacityMenu.y + 3 + (count - 1) * CapacityRowHeight,
                    CapacityMenu.width - 6, CapacityRowHeight);
                if (GUI.Button(row, $"{count} 人", count == maxBodies ? selected : button)) {
                    capacityMenuOpen = false;
                    SetCapacity(count);
                }
            }
        }

        private void DrawCard(Rect card, string label, Texture texture, string detail, bool showRegions = false)
        {
            Box(card, Panel);
            GUI.Label(new Rect(card.x + 17, card.y + 4, card.width - 34, 44), label, title);
            Rect window = new Rect(card.x + 16, card.y + 49, card.width - 32, 365);
            Box(window, Inset);
            bool hasTexture = texture != null && texture.width > 0 && texture.height > 0;
            if (hasTexture || showRegions) {
                int width = hasTexture ? texture.width : 1280;
                int height = hasTexture ? texture.height : 720;
                float scale = Mathf.Min(window.width / width, window.height / height);
                var image = new Rect(window.center.x - width * scale / 2, window.center.y - height * scale / 2,
                    width * scale, height * scale);
                if (hasTexture) GUI.DrawTexture(image, texture, ScaleMode.StretchToFill, false);
                if (showRegions && recognition != null) DrawRegions(image);
            }
            if (!hasTexture) GUI.Label(window, "等待输入 / 结果", badge);
            GUI.Label(new Rect(card.x + 16, card.y + 418, card.width - 32, 34), detail, small);
        }

        private void DrawRegions(Rect image)
        {
            for (int i = 0; i < recognition.Regions.Length; i++) {
                Rect region = recognition.Regions[i];
                var bounds = new Rect(image.x + region.x * image.width, image.y + region.y * image.height,
                    region.width * image.width, region.height * image.height);
                var border = RuntimePrefabRedrawRig.GetSlotColor(i);
                Box(new Rect(bounds.x, bounds.y, bounds.width, 1), border);
                Box(new Rect(bounds.x, bounds.yMax - 1, bounds.width, 1), border);
                Box(new Rect(bounds.x, bounds.y, 1, bounds.height), border);
                Box(new Rect(bounds.xMax - 1, bounds.y, 1, bounds.height), border);
                var label = new Rect(bounds.x + 3, bounds.y + 3, Mathf.Min(bounds.width, 86), 23);
                Box(label, Inset);
                GUI.Label(label, $"区域 {i}", badge);
            }
        }

        private static void Box(Rect rect, Color color)
        {
            if (Event.current.type != EventType.Repaint) return;
            Color previous = GUI.color;
            GUI.color = color;
            GUI.DrawTexture(rect, Texture2D.whiteTexture);
            GUI.color = previous;
        }

        private void InitStyles()
        {
            if (heading != null) return;
            uiFont = Font.CreateDynamicFontFromOSFont(new[] {"Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans CJK SC", "Droid Sans Fallback", "Arial"}, 22);
            heading = Text(34, Color.white, true);
            title = Text(25, Color.white, true);
            body = Text(20, Color.white, false);
            small = Text(17, Dim, false);
            badge = Text(17, Color.white, true);
            badge.alignment = TextAnchor.MiddleCenter;
            field = new GUIStyle(GUI.skin.textField) { font = uiFont, fontSize = 20 };
            button = new GUIStyle(GUI.skin.button) { font = uiFont, fontSize = 19 };
            selected = new GUIStyle(button) { fontStyle = FontStyle.Bold };
            selected.normal.textColor = Highlight;
        }

        private GUIStyle Text(int size, Color color, bool bold)
        {
            var style = new GUIStyle(GUI.skin.label) {
                font = uiFont, fontSize = size, alignment = TextAnchor.MiddleLeft,
                fontStyle = bold ? FontStyle.Bold : FontStyle.Normal, clipping = TextClipping.Clip, wordWrap = false
            };
            style.normal.textColor = color;
            return style;
        }
    }
}
