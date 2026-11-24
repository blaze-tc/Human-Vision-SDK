#if UNITY_EDITOR
using System;
using HumanVision;
using UnityEditor;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using UI = HumanVisionAcceptance.RuntimeAcceptanceUi;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        internal void BakeAcceptanceUi(Font font = null)
        {
            if (Application.isPlaying)
                throw new InvalidOperationException("请先退出 Play 模式再搭建验收界面。");
            if (demoCanvas != null)
                return;

            Font previousFont = UI.Font;
            UI.Font = font;
            try
            {
                BuildAcceptanceCanvas();
                BuildTopBar();
                BuildLeftControls();
                BuildPreviewUi();

                var dialogs = UI.Rect(safeUi, "Dialogs");
                UI.Fill(dialogs);
                BuildConnectionUi(dialogs);
                BuildAdvancedUi(dialogs);
            }
            finally
            {
                UI.Font = previousFont;
            }
        }

        private void BuildAcceptanceCanvas()
        {
            var canvasRoot = UI.Rect(transform, "Runtime Acceptance UGUI");
            demoCanvas = canvasRoot.gameObject.AddComponent<Canvas>();
            demoCanvas.renderMode = RenderMode.ScreenSpaceOverlay;
            demoCanvas.sortingOrder = 20;

            var scaler = canvasRoot.gameObject.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1440, 866);
            scaler.matchWidthOrHeight = .50f;
            canvasRoot.gameObject.AddComponent<GraphicRaycaster>();

            foreach (var sceneRoot in gameObject.scene.GetRootGameObjects())
            {
                eventSystem = sceneRoot.GetComponentInChildren<EventSystem>(true);
                if (eventSystem != null)
                    break;
            }
            if (eventSystem == null)
            {
                var node = new GameObject("Runtime Acceptance EventSystem", typeof(EventSystem), typeof(StandaloneInputModule));
                node.transform.SetParent(transform, false);
                eventSystem = node.GetComponent<EventSystem>();
            }

            UI.Fill(UI.Panel(canvasRoot, "Backdrop", UI.Background));
            safeUi = UI.Rect(canvasRoot, "Safe Area");
            UI.Fill(safeUi);
        }

        private void BuildTopBar()
        {
            headerUi = UI.Row(safeUi, "Header");
            UI.Height(UI.Label(headerUi, "Human Vision   /   实时识别", 29), 54);
            var version = UI.Label(headerUi, "SDK  " + HumanVisionSdk.Version, 17);
            version.color = UI.Muted;
            version.alignment = TextAnchor.MiddleRight;
            Place(headerUi, new Vector2(0, 1), Vector2.one, new Vector2(18, -72), new Vector2(-18, -12));
        }

        private void BuildLeftControls()
        {
            leftUi = UI.Panel(safeUi, "Controls and Diagnostics", UI.Surface);
            Place(leftUi, Vector2.zero, new Vector2(.34f, 1), new Vector2(18, 16), new Vector2(-8, -78));

            var controls = UI.Scroll(leftUi, "Control Scroll");
            Place((RectTransform)controls.parent.parent, new Vector2(0, .43f), Vector2.one,
                new Vector2(14, 8), new Vector2(-14, -14));
            connectionButton = UI.Button(controls, "输入连接", null, 44);
            UI.Select(connectionButton, true);
            settingsButton = UI.Button(controls, "识别与显示设置", null);
            inputSummary = UI.Label(controls, "未连接", 17);
            inputSummary.name = "Current Input";
            inputSummary.color = UI.Muted;
            UI.Height(inputSummary, 28);

            BuildRecognitionButtons(controls);
            var lifecycle = UI.Row(controls, "SDK Actions");
            UI.Height(lifecycle, 36);
            restartButton = UI.Button(lifecycle, "重启", null);
            stopButton = UI.Button(lifecycle, "停止", null);
            unloadButton = UI.Button(lifecycle, "卸载", null);
            photoButton = UI.Button(controls, "拍照（精确同帧）", null);
            campaignButton = UI.Button(controls, "一键测试", null);
            campaignCaption = campaignButton.GetComponentInChildren<Text>();

            BuildDiagnostics();
        }

        private void BuildRecognitionButtons(RectTransform controls)
        {
            UI.Label(controls, "推理后端", 19);
            var backends = UI.Row(controls, "Backend Options");
            UI.Height(backends, 36);
            string[] backendNames = { "CPU", "GPU", "NPU" };
            modeButtons = new UiChoice[backendNames.Length];
            for (int i = 0; i < backendNames.Length; i++)
                modeButtons[i] = BuildChoice(backends, backendNames[i], 36);

            UI.Label(controls, "模型等级", 19);
            var qualities = UI.Row(controls, "Model Quality Options");
            UI.Height(qualities, 36);
            string[] qualityNames = { "低", "中", "高", "超低" };
            qualityButtons = new UiChoice[qualityNames.Length];
            for (int i = 0; i < qualityNames.Length; i++)
                qualityButtons[i] = BuildChoice(qualities, qualityNames[i], 36);

            UI.Label(controls, "人数上限", 19);
            capacityButtons = new UiChoice[MaxCapacity];
            RectTransform row = null;
            for (int i = 0; i < capacityButtons.Length; i++)
            {
                if (i % 4 == 0)
                {
                    row = UI.Row(controls, "Body Capacity " + (i / 4 + 1));
                    UI.Height(row, 34);
                }
                capacityButtons[i] = BuildChoice(row, (i + 1) + " 人", 34);
            }
        }

        private static UiChoice BuildChoice(Transform parent, string caption, float height)
        {
            var button = UI.Button(parent, caption, null, height);
            return new UiChoice(button, button.GetComponentInChildren<Text>());
        }

        private void BuildDiagnostics()
        {
            var logs = UI.Panel(leftUi, "Diagnostics", UI.Field);
            Place(logs, Vector2.zero, new Vector2(1, .43f), new Vector2(12, 12), new Vector2(-12, -4));
            var bar = UI.Row(logs, "Log Header");
            Place(bar, new Vector2(0, 1), Vector2.one, new Vector2(10, -47), new Vector2(-10, -9));
            UI.Label(bar, "实时状态 / 日志", 19);
            captureLogsButton = UI.Button(bar, "一键采集日志", null, 35);

            var content = UI.Scroll(logs, "Log Scroll", 10);
            logScroll = content.parent.parent.GetComponent<ScrollRect>();
            Place((RectTransform)logScroll.transform, Vector2.zero, Vector2.one, new Vector2(10, 54), new Vector2(-10, -55));
            exportText = UI.Label(content, "", 15);
            exportText.name = "Exported Log Path";
            exportText.color = UI.Accent;
            logText = UI.Label(content, "", 16);
            logText.name = "Diagnostics Text";
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
            rightUi = UI.Panel(safeUi, "Live Preview", UI.Surface);
            Place(rightUi, new Vector2(.34f, 0), Vector2.one, new Vector2(8, 16), new Vector2(-18, -78));
            var bar = UI.Row(rightUi, "Preview Toolbar");
            Place(bar, new Vector2(0, 1), Vector2.one, new Vector2(12, -54), new Vector2(-12, -11));
            UI.Label(bar, "实时预览", 22);
            skeletonButton = UI.Button(bar, "关闭骨骼", null);
            skeletonCaption = skeletonButton.GetComponentInChildren<Text>();
            cropButton = UI.Button(bar, "填满裁切", null);
            cropCaption = cropButton.GetComponentInChildren<Text>();
            cropButton.gameObject.SetActive(false);
            fullscreenButton = UI.Button(bar, "全屏预览", null);
            fullscreenCaption = fullscreenButton.GetComponentInChildren<Text>();

            previewViewport = UI.Panel(rightUi, "Preview Viewport", Color.black);
            Place(previewViewport, Vector2.zero, Vector2.one, new Vector2(12, 51), new Vector2(-12, -65));
            previewViewport.gameObject.AddComponent<RectMask2D>();
            sourceImage = BuildPreviewImage(previewViewport, "Live Source Texture");
            sceneSkeletonImage = BuildPreviewImage(previewViewport, "Scene Skeleton Camera Texture");
            previewColorShader = Resources.Load<Shader>("HumanVisionRuntimePreviewColor");

            regionPlane = UI.Rect(previewViewport, "Recognition Regions");
            regionViews = new RectTransform[MaxCapacity];
            for (int i = 0; i < regionViews.Length; i++)
                regionViews[i] = BuildRegion(regionPlane, i);

            previewEmpty = UI.Label(previewViewport, "等待当前输入", 20);
            previewEmpty.name = "Preview Placeholder";
            previewEmpty.alignment = TextAnchor.MiddleCenter;
            UI.Fill(previewEmpty.rectTransform);
            previewInfo = UI.Label(rightUi, "等待当前输入", 17);
            previewInfo.name = "Preview Status";
            previewInfo.color = UI.Muted;
            Place(previewInfo.rectTransform, Vector2.zero, new Vector2(1, 0), new Vector2(12, 6), new Vector2(-12, 46));
        }

        private static RawImage BuildPreviewImage(RectTransform parent, string name)
        {
            var image = UI.Rect(parent, name).gameObject.AddComponent<RawImage>();
            image.raycastTarget = false;
            image.enabled = false;
            return image;
        }

        private static RectTransform BuildRegion(Transform parent, int index)
        {
            var rect = UI.Rect(parent, "Region " + (index + 1));
            Color color = RuntimePrefabRedrawRig.GetSlotColor(index);
            BuildRegionEdge(rect, "Top", new Vector2(0, 1), Vector2.one, new Vector2(0, -1.5f), Vector2.zero, color);
            BuildRegionEdge(rect, "Bottom", Vector2.zero, new Vector2(1, 0), Vector2.zero, new Vector2(0, 1.5f), color);
            BuildRegionEdge(rect, "Left", Vector2.zero, new Vector2(0, 1), Vector2.zero, new Vector2(1.5f, 0), color);
            BuildRegionEdge(rect, "Right", new Vector2(1, 0), Vector2.one, new Vector2(-1.5f, 0), Vector2.zero, color);
            var label = UI.Label(rect, "区域 " + (index + 1), 15);
            label.color = color;
            Place(label.rectTransform, new Vector2(0, 1), new Vector2(1, 1), new Vector2(6, -26), new Vector2(-6, -3));
            rect.gameObject.SetActive(false);
            return rect;
        }

        private static void BuildRegionEdge(Transform parent, string name, Vector2 from, Vector2 to, Vector2 min, Vector2 max, Color color)
        {
            var edge = UI.Panel(parent, name, color);
            edge.GetComponent<Image>().raycastTarget = false;
            Place(edge, from, to, min, max);
        }
    }
}
#endif
