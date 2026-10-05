using System;
using HumanVision.Input;

namespace HumanVision.Demo
{
    [Serializable]
    public sealed class DemoModeSettings
    {
        public int Version = 1;
        public string CameraDevice = "", VideoPath = "", RtspUrl = "";
        public string RtspComputerHost = "";
        public bool Mirror;
        public int RequestedWidth = 1280, RequestedHeight = 720, RequestedFramesPerSecond = 30;
        public float LineWidth = 9, PointDiameter = 27;
        public void Validate()
        {
            if (Version != 1 || RequestedWidth < 64 || RequestedHeight < 64 || RequestedWidth > 4096 ||
                RequestedHeight > 4096 || RequestedFramesPerSecond < 1 || RequestedFramesPerSecond > 120 ||
                float.IsNaN(LineWidth) || float.IsInfinity(LineWidth) || LineWidth < 1 || LineWidth > 64 ||
                float.IsNaN(PointDiameter) || float.IsInfinity(PointDiameter) || PointDiameter < 1 || PointDiameter > 128)
                throw new ArgumentException("Invalid capture request or skeleton size.");
        }
        public HumanVisionSourceSettings ToSourceSettings(InputKind kind)
        {
            Validate();
            HumanVisionSourceSettings settings = kind == InputKind.Rtsp ? new RtspSourceSettings() : new HumanVisionSourceSettings();
            settings.Kind = kind; settings.DeviceName = CameraDevice;
            settings.Location = kind == InputKind.Video ? VideoPath : RtspUrl;
            settings.RequestedWidth = RequestedWidth; settings.RequestedHeight = RequestedHeight;
            settings.RequestedFramesPerSecond = RequestedFramesPerSecond; settings.DisplayMirror = Mirror;
            return settings;
        }
    }
}
