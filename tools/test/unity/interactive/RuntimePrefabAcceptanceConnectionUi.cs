#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
using System;
using System.Collections.Generic;
using System.Globalization;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;
using Ui = HumanVisionAcceptance.RuntimeAcceptanceUi;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private sealed class ConnectionCaptureFields
        {
            internal InputField Width, Height, Fps;
            internal Toggle Mirror;

            internal void Show(DemoModeSettings mode)
            {
                Width.SetTextWithoutNotify(mode.RequestedWidth.ToString(CultureInfo.InvariantCulture));
                Height.SetTextWithoutNotify(mode.RequestedHeight.ToString(CultureInfo.InvariantCulture));
                Fps.SetTextWithoutNotify(mode.RequestedFramesPerSecond.ToString(CultureInfo.InvariantCulture));
                Mirror.SetIsOnWithoutNotify(mode.Mirror);
            }

            internal void Read(DemoModeSettings mode)
            {
                mode.RequestedWidth = ReadInt(Width.text, "请求宽度");
                mode.RequestedHeight = ReadInt(Height.text, "请求高度");
                mode.RequestedFramesPerSecond = ReadInt(Fps.text, "请求帧率");
                mode.Mirror = Mirror.isOn;
            }
        }

        private RectTransform connectionDialog, connectionRtspPanel, connectionCameraPanel;
        private Button connectionRtspTab, connectionCameraTab, connectionSubmit, connectionAddressVisibility;
        private InputField connectionAddress;
        private Toggle connectionTcp;
        private Dropdown connectionExecution, connectionDevices;
        private Text connectionHint, connectionCameraHint;
        private ConnectionCaptureFields connectionRtspCapture, connectionCameraCapture;
        private HumanVisionSettingsData connectionDraft;
        private readonly List<string> connectionDeviceNames = new List<string>();

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
            connectionRtspTab = Ui.Button(tabs, "RTSP", () => SelectConnectionTab(InputKind.Rtsp));
            connectionCameraTab = Ui.Button(tabs, "本地相机", () => SelectConnectionTab(InputKind.WebCamera));

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
            Ui.Button(actions, "取消", CloseConnectionUi);
            connectionSubmit = Ui.Button(actions, "应用并连接", ApplyConnectionUi);
            Ui.Select(connectionSubmit, true);
            connectionDialog.gameObject.SetActive(false);
        }

        private void BuildRtspConnectionPage(RectTransform page)
        {
            Ui.Label(page, "完整 RTSP 地址");
            var addressRow = Ui.Row(page, "地址");
            Ui.Height(addressRow, 36);
            connectionAddress = Ui.TextField(addressRow, "", secret: true);
            connectionAddressVisibility = Ui.Button(addressRow, "显示地址", ToggleConnectionAddress, 36);
            LimitConnectionWidth(connectionAddressVisibility, 112);

            connectionRtspCapture = BuildCaptureFields(page);
            var switches = Ui.Row(page, "RTSP 选项");
            Ui.Height(switches, 30);
            connectionTcp = Ui.Toggle(switches, "TCP 传输", true, null);
            connectionRtspCapture.Mirror = Ui.Toggle(switches, "水平镜像", false, null);

            Ui.Label(page, "执行方式");
            connectionExecution = BuildConnectionDropdown(page, "RTSP 执行方式");
            connectionExecution.AddOptions(new List<string> { "兼容输入", "优先独立", "强制独立" });
            connectionExecution.onValueChanged.AddListener(_ => UpdateConnectionMirror());
            var detail = Ui.Label(page, "实时预览使用原始码流。Windows 独立输入须关闭镜像。", 16);
            detail.color = Ui.Muted;
        }

        private void BuildCameraConnectionPage(RectTransform page)
        {
            Ui.Label(page, "相机设备");
            var deviceRow = Ui.Row(page, "选择设备");
            Ui.Height(deviceRow, 36);
            connectionDevices = BuildConnectionDropdown(deviceRow, "本地相机设备");
            var refresh = Ui.Button(deviceRow, "刷新设备", RefreshConnectionCameras, 36);
            LimitConnectionWidth(refresh, 112);

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

        private void OpenConnectionUi()
        {
            if (connectionDialog == null || interactive == null || operating || automatedFixture) return;
            connectionDraft = interactive.Data.Clone();
            if (startup != null) {
                connectionDraft.AccelerationMode = startup.AccelerationMode;
                connectionDraft.InputQuality = startup.InputQuality;
                connectionDraft.RuntimeRoot = startup.RuntimeRoot;
            }
            if (recognition != null) connectionDraft.Recognition = recognition.Clone();
            connectionRtspCapture.Show(connectionDraft.Rtsp);
            connectionCameraCapture.Show(connectionDraft.Camera);
            connectionAddress.contentType = InputField.ContentType.Password;
            connectionAddress.SetTextWithoutNotify(connectionDraft.Rtsp.RtspUrl);
            Ui.Caption(connectionAddressVisibility, "显示地址");
            connectionTcp.SetIsOnWithoutNotify(connectionDraft.RtspTcp);
            connectionExecution.SetValueWithoutNotify((int)interactive.Execution);
            UpdateConnectionMirror();
            connectionDeviceNames.Clear();
            connectionCameraHint.text = "";
            connectionHint.text = "";
            if (connectionDraft.SourceKind != InputKind.WebCamera) connectionDraft.SourceKind = InputKind.Rtsp;
            connectionDialog.gameObject.SetActive(true);
            connectionDialog.SetAsLastSibling();
            SelectConnectionTab(connectionDraft.SourceKind);
        }

        private void RefreshConnectionUi()
        {
            if (connectionDialog == null || !connectionDialog.gameObject.activeSelf) return;
            connectionSubmit.interactable = !operating;
        }

        private void CloseConnectionUi()
        {
            connectionDraft = null;
            connectionAddress.DeactivateInputField();
            connectionDevices.Hide();
            connectionExecution.Hide();
            connectionDialog.gameObject.SetActive(false);
        }

        private void SelectConnectionTab(InputKind kind)
        {
            if (connectionDraft == null) return;
            connectionDraft.SourceKind = kind;
            bool rtsp = kind == InputKind.Rtsp;
            connectionRtspPanel.gameObject.SetActive(rtsp);
            connectionCameraPanel.gameObject.SetActive(!rtsp);
            Ui.Select(connectionRtspTab, rtsp);
            Ui.Select(connectionCameraTab, !rtsp);
            connectionHint.text = "";
            if (!rtsp && connectionDeviceNames.Count == 0) RefreshConnectionCameras();
        }

        private void ToggleConnectionAddress()
        {
            bool show = connectionAddress.contentType == InputField.ContentType.Password;
            connectionAddress.contentType = show ? InputField.ContentType.Standard : InputField.ContentType.Password;
            connectionAddress.ForceLabelUpdate();
            Ui.Caption(connectionAddressVisibility, show ? "隐藏地址" : "显示地址");
        }

        private void UpdateConnectionMirror()
        {
            bool mirrorSupported = IsAndroid || connectionExecution.value == (int)HumanVisionExecutionPolicy.CompatibleUnityInput;
            connectionRtspCapture.Mirror.interactable = mirrorSupported;
        }

        private void RefreshConnectionCameras()
        {
            var requested = connectionDeviceNames.Count > 0 ? connectionDeviceNames[connectionDevices.value] : connectionDraft.Camera.CameraDevice;
            connectionDeviceNames.Clear();
            connectionDeviceNames.Add("");
            var labels = new List<string> { "默认相机" };
            try {
                foreach (var device in WebCamTexture.devices) {
                    if (string.IsNullOrEmpty(device.name) || connectionDeviceNames.Contains(device.name)) continue;
                    connectionDeviceNames.Add(device.name);
                    labels.Add(device.name);
                }
                connectionCameraHint.text = labels.Count > 1 ? "" : "未枚举到相机。可以尝试连接默认设备。";
            } catch (Exception) {
                connectionCameraHint.text = "无法枚举相机。请检查相机权限后刷新设备。";
            }
            int index = string.IsNullOrEmpty(requested) ? 0 : connectionDeviceNames.IndexOf(requested);
            if (index < 0) {
                index = connectionDeviceNames.Count;
                connectionDeviceNames.Add(requested);
                labels.Add(requested + "（未找到）");
            }
            connectionDevices.ClearOptions();
            connectionDevices.AddOptions(labels);
            connectionDevices.SetValueWithoutNotify(index);
        }

        private void ApplyConnectionUi()
        {
            if (connectionDraft == null || operating) return;
            try {
                var candidate = connectionDraft.Clone();
                var execution = interactive.Execution;
                if (candidate.SourceKind == InputKind.Rtsp) {
                    var address = connectionAddress.text.Trim();
                    if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp" || string.IsNullOrEmpty(uri.Host))
                        throw new ArgumentException("请输入完整的 rtsp:// 地址。");
                    candidate.Rtsp.RtspUrl = address;
                    connectionRtspCapture.Read(candidate.Rtsp);
                    candidate.RtspTcp = connectionTcp.isOn;
                    execution = (HumanVisionExecutionPolicy)connectionExecution.value;
                    if (!connectionRtspCapture.Mirror.interactable) candidate.Rtsp.Mirror = false;
                } else {
                    connectionCameraCapture.Read(candidate.Camera);
                    candidate.Camera.CameraDevice = connectionDeviceNames[connectionDevices.value];
                }
                var options = BuildInteractiveOptions(candidate, execution, interactive.Preview);
                options.Validate();
                if (!HasManualQuality(options.AccelerationMode, options.InputQuality))
                    throw new ArgumentException(BackendReason(options.AccelerationMode));
                interactive.Data = candidate;
                interactive.Execution = execution;
                thermalPhase = options.SourceKind == InputKind.Rtsp ? "interactive_rtsp" : "interactive_webcamera";
                streamSelection = default;
                streamLabel = null;
                displayedDecodedSize = default;
                CloseConnectionUi();
                Run(Initialize(options));
            } catch (ArgumentException error) {
                connectionHint.text = diagnostics.Redact(error.Message);
            }
        }
    }
}
#endif
