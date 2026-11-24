#if UNITY_EDITOR
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using Ui = HumanVisionAcceptance.RuntimeAcceptanceUi;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private void BuildConnectionUi(RectTransform overlayRoot)
        {
            connectionDialog = Ui.Panel(overlayRoot, "输入连接弹窗", new Color(0, 0, 0, .72f));
            Ui.Fill(connectionDialog);
            var card = Ui.Panel(connectionDialog, "输入连接", Ui.Surface);
            card.anchorMin = card.anchorMax = card.pivot = new Vector2(.5f, .5f);
            card.anchoredPosition = Vector2.zero;
            card.sizeDelta = new Vector2(720, 600);
            var layout = Ui.Stack(card, "连接设置", 10);
            Ui.Fill(layout, 22);
            Ui.Height(Ui.Label(layout, "输入连接", 29), 40);

            var tabs = Ui.Row(layout, "输入方式");
            Ui.Height(tabs, 42);
            connectionRtspTab = Ui.Button(tabs, "RTSP", null);
            connectionRtspTabLabel = connectionRtspTab.GetComponentInChildren<Text>(true);
            connectionCameraTab = Ui.Button(tabs, "本地相机", null);
            connectionCameraTabLabel = connectionCameraTab.GetComponentInChildren<Text>(true);

            var pages = Ui.Rect(layout, "输入配置页");
            Ui.Height(pages, 324);
            connectionRtspPanel = Ui.Stack(pages, "RTSP 配置");
            Ui.Fill(connectionRtspPanel);
            BuildRtspConnectionPage(connectionRtspPanel);
            connectionCameraPanel = Ui.Stack(pages, "本地相机配置");
            Ui.Fill(connectionCameraPanel);
            BuildCameraConnectionPage(connectionCameraPanel);

            connectionHint = Ui.Label(layout, "", 16);
            connectionHint.color = Ui.Accent;
            Ui.Height(connectionHint, 38);
            var actions = Ui.Row(layout, "连接操作");
            Ui.Height(actions, 44);
            connectionCancel = Ui.Button(actions, "取消", null);
            connectionSubmit = Ui.Button(actions, "应用并连接", null);
            Ui.Select(connectionSubmit, true);
            connectionDialog.gameObject.SetActive(false);
        }

        private void BuildRtspConnectionPage(RectTransform page)
        {
            Ui.Label(page, "完整 RTSP 地址");
            var addressRow = Ui.Row(page, "地址");
            Ui.Height(addressRow, 36);
            connectionAddress = Ui.TextField(addressRow, "", secret: true);
            connectionAddressVisibility = Ui.Button(addressRow, "显示地址", null, 36);
            connectionAddressVisibilityLabel = connectionAddressVisibility.GetComponentInChildren<Text>(true);
            LimitConnectionWidth(connectionAddressVisibility, 112);

            connectionRtspCapture = BuildCaptureFields(page);
            var switches = Ui.Row(page, "RTSP 选项");
            Ui.Height(switches, 30);
            connectionTcp = Ui.Toggle(switches, "TCP 传输", true, null);
            connectionRtspCapture.Mirror = Ui.Toggle(switches, "水平镜像", false, null);

            Ui.Label(page, "执行方式");
            connectionExecution = BuildConnectionDropdown(page, "RTSP 执行方式");
            connectionExecution.AddOptions(new List<string> { "兼容输入", "优先独立", "强制独立" });
            var detail = Ui.Label(page, "实时预览使用原始码流。Windows 独立输入须关闭镜像。", 16);
            detail.color = Ui.Muted;
        }

        private void BuildCameraConnectionPage(RectTransform page)
        {
            Ui.Label(page, "相机设备");
            var deviceRow = Ui.Row(page, "选择设备");
            Ui.Height(deviceRow, 36);
            connectionDevices = BuildConnectionDropdown(deviceRow, "本地相机设备");
            connectionRefreshDevices = Ui.Button(deviceRow, "刷新设备", null, 36);
            LimitConnectionWidth(connectionRefreshDevices, 112);

            connectionCameraHint = Ui.Label(page, "", 16);
            connectionCameraHint.color = Ui.Muted;
            Ui.Height(connectionCameraHint, 30);
            connectionCameraCapture = BuildCaptureFields(page);
            connectionCameraCapture.Mirror = Ui.Toggle(page, "水平镜像", false, null);
        }

        private static ConnectionCaptureFields BuildCaptureFields(Transform page)
        {
            var size = Ui.Row(page, "尺寸和帧率");
            Ui.Height(size, 66);
            return new ConnectionCaptureFields {
                Width = BuildConnectionNumber(size, "请求宽度"),
                Height = BuildConnectionNumber(size, "请求高度"),
                Fps = BuildConnectionNumber(size, "请求帧率")
            };
        }

        private static InputField BuildConnectionNumber(Transform parent, string name)
        {
            var column = Ui.Stack(parent, name, 4);
            Ui.Label(column, name, 16).name = "说明";
            var field = Ui.TextField(column, "");
            field.contentType = InputField.ContentType.IntegerNumber;
            field.characterLimit = 4;
            return field;
        }

        private static void LimitConnectionWidth(Component control, float width)
        {
            var layout = control.GetComponent<LayoutElement>();
            layout.minWidth = layout.preferredWidth = width;
            control.transform.parent.GetComponent<HorizontalLayoutGroup>().childForceExpandWidth = false;
            foreach (RectTransform sibling in control.transform.parent) {
                if (sibling == control.transform) continue;
                var siblingLayout = sibling.GetComponent<LayoutElement>();
                if (siblingLayout == null) siblingLayout = sibling.gameObject.AddComponent<LayoutElement>();
                siblingLayout.flexibleWidth = 1;
            }
        }

        private static Dropdown BuildConnectionDropdown(Transform parent, string name)
        {
            var root = Ui.Panel(parent, name, Ui.Field);
            var dropdown = root.gameObject.AddComponent<Dropdown>();
            dropdown.targetGraphic = root.GetComponent<Image>();
            Ui.Height(dropdown, 36);
            dropdown.captionText = Ui.Label(root, "", 18);
            FillConnectionCaption(dropdown.captionText.rectTransform, 34);
            var arrow = Ui.Label(root, "▾", 17);
            arrow.rectTransform.anchorMin = new Vector2(1, 0);
            arrow.rectTransform.anchorMax = Vector2.one;
            arrow.rectTransform.offsetMin = new Vector2(-27, 0);
            arrow.rectTransform.offsetMax = Vector2.zero;

            var template = Ui.Panel(root, "设备选项", Ui.Border);
            template.anchorMin = Vector2.zero;
            template.anchorMax = new Vector2(1, 0);
            template.pivot = new Vector2(.5f, 1);
            template.anchoredPosition = new Vector2(0, -2);
            template.sizeDelta = new Vector2(0, 176);
            var scroll = template.gameObject.AddComponent<ScrollRect>();
            scroll.horizontal = false;
            scroll.movementType = ScrollRect.MovementType.Clamped;
            scroll.scrollSensitivity = 30;
            var viewport = Ui.Panel(template, "选项可见区", Ui.Surface);
            Ui.Fill(viewport, 2);
            viewport.gameObject.AddComponent<RectMask2D>();

            var content = Ui.Rect(viewport, "选项内容");
            content.anchorMin = new Vector2(0, 1);
            content.anchorMax = Vector2.one;
            content.pivot = new Vector2(.5f, 1);
            content.anchoredPosition = Vector2.zero;
            content.sizeDelta = new Vector2(0, 36);
            var item = Ui.Panel(content, "选项", Ui.Field);
            item.anchorMin = new Vector2(0, .5f);
            item.anchorMax = new Vector2(1, .5f);
            item.anchoredPosition = Vector2.zero;
            item.sizeDelta = new Vector2(0, 36);
            var toggle = item.gameObject.AddComponent<Toggle>();
            toggle.targetGraphic = item.GetComponent<Image>();
            var check = Ui.Panel(item, "当前选项", Ui.Accent);
            check.anchorMin = Vector2.zero;
            check.anchorMax = new Vector2(0, 1);
            check.offsetMin = new Vector2(4, 4);
            check.offsetMax = new Vector2(8, -4);
            check.GetComponent<Image>().raycastTarget = false;
            toggle.graphic = check.GetComponent<Image>();
            dropdown.itemText = Ui.Label(item, "", 17);
            FillConnectionCaption(dropdown.itemText.rectTransform, 8);
            scroll.viewport = viewport;
            scroll.content = content;
            dropdown.template = template;
            template.gameObject.SetActive(false);
            return dropdown;
        }

        private static void FillConnectionCaption(RectTransform rect, float rightPadding)
        {
            Ui.Fill(rect);
            rect.offsetMin = new Vector2(14, 0);
            rect.offsetMax = new Vector2(-rightPadding, 0);
        }

        private void BuildAdvancedUi(RectTransform host)
        {
            advancedUi = Ui.Panel(host, "Recognition Settings Dialog", new Color(0, 0, 0, .72f));
            Ui.Fill(advancedUi);
            var card = Ui.Panel(advancedUi, "Recognition Settings", Ui.Surface);
            Place(card, new Vector2(.22f, .09f), new Vector2(.78f, .91f), Vector2.zero, Vector2.zero);
            var title = Ui.Label(card, "识别与显示设置", 25);
            Place(title.rectTransform, new Vector2(0, 1), Vector2.one, new Vector2(20, -63), new Vector2(-20, -12));
            var content = Ui.Scroll(card, "Recognition Settings Scroll", 10);
            Place((RectTransform)content.parent.parent, Vector2.zero, Vector2.one, new Vector2(20, 89), new Vector2(-20, -69));
            var display = Ui.Row(content, "Display");
            Ui.Height(display, 32);
            advancedPreview = Ui.Toggle(display, "显示预览", true, null);
            advancedSkeleton = Ui.Toggle(display, "显示骨骼", true, null);
            advancedUseRegions = Ui.Toggle(display, "启用识别区域", true, null);
            Ui.Label(content, "Unity 帧率上限", 18);
            var fps = Ui.Row(content, "Unity Frame Rate");
            Ui.Height(fps, 36);
            advancedFps = new Button[AdvancedFrameRates.Length];
            advancedFpsLabels = new Text[AdvancedFrameRates.Length];
            for (int index = 0; index < AdvancedFrameRates.Length; index++)
            {
                advancedFps[index] = Ui.Button(fps, AdvancedFrameRates[index] + " FPS", null);
                advancedFpsLabels[index] = advancedFps[index].GetComponentInChildren<Text>(true);
            }
            var filters = Ui.Row(content, "Recognition Limits");
            advancedCount = AdvancedInput(filters, "人数上限（1–8）");
            advancedConfidence = AdvancedInput(filters, "关节置信度");
            advancedAge = AdvancedInput(filters, "身体时效（ms）");
            advancedHandAge = AdvancedInput(filters, "手时效（ms）");
            var regionLabel = Ui.Row(content, "Region Editor");
            Ui.Height(regionLabel, 35);
            Ui.Label(regionLabel, "识别区域（左上原点，0–1）", 17);
            advancedDistributeRegions = Ui.Button(regionLabel, "按人数横向均分", null, 34);
            advancedRegions = Ui.TextField(content, "", multiline: true);
            Ui.Label(content, "每行输入 x,y,width,height，区域不可重叠。", 15).color = Ui.Muted;
            advancedStatus = Ui.Label(card, "", 15);
            advancedStatus.color = Ui.Muted;
            Place(advancedStatus.rectTransform, Vector2.zero, new Vector2(1, 0), new Vector2(20, 53), new Vector2(-20, 86));
            var actions = Ui.Row(card, "Recognition Settings Actions");
            Place(actions, Vector2.zero, new Vector2(1, 0), new Vector2(20, 13), new Vector2(-20, 51));
            advancedLoad = Ui.Button(actions, "读取", null);
            advancedSave = Ui.Button(actions, "保存草稿", null);
            advancedCancel = Ui.Button(actions, "取消", null);
            advancedApply = Ui.Button(actions, "应用", null);
            Ui.Select(advancedApply, true);
            advancedUi.gameObject.SetActive(false);
        }

        private static InputField AdvancedInput(Transform parent, string label)
        {
            var column = Ui.Stack(parent, label, 4);
            Ui.Label(column, label, 16);
            return Ui.TextField(column, "");
        }

    }
}
#endif
