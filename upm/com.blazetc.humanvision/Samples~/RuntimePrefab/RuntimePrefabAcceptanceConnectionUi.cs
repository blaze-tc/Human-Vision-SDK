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
        [Serializable]
        private sealed class ConnectionCaptureFields
        {
            [SerializeField] internal InputField Width;
            [SerializeField] internal InputField Height;
            [SerializeField] internal InputField Fps;
            [SerializeField] internal Toggle Mirror;

            internal bool IsAssigned => Width != null && Height != null && Fps != null && Mirror != null;

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

        [Header("输入连接弹窗")]
        [SerializeField] private RectTransform connectionDialog;
        [SerializeField] private Button connectionRtspTab;
        [SerializeField] private Text connectionRtspTabLabel;
        [SerializeField] private Button connectionCameraTab;
        [SerializeField] private Text connectionCameraTabLabel;
        [SerializeField] private Button connectionSubmit;
        [SerializeField] private Button connectionCancel;
        [SerializeField] private Text connectionHint;

        [Header("RTSP 页面")]
        [SerializeField] private RectTransform connectionRtspPanel;
        [SerializeField] private InputField connectionAddress;
        [SerializeField] private Button connectionAddressVisibility;
        [SerializeField] private Text connectionAddressVisibilityLabel;
        [SerializeField] private Toggle connectionTcp;
        [SerializeField] private Dropdown connectionExecution;
        [SerializeField] private ConnectionCaptureFields connectionRtspCapture;

        [Header("本地相机页面")]
        [SerializeField] private RectTransform connectionCameraPanel;
        [SerializeField] private Dropdown connectionDevices;
        [SerializeField] private Button connectionRefreshDevices;
        [SerializeField] private Text connectionCameraHint;
        [SerializeField] private ConnectionCaptureFields connectionCameraCapture;

        private HumanVisionSettingsData connectionDraft;
        private readonly List<string> connectionDeviceNames = new List<string>();
        private bool connectionUiBound;

        private bool BindConnectionUi()
        {
            if (connectionUiBound) return true;
            if (connectionDialog == null || connectionRtspTab == null || connectionRtspTabLabel == null ||
                connectionCameraTab == null || connectionCameraTabLabel == null || connectionSubmit == null ||
                connectionCancel == null || connectionHint == null || connectionRtspPanel == null ||
                connectionAddress == null || connectionAddressVisibility == null || connectionAddressVisibilityLabel == null ||
                connectionTcp == null || connectionExecution == null || connectionRtspCapture == null ||
                !connectionRtspCapture.IsAssigned || connectionCameraPanel == null || connectionDevices == null ||
                connectionRefreshDevices == null || connectionCameraHint == null || connectionCameraCapture == null ||
                !connectionCameraCapture.IsAssigned)
            {
                Debug.LogError("RuntimePrefabAcceptance：请在 Inspector 检查输入连接弹窗引用。", this);
                return false;
            }

            connectionRtspTab.onClick.AddListener(ShowRtspConnectionTab);
            connectionCameraTab.onClick.AddListener(ShowCameraConnectionTab);
            connectionSubmit.onClick.AddListener(ApplyConnectionUi);
            connectionCancel.onClick.AddListener(CloseConnectionUi);
            connectionAddressVisibility.onClick.AddListener(ToggleConnectionAddress);
            connectionRefreshDevices.onClick.AddListener(RefreshConnectionCameras);
            connectionExecution.onValueChanged.AddListener(OnConnectionExecutionChanged);
            connectionDialog.gameObject.SetActive(false);
            connectionUiBound = true;
            return true;
        }

        private void OpenConnectionUi()
        {
            if (!connectionUiBound || interactive == null || operating) return;

            // 弹窗编辑独立草稿，切换标签或取消不改变正在使用的配置。
            connectionDraft = interactive.Data.Clone();
            if (startup != null)
            {
                connectionDraft.AccelerationMode = startup.AccelerationMode;
                connectionDraft.InputQuality = startup.InputQuality;
                connectionDraft.RuntimeRoot = startup.RuntimeRoot;
            }
            if (recognition != null) connectionDraft.Recognition = recognition.Clone();

            connectionRtspCapture.Show(connectionDraft.Rtsp);
            connectionCameraCapture.Show(connectionDraft.Camera);
            connectionAddress.contentType = InputField.ContentType.Password;
            connectionAddress.SetTextWithoutNotify(connectionDraft.Rtsp.RtspUrl);
            connectionAddressVisibilityLabel.text = "显示地址";
            connectionTcp.SetIsOnWithoutNotify(connectionDraft.RtspTcp);
            connectionExecution.SetValueWithoutNotify((int)interactive.Execution);
            OnConnectionExecutionChanged(connectionExecution.value);
            connectionDeviceNames.Clear();
            connectionCameraHint.text = "";
            connectionDialog.gameObject.SetActive(true);
            connectionDialog.SetAsLastSibling();
            SelectConnectionTab(connectionDraft.SourceKind == InputKind.WebCamera ? InputKind.WebCamera : InputKind.Rtsp);
        }

        private void RefreshConnectionUi()
        {
            if (connectionUiBound && connectionDialog.gameObject.activeSelf)
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

        private void ShowRtspConnectionTab()
        {
            SelectConnectionTab(InputKind.Rtsp);
        }

        private void ShowCameraConnectionTab()
        {
            SelectConnectionTab(InputKind.WebCamera);
        }

        private void SelectConnectionTab(InputKind kind)
        {
            if (connectionDraft == null) return;

            connectionDraft.SourceKind = kind;
            bool isRtsp = kind == InputKind.Rtsp;
            connectionRtspPanel.gameObject.SetActive(isRtsp);
            connectionCameraPanel.gameObject.SetActive(!isRtsp);
            SetDialogButtonSelected(connectionRtspTab, connectionRtspTabLabel, isRtsp);
            SetDialogButtonSelected(connectionCameraTab, connectionCameraTabLabel, !isRtsp);
            connectionHint.text = "";
            if (!isRtsp && connectionDeviceNames.Count == 0) RefreshConnectionCameras();
        }

        private static void SetDialogButtonSelected(Button button, Text label, bool isSelected)
        {
            button.targetGraphic.color = isSelected ? Ui.Accent : Ui.Border;
            label.color = isSelected ? Ui.Background : Ui.Ink;
        }

        private void ToggleConnectionAddress()
        {
            bool showAddress = connectionAddress.contentType == InputField.ContentType.Password;
            connectionAddress.contentType = showAddress ? InputField.ContentType.Standard : InputField.ContentType.Password;
            connectionAddress.ForceLabelUpdate();
            connectionAddressVisibilityLabel.text = showAddress ? "隐藏地址" : "显示地址";
        }

        private void OnConnectionExecutionChanged(int execution)
        {
            connectionRtspCapture.Mirror.interactable =
                IsAndroid || execution == (int)HumanVisionExecutionPolicy.CompatibleUnityInput;
        }

        private void RefreshConnectionCameras()
        {
            if (connectionDraft == null) return;

            string requested = connectionDeviceNames.Count > 0
                ? connectionDeviceNames[connectionDevices.value]
                : connectionDraft.Camera.CameraDevice;
            connectionDeviceNames.Clear();
            connectionDeviceNames.Add("");
            var labels = new List<string> { "默认相机" };
            try
            {
                foreach (var device in WebCamTexture.devices)
                {
                    if (string.IsNullOrEmpty(device.name) || connectionDeviceNames.Contains(device.name)) continue;
                    connectionDeviceNames.Add(device.name);
                    labels.Add(device.name);
                }
                connectionCameraHint.text = labels.Count > 1 ? "" : "未枚举到相机。可以尝试连接默认设备。";
            }
            catch (Exception)
            {
                connectionCameraHint.text = "无法枚举相机。请检查相机权限后刷新设备。";
            }

            int index = string.IsNullOrEmpty(requested) ? 0 : connectionDeviceNames.IndexOf(requested);
            if (index < 0)
            {
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

            try
            {
                var candidate = connectionDraft.Clone();
                var execution = interactive.Execution;
                if (candidate.SourceKind == InputKind.Rtsp)
                {
                    ReadRtspConnection(candidate);
                    execution = (HumanVisionExecutionPolicy)connectionExecution.value;
                }
                else
                {
                    connectionCameraCapture.Read(candidate.Camera);
                    candidate.Camera.CameraDevice = connectionDeviceNames[connectionDevices.value];
                }

                var options = BuildInteractiveOptions(candidate, execution, interactive.Preview);
                options.Validate();
                if (!HasManualQuality(options.AccelerationMode, options.InputQuality))
                    throw new ArgumentException(BackendReason(options.AccelerationMode));

                interactive.Data = candidate;
                interactive.Execution = execution;
                diagnosticPhase = options.SourceKind == InputKind.Rtsp ? "interactive_rtsp" : "interactive_webcamera";
                displayedDecodedSize = default;
                CloseConnectionUi();
                rememberConnectedInput = true;
                Run(Initialize(options));
            }
            catch (ArgumentException error)
            {
                connectionHint.text = diagnostics.Redact(error.Message);
            }
        }

        private void ReadRtspConnection(HumanVisionSettingsData candidate)
        {
            string address = connectionAddress.text.Trim();
            if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp" || string.IsNullOrEmpty(uri.Host))
                throw new ArgumentException("请输入完整的 rtsp:// 地址。");

            candidate.Rtsp.RtspUrl = address;
            connectionRtspCapture.Read(candidate.Rtsp);
            candidate.RtspTcp = connectionTcp.isOn;
            if (!connectionRtspCapture.Mirror.interactable) candidate.Rtsp.Mirror = false;
        }
    }
}
