using System;
using HumanVision;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using UI = HumanVisionAcceptance.RuntimeAcceptanceUi;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        [Header("场景界面")]
        [SerializeField] private Canvas demoCanvas;
        [SerializeField] private EventSystem eventSystem;
        [SerializeField] private RectTransform safeUi;
        [SerializeField] private RectTransform headerUi;
        [SerializeField] private RectTransform leftUi;
        [SerializeField] private RectTransform rightUi;

        [Header("输入与识别")]
        [SerializeField] private Button connectionButton;
        [SerializeField] private Text inputSummary;
        [SerializeField] private UiChoice[] modeButtons = new UiChoice[3];
        [SerializeField] private UiChoice[] qualityButtons = new UiChoice[4];
        [SerializeField] private UiChoice[] capacityButtons = new UiChoice[MaxCapacity];
        [SerializeField] private Button restartButton;
        [SerializeField] private Button stopButton;
        [SerializeField] private Button unloadButton;
        [SerializeField] private Button photoButton;
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
        [SerializeField] private Button settingsButton;
        [SerializeField] private Button campaignButton;
        [SerializeField] private Text campaignCaption;
#endif

        [Header("实时预览")]
        [SerializeField] private RectTransform previewViewport;
        [SerializeField] private RawImage sourceImage;
        [SerializeField] private RawImage sceneSkeletonImage;
        [SerializeField] private Text previewInfo;
        [SerializeField] private Text previewEmpty;
        [SerializeField] private RectTransform regionPlane;
        [SerializeField] private RectTransform[] regionViews = new RectTransform[MaxCapacity];
        [SerializeField] private Shader previewColorShader;
        [SerializeField] private Button skeletonButton;
        [SerializeField] private Text skeletonCaption;
        [SerializeField] private Button fullscreenButton;
        [SerializeField] private Text fullscreenCaption;
        [SerializeField] private Button cropButton;
        [SerializeField] private Text cropCaption;

        [Header("日志与温度")]
        [SerializeField] private Text logText;
        [SerializeField] private Text deviceTemperatures;
#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
        [SerializeField] private Button captureLogsButton;
        [SerializeField] private Text exportText;
        [SerializeField] private ScrollRect logScroll;
#endif

        [Serializable]
        private sealed class UiChoice
        {
            [SerializeField] private Button button;
            [SerializeField] private Text caption;

            internal UiChoice(Button button, Text caption)
            {
                this.button = button;
                this.caption = caption;
            }

            internal bool IsAssigned => button != null && button.targetGraphic != null && caption != null;

            internal void Bind(UnityAction click)
            {
                button.onClick.AddListener(click);
            }

            internal void Show(bool visible)
            {
                if (button.gameObject.activeSelf != visible)
                    button.gameObject.SetActive(visible);
            }

            internal void Refresh(bool selected, bool interactable)
            {
                button.interactable = interactable;
                button.targetGraphic.color = selected ? UI.Accent : UI.Border;
                caption.color = selected ? UI.Background : UI.Ink;
            }
        }

        private static readonly HumanVisionAccelerationMode[] UiModes =
        {
            HumanVisionAccelerationMode.Cpu, HumanVisionAccelerationMode.Graphics, HumanVisionAccelerationMode.Neural
        };

        private static readonly ModelInputQuality[] UiQualities =
        {
            ModelInputQuality.Low, ModelInputQuality.Medium, ModelInputQuality.High, ModelInputQuality.UltraLow
        };

        private readonly Rect[] displayedRegions = new Rect[MaxCapacity];
        private Vector2 normalPreviewAnchorMin, normalPreviewAnchorMax;
        private Vector2 normalPreviewOffsetMin, normalPreviewOffsetMax;
        private Vector2 lastScreen;
        private Rect lastSafeArea, lastPreviewRect;
        private bool acceptanceUiBound, lastFullscreen, lastPreviewCrop;
        private int lastSourceWidth, lastSourceHeight;
        private float nextUiTextAt;

        private bool BindAcceptanceUi()
        {
            if (acceptanceUiBound)
                return true;

            if (!CheckAcceptanceUiReferences() || !BindSceneSkeleton())
                return false;

#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            if (!BindConnectionUi() || !BindAdvancedUi())
                return false;

            connectionButton.onClick.AddListener(OpenConnectionUi);
            settingsButton.onClick.AddListener(ShowAdvancedUi);
            captureLogsButton.onClick.AddListener(OnCaptureLogs);
            campaignButton.onClick.AddListener(OnCampaign);
#else
            connectionButton.onClick.AddListener(OnConnect);
#endif
            restartButton.onClick.AddListener(OnRestart);
            stopButton.onClick.AddListener(OnStop);
            unloadButton.onClick.AddListener(OnUnload);
            photoButton.onClick.AddListener(OnTakePhoto);
            skeletonButton.onClick.AddListener(OnToggleSkeleton);
            cropButton.onClick.AddListener(OnToggleCrop);
            fullscreenButton.onClick.AddListener(OnToggleFullscreen);
            BindRecognitionButtons();

            normalPreviewAnchorMin = rightUi.anchorMin;
            normalPreviewAnchorMax = rightUi.anchorMax;
            normalPreviewOffsetMin = rightUi.offsetMin;
            normalPreviewOffsetMax = rightUi.offsetMax;
            cropButton.gameObject.SetActive(false);
            acceptanceUiBound = true;
            return true;
        }

        private bool CheckAcceptanceUiReferences()
        {
            if (demoCanvas == null || eventSystem == null || safeUi == null ||
                headerUi == null || leftUi == null || rightUi == null)
            {
                Debug.LogError("验收 Demo 的场景界面引用不完整，请检查 Canvas、EventSystem 和页面容器。", this);
                return false;
            }

            if (connectionButton == null || inputSummary == null || restartButton == null ||
                stopButton == null || unloadButton == null || photoButton == null ||
                !CheckChoices(modeButtons, UiModes.Length) || !CheckChoices(qualityButtons, UiQualities.Length) ||
                !CheckChoices(capacityButtons, MaxCapacity))
            {
                Debug.LogError("验收 Demo 的输入与识别按钮引用不完整，请检查 Inspector。", this);
                return false;
            }

            if (previewViewport == null || sourceImage == null || sceneSkeletonImage == null ||
                previewInfo == null || previewEmpty == null || regionPlane == null ||
                regionViews == null || regionViews.Length != MaxCapacity ||
                skeletonButton == null || skeletonCaption == null || fullscreenButton == null ||
                fullscreenCaption == null || cropButton == null || cropCaption == null)
            {
                Debug.LogError("验收 Demo 的实时预览引用不完整，请检查原图、骨骼叠层和预览按钮。", this);
                return false;
            }

            for (int i = 0; i < regionViews.Length; i++)
            {
                if (regionViews[i] != null)
                    continue;

                Debug.LogError($"验收 Demo 的区域 {i + 1} 没有场景引用。", this);
                return false;
            }

            if (logText == null || deviceTemperatures == null)
            {
                Debug.LogError("验收 Demo 的日志或温度文本没有场景引用。", this);
                return false;
            }

#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            if (settingsButton == null || campaignButton == null || campaignCaption == null ||
                captureLogsButton == null || exportText == null || logScroll == null)
            {
                Debug.LogError("验收 Demo 的诊断功能引用不完整，请检查设置、采集和导出控件。", this);
                return false;
            }
#endif
            return true;
        }

        private static bool CheckChoices(UiChoice[] choices, int expectedCount)
        {
            if (choices == null || choices.Length != expectedCount)
                return false;

            foreach (var choice in choices)
            {
                if (choice == null || !choice.IsAssigned)
                    return false;
            }
            return true;
        }

        private void BindRecognitionButtons()
        {
            for (int i = 0; i < modeButtons.Length; i++)
            {
                var mode = UiModes[i];
                modeButtons[i].Bind(() => SelectBackendFromUi(mode));
            }
            for (int i = 0; i < qualityButtons.Length; i++)
            {
                var quality = UiQualities[i];
                qualityButtons[i].Bind(() => SelectCompute(startup.AccelerationMode, quality));
            }
            for (int i = 0; i < capacityButtons.Length; i++)
            {
                int capacity = i + 1;
                capacityButtons[i].Bind(() => SetCapacity(capacity));
            }
        }

        private void OnConnect() => Run(Connect());
        private void OnRestart() => Run(Restart());
        private void OnStop() => Run(StopRuntime());
        private void OnUnload() => Run(Unload());
        private void OnTakePhoto() => Run(TakeExactPhoto());
        private void OnToggleCrop() => fullscreenCrop = !fullscreenCrop;
        private void OnToggleFullscreen() => SetFullscreenPreview(!fullscreenPreview);

        private void OnToggleSkeleton()
        {
            showSkeleton = !showSkeleton;
            RefreshPresentationMode();
        }

#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
        private void OnCaptureLogs()
        {
            CollectDiagnosticLogs();
            RefreshUiText();
            Canvas.ForceUpdateCanvases();
            logScroll.verticalNormalizedPosition = 1f;
        }

        private void OnCampaign()
        {
            if (campaignRunning)
            {
                campaignCancelled = true;
                status = "正在结束当前测试步骤…";
                return;
            }

            Run(RunThermalCampaign());
        }
#endif

        private void RenderAcceptanceUi()
        {
            if (!acceptanceUiBound || startup == null)
                return;

            RefreshSafeArea();
            if (fullscreenPreview && UnityEngine.Input.GetKeyDown(KeyCode.Escape))
                SetFullscreenPreview(false);

            RefreshFullscreenLayout();
            RefreshPreviewUi();
            RecordDrawnPreview(live, default);

            if (Time.unscaledTime >= nextUiTextAt)
            {
                nextUiTextAt = Time.unscaledTime + .15f;
                RefreshUiText();
            }
        }

        private void RefreshSafeArea()
        {
            var screen = new Vector2(Screen.width, Screen.height);
            Rect area = Screen.safeArea;
            if (screen.x <= 0 || screen.y <= 0 || area.width <= 0 || area.height <= 0 ||
                (screen == lastScreen && area == lastSafeArea))
                return;

            lastScreen = screen;
            lastSafeArea = area;
            Place(safeUi, new Vector2(area.xMin / screen.x, area.yMin / screen.y),
                new Vector2(area.xMax / screen.x, area.yMax / screen.y), Vector2.zero, Vector2.zero);
        }

        private void RefreshFullscreenLayout()
        {
            if (lastFullscreen == fullscreenPreview)
                return;

            lastFullscreen = fullscreenPreview;
            headerUi.gameObject.SetActive(!fullscreenPreview);
            leftUi.gameObject.SetActive(!fullscreenPreview);
            cropButton.gameObject.SetActive(fullscreenPreview);
            if (fullscreenPreview)
                Place(rightUi, Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);
            else
                Place(rightUi, normalPreviewAnchorMin, normalPreviewAnchorMax, normalPreviewOffsetMin, normalPreviewOffsetMax);
            fullscreenCaption.text = fullscreenPreview ? "退出全屏" : "全屏预览";
            lastPreviewRect = default;
        }

        private void RefreshPreviewUi()
        {
            int width = live != null ? live.width : 1280;
            int height = live != null ? live.height : 720;
            sceneSkeleton.Refresh(liveSkeleton, width, height);

            Texture skeletonTexture = sceneSkeleton.Output;
            sourceImage.texture = live;
            sourceImage.enabled = live != null;
            sceneSkeletonImage.texture = skeletonTexture;
            sceneSkeletonImage.enabled = skeletonTexture != null;
            previewEmpty.enabled = live == null && skeletonTexture == null;
            sourceImage.material = ResolveUiPreviewMaterial();
            RefreshPreviewGeometry(width, height);
            RefreshRegionViews();
        }

        private void RefreshPreviewGeometry(int width, int height)
        {
            Rect viewport = previewViewport.rect;
            bool crop = fullscreenPreview && fullscreenCrop;
            if (viewport.width <= 0 || viewport.height <= 0)
                return;
            if (lastPreviewRect == viewport && lastSourceWidth == width &&
                lastSourceHeight == height && lastPreviewCrop == crop)
                return;

            lastPreviewRect = viewport;
            lastSourceWidth = width;
            lastSourceHeight = height;
            lastPreviewCrop = crop;
            Rect image = RuntimePrefabLiveSkeleton.Fit(new Rect(0, 0, viewport.width, viewport.height), width, height, crop);
            Vector2 size = crop ? viewport.size : image.size;
            Center(sourceImage.rectTransform, size);
            Center(sceneSkeletonImage.rectTransform, size);
            Center(regionPlane, image.size);
            var uv = new Rect((1 - size.x / image.width) * .5f, (1 - size.y / image.height) * .5f,
                size.x / image.width, size.y / image.height);
            sourceImage.uvRect = uv;
            sceneSkeletonImage.uvRect = uv;
        }

        private void RefreshRegionViews()
        {
            int count = recognition != null && recognition.UseRegions ? recognition.Regions.Length : 0;
            for (int i = 0; i < regionViews.Length; i++)
            {
                bool visible = i < count;
                if (regionViews[i].gameObject.activeSelf != visible)
                    regionViews[i].gameObject.SetActive(visible);
                if (!visible || displayedRegions[i] == recognition.Regions[i])
                    continue;

                Rect region = recognition.Regions[i];
                displayedRegions[i] = region;
                Place(regionViews[i], new Vector2(region.xMin, 1f - region.yMax),
                    new Vector2(region.xMax, 1f - region.yMin), Vector2.zero, Vector2.zero);
            }
        }

        private static void Place(RectTransform rect, Vector2 from, Vector2 to, Vector2 min, Vector2 max)
        {
            rect.anchorMin = from;
            rect.anchorMax = to;
            rect.offsetMin = min;
            rect.offsetMax = max;
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
                input.ColorSpace == FrameColorSpace.Linear || !(live is RenderTexture target) || target.sRGB)
                return null;

            if (previewColorMaterial == null && previewColorShader != null && previewColorShader.isSupported)
                previewColorMaterial = new Material(previewColorShader) { hideFlags = HideFlags.HideAndDontSave };
            return previewColorMaterial;
        }

        private void RefreshUiText()
        {
            if (startup == null)
                return;

            inputSummary.text = "当前输入  " + GetInputDisplayName();
            previewInfo.text = inputLine;
            previewEmpty.text = startup.PreviewEnabled ? "等待当前输入" : "预览已关闭，识别可继续";
            skeletonCaption.text = showSkeleton ? "关闭骨骼" : "开启骨骼";
            cropCaption.text = fullscreenCrop ? "恢复等比" : "填满裁切";
            connectionButton.interactable = !operating;
            restartButton.interactable = stopButton.interactable = unloadButton.interactable = !operating && view != null;
            photoButton.interactable = !operating && CanTakePhoto;
            RefreshRecognitionButtons();
            RefreshLogText();

#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            connectionButton.interactable = !operating && !automatedFixture && interactive != null;
            settingsButton.interactable = connectionButton.interactable;
            campaignButton.interactable = campaignRunning ? !campaignCancelled :
                !operating && startup.SourceKind == InputKind.Rtsp && (automatedFixture ||
                    startup.InputQuality == ModelInputQuality.Low && HasManualQuality(startup.AccelerationMode, ModelInputQuality.Low));
            campaignCaption.text = campaignRunning ? "取消一键测试" : "一键测试";
            RefreshConnectionUi();
            RefreshAdvancedUi();
#endif
        }

        private string GetInputDisplayName()
        {
            switch (startup.SourceKind)
            {
                case InputKind.WebCamera: return "本地相机";
                case InputKind.Rtsp: return "RTSP";
                default: return "本地视频";
            }
        }

        private void RefreshRecognitionButtons()
        {
            for (int i = 0; i < modeButtons.Length; i++)
            {
                var mode = UiModes[i];
                modeButtons[i].Refresh(startup.AccelerationMode == mode, !operating && CanChooseUiMode(mode));
            }
            for (int i = 0; i < qualityButtons.Length; i++)
            {
                var quality = UiQualities[i];
                bool supported = UiQualityAvailable(startup.AccelerationMode, quality);
                qualityButtons[i].Show(quality != ModelInputQuality.UltraLow || supported);
                qualityButtons[i].Refresh(startup.InputQuality == quality, !operating && supported);
            }
            for (int i = 0; i < capacityButtons.Length; i++)
                capacityButtons[i].Refresh(i + 1 == maxBodies, !operating);
        }

        private void RefreshLogText()
        {
            string output = telemetry.PerformanceLine + "\n" +
                $"{liveSkeleton.VisibleBodies} 人 · {visibleJoints} 个点 · {visibleBones} 条线\n" +
                status + "\n" + sdkLine + "\n" + inputLine + "\n" + telemetry.ReadLine + "\n" + telemetry.StageLine;
            string previewError = view?.Sdk.PreviewError;
            string sdkError = view?.Sdk.LastError;
            if (!string.IsNullOrEmpty(previewError))
                output += "\n原图预览：" + previewError;
            if (!string.IsNullOrEmpty(sdkError) && sdkError != status)
                output += "\n" + sdkError;

#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
            logText.text = diagnostics.Redact(output + "\n" + BackendAvailability);
            exportText.text = diagnosticExportStatus + (string.IsNullOrEmpty(diagnosticExportPath) ? "" : "\n" + diagnosticExportPath);
            deviceTemperatures.text = diagnostics.TemperatureLine;
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
            foreach (var quality in UiQualities)
            {
                if (UiQualityAvailable(mode, quality))
                    return true;
            }
            return false;
        }

        private void SelectBackendFromUi(HumanVisionAccelerationMode mode)
        {
            if (UiQualityAvailable(mode, startup.InputQuality))
            {
                SelectCompute(mode, startup.InputQuality);
                return;
            }

            foreach (var quality in UiQualities)
            {
                if (!UiQualityAvailable(mode, quality))
                    continue;
                SelectCompute(mode, quality);
                return;
            }
        }

        private void DisposeAcceptanceUi()
        {
            sceneSkeleton?.Dispose();
        }
    }
}
