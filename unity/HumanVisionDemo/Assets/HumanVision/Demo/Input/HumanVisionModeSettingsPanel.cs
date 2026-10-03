using System;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    public sealed class HumanVisionModeSettingsPanel : MonoBehaviour
    {
        private InputField location, width, height, fps, line, point;
        private bool mirror;
        public void Build(HumanVisionDemoNavigator navigator, Transform content)
        {
            var mode = navigator.Mode; mirror = mode.Mirror;
            location = InputPreviewCanvas.Field(content, navigator.Kind == InputKind.Video ? "Video path" : navigator.Kind == InputKind.WebCamera ? "Camera device (empty = default)" : "RTSP URL (H.264 / TCP)",
                navigator.Kind == InputKind.Video ? mode.VideoPath : navigator.Kind == InputKind.WebCamera ? mode.CameraDevice : mode.RtspUrl);
            width = InputPreviewCanvas.Field(content, "Requested capture width", mode.RequestedWidth.ToString());
            height = InputPreviewCanvas.Field(content, "Requested capture height", mode.RequestedHeight.ToString());
            fps = InputPreviewCanvas.Field(content, "Requested capture FPS", mode.RequestedFramesPerSecond.ToString());
            line = InputPreviewCanvas.Field(content, "Skeleton line width (px)", mode.LineWidth.ToString(System.Globalization.CultureInfo.InvariantCulture));
            point = InputPreviewCanvas.Field(content, "Joint diameter (px)", mode.PointDiameter.ToString(System.Globalization.CultureInfo.InvariantCulture));
            var toggle = InputPreviewCanvas.Button(content, "Mirror: " + mirror, () => {});
            toggle.onClick.AddListener(() => { mirror = !mirror; toggle.GetComponentInChildren<Text>().text = "Mirror: " + mirror; });
            InputPreviewCanvas.Button(content, "Start / reconnect source", navigator.OpenSource);
            InputPreviewCanvas.Button(content, "Stop source", navigator.Input.Close);
        }
        public void ReadInto(DemoModeSettings settings)
        {
            if (!int.TryParse(width.text, out settings.RequestedWidth) || !int.TryParse(height.text, out settings.RequestedHeight) ||
                !int.TryParse(fps.text, out settings.RequestedFramesPerSecond) ||
                !float.TryParse(line.text, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out settings.LineWidth) ||
                !float.TryParse(point.text, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out settings.PointDiameter))
                throw new ArgumentException("Capture requests and skeleton sizes must be numbers.");
            settings.CameraDevice = settings.VideoPath = settings.RtspUrl = location.text; settings.Mirror = mirror; settings.Validate();
        }
    }
}
