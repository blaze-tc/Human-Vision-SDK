using System;
using HumanVision.Input;

namespace HumanVision.Demo
{
    /// <summary>各输入源独立保存的采集请求及显示参数；Requested 尺寸/FPS 是请求，不保证源设备接受。</summary>
    [Serializable]
    public sealed class DemoModeSettings
    {
        /// <summary>设置文件格式版本。</summary>
        public int Version = 1;
        /// <summary>Camera 设备名；空串由输入组件选择默认相机。</summary>
        public string CameraDevice = "";
        /// <summary>视频完整路径或受支持的 URL；StreamingAssets 请拼接 Application.streamingAssetsPath。</summary>
        public string VideoPath = "";
        /// <summary>RTSP 完整地址，必须包含 rtsp://；日志会对凭据脱敏。</summary>
        public string RtspUrl = "";
        /// <summary>Settings 中自动填入电脑推流地址的主机；不会替代 RtspUrl。</summary>
        public string RtspComputerHost = "";
        /// <summary>水平镜像显示和骨骼映射；调用方不要再次镜像关节。</summary>
        public bool Mirror;
        /// <summary>相机请求采集宽高；RTSP 为本地输出上限；视频使用源尺寸。与模型等级无关。</summary>
        public int RequestedWidth = 1280, RequestedHeight = 720;
        /// <summary>相机/输入请求帧率（1–120）；不提高摄像头或上游 RTSP 的原始帧率。</summary>
        public int RequestedFramesPerSecond = 30;
        /// <summary>设置界面骨骼线宽和点直径，屏幕像素。</summary>
        public float LineWidth = 4.5f, PointDiameter = 13.5f;
        /// <summary>检查尺寸、帧率和样式范围。源地址由 SDK 启动预检验证。</summary>
        public void Validate()
        {
            if (Version != 1 || RequestedWidth < 64 || RequestedHeight < 64 || RequestedWidth > 4096 ||
                RequestedHeight > 4096 || RequestedFramesPerSecond < 1 || RequestedFramesPerSecond > 120 ||
                float.IsNaN(LineWidth) || float.IsInfinity(LineWidth) || LineWidth < 1 || LineWidth > 64 ||
                float.IsNaN(PointDiameter) || float.IsInfinity(PointDiameter) || PointDiameter < 1 || PointDiameter > 128)
                throw new ArgumentException("Invalid capture request or skeleton size.");
        }
        /// <summary>转换为独立输入包的配置，不打开设备。</summary>
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
