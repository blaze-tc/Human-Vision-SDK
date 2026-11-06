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
        [SerializeField] private RuntimePrefabRedrawRig redrawRig;

        private static readonly Color Highlight = new Color(.22f,.91f,1);
        private static readonly Color Background = new Color(.048f,.071f,.105f);
        private static readonly Color Panel = new Color(.088f,.123f,.173f);
        private static readonly Color Inset = new Color(.025f,.042f,.063f);
        private static readonly Color Dim = new Color(.67f,.75f,.85f);
        private static readonly int[] CapacityChoices = {1, 2, 4, 8};
        private static readonly Rect LiveCard = new Rect(28, 152, 680, 460);
        private static readonly Rect PoseCard = new Rect(732, 152, 680, 460);

        private readonly RuntimeSkeletonReader skeletonReader = new RuntimeSkeletonReader();
        private readonly Vector2[,] viewportJoints = new Vector2[8, 32];
        private readonly bool[,] valid = new bool[8, 32];
        private readonly long[] ids = new long[8];
        private HumanVisionRuntimeView view;
        private HumanVisionSdkConfiguration recognition;
        private HumanVisionPresentedFrame pair;
        private HumanVisionTextureFrame input;
        private Texture live;
        private Font uiFont;
        private GUIStyle heading, title, body, small, field, button, selected, badge;
        private bool paired, hasInput, showSdkTexture, operating;
        private int presented, visibleJoints, visibleBones;
        private string status = "填入设备可访问的 RTSP 地址，然后点击连接";
        private string sdkLine = "SDK 未创建";
        private string poseLine = "等待同帧结果";
        private string inputLine = "等待实机图像";
        private float nextStatusAt;
        private readonly RuntimeAcceptanceTelemetry telemetry = new RuntimeAcceptanceTelemetry();

        private void Start()
        {
            Application.targetFrameRate = 60;
            telemetry.Reset();
            LoadRegionSettings();
            if (autoConnect) Run(Connect());
        }

        private void OnDestroy()
        {
            if (view != null) view.FramePresented -= CountPresented;
            if (uiFont != null) Destroy(uiFont);
        }

        private void LoadRegionSettings()
        {
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
            string address = rtspUrl.Trim();
            if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp" || string.IsNullOrEmpty(uri.Host)) {
                status = "请输入有效的 rtsp:// 地址";
                yield break;
            }
            if (view == null)
            {
                view = HumanVisionRuntimeView.Create();
                view.FramePresented += CountPresented;
            }
            var options = view.Sdk.Configuration;
            options.SourceKind = InputKind.Rtsp;
            options.Input.RtspUrl = address;
            options.RtspTcp = true;
            options.UseWindowsCpu = true;
            options.AccelerationMode = HumanVisionAccelerationMode.Graphics;
            if (Application.platform == RuntimePlatform.Android) options.InputQuality = ModelInputQuality.Low;
            options.Recognition = recognition.Clone();
            options.OpenInputOnInitialize = true;
            if (Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer) {
                string staged = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
                if (File.Exists(Path.Combine(staged, "index.json"))) options.RuntimeRoot = staged;
            }
            status = "正在连接 RTSP…";
            presented = 0;
            yield return view.Sdk.Initialize(options);
            status = view.Sdk.IsRunning ? "RTSP 已连接，等待 / 接收同帧结果" : view.Sdk.LastError;
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
            if (view != null && !view.Sdk.TryApplyConfiguration(requestedRecognition)) {
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

            telemetry.Observe(updateStart, view != null ? view.Sdk : null, paired, pair, visibleJoints, visibleBones);
            if (Time.unscaledTime < nextStatusAt) return;
            nextStatusAt = Time.unscaledTime + .15f;
            if (view == null) {
                sdkLine = "SDK 未创建";
            } else {
                var sdk = view.Sdk;
                sdkLine = $"{sdk.State}  |  {sdk.RuntimeProfile}  |  {sdk.FreshResultFps:F1} 识别 FPS  |  识别区域 {capacity}";
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
            InitStyles();
            var previousMatrix = GUI.matrix;
            float scale = Mathf.Min(Screen.width / 1440f, Screen.height / 866f);
            if (scale <= 0) return;
            GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 1440f * scale) / 2f, (Screen.height - 866f * scale) / 2f, 0),
                Quaternion.identity, Vector3.one * scale);
            try {
                Box(new Rect(0,0,1440,866), Background);
                GUI.Label(new Rect(28, 12, 900, 52), "Human Vision  /  Runtime Prefab 验收", heading);
                GUI.Label(new Rect(30, 65, 1250, 30), "01  实机图像    /    02  同帧骨骼    /    03  SDK 公共接口", small);
                Box(new Rect(1188, 24, 224, 34), Inset);
                GUI.Label(new Rect(1188, 24, 224, 34), $"SDK  {HumanVisionSdk.Version}", badge);

                GUI.Label(new Rect(28, 104, 118, 34), "RTSP 地址", body);
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
                for (int i = 0; i < CapacityChoices.Length; i++) {
                    int count = CapacityChoices[i];
                    if (GUI.Button(new Rect(181 + i*49, 674, 42, 33), count.ToString(), count == maxBodies ? selected : button)) SetCapacity(count);
                }
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
                GUI.Label(new Rect(29, 833, 1376, 29), "实时图像可先于识别更新。右侧只在图像与骨骼匹配时显示。", small);
            } finally {
                GUI.matrix = previousMatrix;
                GUI.enabled = true;
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
