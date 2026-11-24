using HumanVision;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using UI = HumanVisionAcceptance.RuntimeAcceptanceUi;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private static readonly HumanVisionAccelerationMode[] UiModes = {
            HumanVisionAccelerationMode.Cpu, HumanVisionAccelerationMode.Graphics, HumanVisionAccelerationMode.Neural
        };
        private static readonly ModelInputQuality[] UiQualities = {
            ModelInputQuality.Low, ModelInputQuality.Medium, ModelInputQuality.High, ModelInputQuality.UltraLow
        };
        private readonly RuntimePrefabSceneSkeleton sceneSkeleton = new RuntimePrefabSceneSkeleton();
        private RectTransform demoCanvas, safeUi, headerUi, leftUi, rightUi, previewViewport, regionPlane;
        private RawImage sourceImage, sceneSkeletonImage;
        private Text previewInfo, previewEmpty, deviceTemperatures, inputSummary, logText, exportText;
        private ScrollRect logScroll;
        private Button connectionButton, settingsButton, photoButton, restartButton, stopButton, unloadButton;
        private Button skeletonButton, fullscreenButton, cropButton, campaignButton;
        private readonly Button[] modeButtons = new Button[UiModes.Length];
        private readonly Button[] qualityButtons = new Button[UiQualities.Length];
        private readonly Button[] capacityButtons = new Button[MaxCapacity];
        private readonly RectTransform[] regionViews = new RectTransform[MaxCapacity];
        private readonly Rect[] displayedRegions = new Rect[MaxCapacity];
        private Vector2 lastScreen;
        private Rect lastSafe, lastImageBox;
        private bool? lastFullscreen;
        private int lastPreviewWidth, lastPreviewHeight;
        private bool lastPreviewCrop;
        private float nextUiTextAt;

        private void BuildAcceptanceUi()
        {
            if (demoCanvas != null) return;
            if (Application.isPlaying && uiFont == null)
                uiFont = Font.CreateDynamicFontFromOSFont(new[] { "Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans CJK SC", "Droid Sans Fallback", "Arial" }, 22);
            UI.Font = uiFont;
            demoCanvas = UI.Rect(transform, "Runtime Acceptance UGUI");
            var canvas = demoCanvas.gameObject.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = 20;
            var scaler = demoCanvas.gameObject.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1440, 866);
            scaler.matchWidthOrHeight = .50f;
            demoCanvas.gameObject.AddComponent<GraphicRaycaster>();
            if (EventSystem.current == null) {
                var eventSystem = new GameObject("Runtime Acceptance EventSystem", typeof(EventSystem), typeof(StandaloneInputModule));
                eventSystem.transform.SetParent(transform, false);
            }
            UI.Fill(UI.Panel(demoCanvas, "Backdrop", UI.Background));
            safeUi = UI.Rect(demoCanvas, "Safe Area");
            UI.Fill(safeUi);

            headerUi = UI.Row(safeUi, "Header");
            UI.Height(UI.Label(headerUi, "Human Vision   /   实时识别", 29), 54);
            var version = UI.Label(headerUi, "SDK  " + HumanVisionSdk.Version, 17);
            version.color = UI.Muted;
            version.alignment = TextAnchor.MiddleRight;
            Place(headerUi, new Vector2(0, 1), Vector2.one, new Vector2(18, -72), new Vector2(-18, -12));

            leftUi = UI.Panel(safeUi, "Controls and Diagnostics", UI.Surface);
            Place(leftUi, Vector2.zero, new Vector2(.34f, 1), new Vector2(18, 16), new Vector2(-8, -78));
            BuildLeftControls();

            rightUi = UI.Panel(safeUi, "Live Preview", UI.Surface);
            BuildPreviewUi();
            SetPreviewLayout(false);
            var modalHost = UI.Rect(safeUi, "Dialogs");
            UI.Fill(modalHost);
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            BuildConnectionUi(modalHost);
            BuildAdvancedUi(modalHost);
#endif
        }

        private void BuildLeftControls()
        {
            var controls = UI.Scroll(leftUi, "Control Scroll");
            Place((RectTransform)controls.parent.parent, new Vector2(0, .43f), Vector2.one,
                new Vector2(14, 8), new Vector2(-14, -14));
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            connectionButton = UI.Button(controls, "输入连接", OpenConnectionUi, 44);
            UI.Select(connectionButton, true);
            settingsButton = UI.Button(controls, "识别与显示设置", ShowAdvancedUi);
#else
            connectionButton = UI.Button(controls, "输入连接", () => Run(Connect()), 44);
#endif
            inputSummary = UI.Label(controls, "", 17);
            inputSummary.color = UI.Muted;
            UI.Height(inputSummary, 28);
            UI.Label(controls, "推理后端", 19);
            var backends = UI.Row(controls, "Backend Options");
            UI.Height(backends, 36);
            string[] backendNames = { "CPU", "GPU", "NPU" };
            for (int i = 0; i < UiModes.Length; i++) {
                var mode = UiModes[i];
                modeButtons[i] = UI.Button(backends, backendNames[i], () => SelectBackendFromUi(mode), 36);
            }
            UI.Label(controls, "模型等级", 19);
            var qualities = UI.Row(controls, "Model Quality Options");
            UI.Height(qualities, 36);
            string[] qualityNames = { "低", "中", "高", "超低" };
            for (int i = 0; i < UiQualities.Length; i++) {
                var quality = UiQualities[i];
                qualityButtons[i] = UI.Button(qualities, qualityNames[i], () => SelectCompute(startup.AccelerationMode, quality), 36);
            }
            UI.Label(controls, "人数上限", 19);
            for (int line = 0; line < 2; line++) {
                var row = UI.Row(controls, "Body Capacity");
                UI.Height(row, 34);
                for (int col = 0; col < 4; col++) {
                    int value = line * 4 + col + 1;
                    capacityButtons[value - 1] = UI.Button(row, value + " 人", () => SetCapacity(value), 34);
                }
            }
            var lifecycle = UI.Row(controls, "SDK Actions");
            UI.Height(lifecycle, 36);
            restartButton = UI.Button(lifecycle, "重启", () => Run(Restart()));
            stopButton = UI.Button(lifecycle, "停止", () => Run(StopRuntime()));
            unloadButton = UI.Button(lifecycle, "卸载", () => Run(Unload()));
            photoButton = UI.Button(controls, "拍照（精确同帧）", () => Run(TakeExactPhoto()));
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            campaignButton = UI.Button(controls, "一键测试", () => {
                if (campaignRunning) { campaignCancelled = true; status = "正在结束当前测试步骤…"; }
                else Run(RunThermalCampaign());
            });
#endif

            var logs = UI.Panel(leftUi, "Diagnostics", UI.Field);
            Place(logs, Vector2.zero, new Vector2(1, .43f), new Vector2(12, 12), new Vector2(-12, -4));
            var bar = UI.Row(logs, "Log Header");
            Place(bar, new Vector2(0, 1), Vector2.one, new Vector2(10, -47), new Vector2(-10, -9));
            UI.Label(bar, "实时状态 / 日志", 19);
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            UI.Button(bar, "一键采集日志", () => {
                CollectDiagnosticLogs();
                RefreshUiText();
                Canvas.ForceUpdateCanvases();
                logScroll.verticalNormalizedPosition = 1;
            }, 35);
#endif
            var content = UI.Scroll(logs, "Log Scroll", 10);
            logScroll = content.parent.parent.GetComponent<ScrollRect>();
            Place((RectTransform)logScroll.transform, Vector2.zero, Vector2.one, new Vector2(10, 54), new Vector2(-10, -55));
            exportText = UI.Label(content, "", 15);
            exportText.color = UI.Accent;
            logText = UI.Label(content, "", 16);
            logText.color = UI.Muted;
            logText.alignment = TextAnchor.UpperLeft;
            var divider = UI.Panel(logs, "Temperature Divider", UI.Border);
            divider.GetComponent<Image>().raycastTarget = false;
            Place(divider, Vector2.zero, new Vector2(1, 0), new Vector2(10, 47), new Vector2(-10, 48));
            deviceTemperatures = UI.Label(logs, "CPU —   GPU —   NPU —   电池 —", 15);
            deviceTemperatures.name = "Device Temperatures";
            deviceTemperatures.resizeTextForBestFit = true;
            deviceTemperatures.resizeTextMinSize = 10;
            deviceTemperatures.resizeTextMaxSize = 15;
            deviceTemperatures.color = UI.Muted;
            Place(deviceTemperatures.rectTransform, Vector2.zero, new Vector2(1, 0), new Vector2(10, 5), new Vector2(-10, 44));
        }

        private void BuildPreviewUi()
        {
            var bar = UI.Row(rightUi, "Preview Toolbar");
            Place(bar, new Vector2(0, 1), Vector2.one, new Vector2(12, -54), new Vector2(-12, -11));
            UI.Label(bar, "实时预览", 22);
            skeletonButton = UI.Button(bar, "关闭骨骼", () => { showSkeleton = !showSkeleton; RefreshPresentationMode(); });
            cropButton = UI.Button(bar, "填满裁切", () => { fullscreenCrop = !fullscreenCrop; });
            fullscreenButton = UI.Button(bar, "全屏预览", () => SetFullscreenPreview(!fullscreenPreview));
            previewViewport = UI.Panel(rightUi, "Preview Viewport", Color.black);
            Place(previewViewport, Vector2.zero, Vector2.one, new Vector2(12, 51), new Vector2(-12, -65));
            previewViewport.gameObject.AddComponent<RectMask2D>();
            sourceImage = UI.Rect(previewViewport, "Live Source Texture").gameObject.AddComponent<RawImage>();
            sourceImage.raycastTarget = false;
            sceneSkeletonImage = UI.Rect(previewViewport, "Scene Skeleton Camera Texture").gameObject.AddComponent<RawImage>();
            sceneSkeletonImage.raycastTarget = false;
            regionPlane = UI.Rect(previewViewport, "Recognition Regions");
            for (int i = 0; i < regionViews.Length; i++) regionViews[i] = CreateRegion(regionPlane, i);
            previewEmpty = UI.Label(previewViewport, "等待当前输入", 20);
            previewEmpty.alignment = TextAnchor.MiddleCenter;
            UI.Fill(previewEmpty.rectTransform);
            previewInfo = UI.Label(rightUi, "", 17);
            previewInfo.color = UI.Muted;
            Place(previewInfo.rectTransform, Vector2.zero, new Vector2(1, 0), new Vector2(12, 6), new Vector2(-12, 46));
        }

        private static RectTransform CreateRegion(Transform parent, int index)
        {
            var rect = UI.Rect(parent, "Region " + (index + 1));
            Color color = RuntimePrefabRedrawRig.GetSlotColor(index);
            RegionEdge(rect, "Top", new Vector2(0, 1), Vector2.one, new Vector2(0, -1.5f), Vector2.zero, color);
            RegionEdge(rect, "Bottom", Vector2.zero, new Vector2(1, 0), Vector2.zero, new Vector2(0, 1.5f), color);
            RegionEdge(rect, "Left", Vector2.zero, new Vector2(0, 1), Vector2.zero, new Vector2(1.5f, 0), color);
            RegionEdge(rect, "Right", new Vector2(1, 0), Vector2.one, new Vector2(-1.5f, 0), Vector2.zero, color);
            var label = UI.Label(rect, "区域 " + (index + 1), 15);
            label.color = color;
            Place(label.rectTransform, new Vector2(0, 1), new Vector2(1, 1), new Vector2(6, -26), new Vector2(-6, -3));
            rect.gameObject.SetActive(false);
            return rect;
        }

        private static void RegionEdge(Transform parent, string name, Vector2 from, Vector2 to, Vector2 min, Vector2 max, Color color)
        {
            var edge = UI.Panel(parent, name, color);
            edge.GetComponent<Image>().raycastTarget = false;
            Place(edge, from, to, min, max);
        }

        private static void Place(RectTransform rect, Vector2 from, Vector2 to, Vector2 min, Vector2 max)
        {
            rect.anchorMin = from;
            rect.anchorMax = to;
            rect.offsetMin = min;
            rect.offsetMax = max;
        }

        private void SetPreviewLayout(bool fullscreen)
        {
            if (lastFullscreen == fullscreen) return;
            lastFullscreen = fullscreen;
            headerUi.gameObject.SetActive(!fullscreen);
            leftUi.gameObject.SetActive(!fullscreen);
            Vector2 min = fullscreen ? Vector2.zero : new Vector2(.34f, 0);
            Place(rightUi, min, Vector2.one, fullscreen ? Vector2.zero : new Vector2(8, 16), fullscreen ? Vector2.zero : new Vector2(-18, -78));
            UI.Caption(fullscreenButton, fullscreen ? "退出全屏" : "全屏预览");
            cropButton.gameObject.SetActive(fullscreen);
            lastImageBox = default;
        }

        private void RenderAcceptanceUi()
        {
            if (startup == null) return;
            BuildAcceptanceUi();
            Vector2 screen = new Vector2(Screen.width, Screen.height);
            Rect safe = Screen.safeArea;
            if (screen.x > 0 && screen.y > 0 && safe.width > 0 && safe.height > 0 &&
                (screen != lastScreen || safe != lastSafe)) {
                lastScreen = screen;
                lastSafe = safe;
                Place(safeUi, new Vector2(safe.xMin / screen.x, safe.yMin / screen.y),
                    new Vector2(safe.xMax / screen.x, safe.yMax / screen.y), Vector2.zero, Vector2.zero);
            }
            if (fullscreenPreview && UnityEngine.Input.GetKeyDown(KeyCode.Escape)) SetFullscreenPreview(false);
            SetPreviewLayout(fullscreenPreview);
            RefreshPreviewUi();
            RecordDrawnPreview(live, default);
            if (Time.unscaledTime < nextUiTextAt) return;
            nextUiTextAt = Time.unscaledTime + .15f;
            RefreshUiText();
        }

        private void RefreshPreviewUi()
        {
            int width = live != null ? live.width : 1280;
            int height = live != null ? live.height : 720;
            sceneSkeleton.Refresh(liveSkeleton, width, height);
            sourceImage.texture = live;
            sourceImage.enabled = live != null;
            sceneSkeletonImage.texture = sceneSkeleton.Output;
            sceneSkeletonImage.enabled = sceneSkeleton.Output != null;
            previewEmpty.enabled = live == null && sceneSkeleton.Output == null;
            sourceImage.material = ResolveUiPreviewMaterial();
            Rect viewport = previewViewport.rect;
            bool crop = fullscreenPreview && fullscreenCrop;
            if (viewport.width > 0 && viewport.height > 0 &&
                (lastImageBox != viewport || lastPreviewWidth != width || lastPreviewHeight != height || lastPreviewCrop != crop)) {
                lastImageBox = viewport;
                lastPreviewWidth = width;
                lastPreviewHeight = height;
                lastPreviewCrop = crop;
                Rect image = RuntimePrefabLiveSkeleton.Fit(new Rect(0, 0, viewport.width, viewport.height), width, height, crop);
                Vector2 visualSize = crop ? viewport.size : image.size;
                Center(sourceImage.rectTransform, visualSize);
                Center(sceneSkeletonImage.rectTransform, visualSize);
                Center(regionPlane, image.size);
                var uv = new Rect((1 - visualSize.x / image.width) * .5f, (1 - visualSize.y / image.height) * .5f,
                    visualSize.x / image.width, visualSize.y / image.height);
                sourceImage.uvRect = uv;
                sceneSkeletonImage.uvRect = uv;
            }
            int count = recognition != null && recognition.UseRegions ? recognition.Regions.Length : 0;
            for (int i = 0; i < regionViews.Length; i++) {
                bool visible = i < count;
                if (regionViews[i].gameObject.activeSelf != visible) regionViews[i].gameObject.SetActive(visible);
                if (!visible || displayedRegions[i] == recognition.Regions[i]) continue;
                Rect region = recognition.Regions[i];
                displayedRegions[i] = region;
                Place(regionViews[i], new Vector2(region.xMin, 1f - region.yMax), new Vector2(region.xMax, 1f - region.yMin),
                    Vector2.zero, Vector2.zero);
            }
        }

        private static void Center(RectTransform rect, Vector2 size)
        {
            rect.anchorMin = rect.anchorMax = new Vector2(.5f, .5f);
            rect.sizeDelta = size;
            rect.anchoredPosition = Vector2.zero;
        }

        private Material ResolveUiPreviewMaterial()
        {
            if (QualitySettings.activeColorSpace != ColorSpace.Linear || live != input.Texture ||
                Application.platform != RuntimePlatform.Android || input.TimestampKind != FrameTimestampKind.LocalDecode ||
                input.ColorSpace == FrameColorSpace.Linear || !(live is RenderTexture target) || target.sRGB) return null;
            if (previewColorMaterial == null) {
                var shader = Resources.Load<Shader>("HumanVisionRuntimePreviewColor");
                if (shader != null && shader.isSupported)
                    previewColorMaterial = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            }
            return previewColorMaterial;
        }

        private void RefreshUiText()
        {
            bool available = !operating;
            inputSummary.text = startup.SourceKind == InputKind.WebCamera ? "当前输入  本地相机" :
                startup.SourceKind == InputKind.Rtsp ? "当前输入  RTSP" : "当前输入  本地视频";
            previewInfo.text = inputLine;
            previewEmpty.text = startup.PreviewEnabled ? "等待当前输入" : "预览已关闭，识别可继续";
            UI.Caption(skeletonButton, showSkeleton ? "关闭骨骼" : "开启骨骼");
            UI.Caption(cropButton, fullscreenCrop ? "恢复等比" : "填满裁切");
            connectionButton.interactable = available;
            restartButton.interactable = stopButton.interactable = unloadButton.interactable = available && view != null;
            photoButton.interactable = available && CanTakePhoto;
            for (int i = 0; i < modeButtons.Length; i++) {
                var mode = UiModes[i];
                modeButtons[i].interactable = available && CanChooseUiMode(mode);
                UI.Select(modeButtons[i], startup.AccelerationMode == mode);
            }
            for (int i = 0; i < qualityButtons.Length; i++) {
                var quality = UiQualities[i];
                bool supported = UiQualityAvailable(startup.AccelerationMode, quality);
                qualityButtons[i].interactable = available && supported;
                qualityButtons[i].gameObject.SetActive(quality != ModelInputQuality.UltraLow || supported);
                UI.Select(qualityButtons[i], startup.InputQuality == quality);
            }
            for (int i = 0; i < capacityButtons.Length; i++) {
                capacityButtons[i].interactable = available;
                UI.Select(capacityButtons[i], i + 1 == maxBodies);
            }
            string previewError = view?.Sdk.PreviewError;
            string sdkError = view?.Sdk.LastError;
            string output = telemetry.PerformanceLine + "\n" +
                $"{liveSkeleton.VisibleBodies} 人 · {visibleJoints} 个点 · {visibleBones} 条线\n" +
                status + "\n" + sdkLine + "\n" + inputLine + "\n" + telemetry.ReadLine + "\n" + telemetry.StageLine;
            if (!string.IsNullOrEmpty(previewError)) output += "\n原图预览：" + previewError;
            if (!string.IsNullOrEmpty(sdkError) && sdkError != status) output += "\n" + sdkError;
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            output += "\n" + BackendAvailability;
            logText.text = diagnostics.Redact(output);
            exportText.text = diagnosticExportStatus + (string.IsNullOrEmpty(diagnosticExportPath) ? "" : "\n" + diagnosticExportPath);
            deviceTemperatures.text = diagnostics.TemperatureLine;
            connectionButton.interactable = available && !automatedFixture && interactive != null;
            settingsButton.interactable = connectionButton.interactable;
            campaignButton.interactable = campaignRunning ? !campaignCancelled :
                available && startup.SourceKind == InputKind.Rtsp && (automatedFixture ||
                startup.InputQuality == ModelInputQuality.Low && HasManualQuality(startup.AccelerationMode, ModelInputQuality.Low));
            UI.Caption(campaignButton, campaignRunning ? "取消一键测试" : "一键测试");
            RefreshConnectionUi();
            RefreshAdvancedUi();
#else
            logText.text = output;
#endif
        }

        private bool UiQualityAvailable(HumanVisionAccelerationMode mode, ModelInputQuality quality)
        {
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            return !automatedFixture && HasManualQuality(mode, quality);
#else
            return quality != ModelInputQuality.UltraLow;
#endif
        }

        private bool CanChooseUiMode(HumanVisionAccelerationMode mode)
        {
            for (int i = 0; i < UiQualities.Length; i++)
                if (UiQualityAvailable(mode, UiQualities[i])) return true;
            return false;
        }

        private void SelectBackendFromUi(HumanVisionAccelerationMode mode)
        {
            if (UiQualityAvailable(mode, startup.InputQuality)) {
                SelectCompute(mode, startup.InputQuality);
                return;
            }
            for (int i = 0; i < UiQualities.Length; i++) {
                if (!UiQualityAvailable(mode, UiQualities[i])) continue;
                SelectCompute(mode, UiQualities[i]);
                return;
            }
        }

        private void DisposeAcceptanceUi()
        {
            sceneSkeleton.Dispose();
            if (UI.Font == uiFont) UI.Font = null;
        }
    }
}
