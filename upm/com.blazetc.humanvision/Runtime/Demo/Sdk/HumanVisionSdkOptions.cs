using System;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision
{
    /// <summary>总控启动配置。配置是草稿；Configuration 和 ActiveConfiguration 均返回独立副本。</summary>
    [Serializable]
    public sealed class HumanVisionSdkOptions
    {
        /// <summary>共享识别人数、区域与有效性设置。</summary>
        public HumanVisionSdkConfiguration Recognition = new HumanVisionSdkConfiguration();
        /// <summary>选择摄像头、视频或 RTSP。</summary>
        public InputKind SourceKind = InputKind.WebCamera;
        /// <summary>当前输入的采集请求和地址；采集尺寸不是模型尺寸。</summary>
        public DemoModeSettings Input = new DemoModeSettings();
        /// <summary>RTSP 使用 TCP；关闭后使用 UDP。</summary>
        public bool RtspTcp = true;
        /// <summary>Windows 默认 CPU；关闭时使用已有 GPU 配置。</summary>
        public bool UseWindowsCpu = true;
        /// <summary>平台已声明的输入质量档位；不支持的平台保持真实固定合同。</summary>
        public ModelInputQuality InputQuality = ModelInputQuality.Medium;
        /// <summary>可选运行资源根目录；留空时自动从已安装 RuntimeData 准备。</summary>
        [Tooltip("留空自动准备运行资源；开发时可指定已经验证的 Runtime 根目录。")]
        public string RuntimeRoot = "";
        /// <summary>默认初始化后打开输入；关闭可只准备 Runtime，然后手动配置输入。</summary>
        public bool OpenInputOnInitialize = true;
        /// <summary>等待输入进入 Streaming 的最长时间，单位秒。</summary>
        [Min(1)] public float InputOpenTimeoutSeconds = 15;
        /// <summary>深复制可修改设置；调用方修改副本不会改变运行中的 SDK。</summary>
        public HumanVisionSdkOptions Clone()
        {
            var copy = (HumanVisionSdkOptions)MemberwiseClone();
            copy.Recognition = Recognition?.Clone();
            copy.Input = Input == null ? null : JsonUtility.FromJson<DemoModeSettings>(JsonUtility.ToJson(Input));
            return copy;
        }
        /// <summary>验证配置；失败不会停止当前输入。</summary>
        public void Validate()
        {
            if (Recognition == null || Input == null || !Enum.IsDefined(typeof(InputKind), SourceKind) ||
                !Enum.IsDefined(typeof(ModelInputQuality), InputQuality))
                throw new ArgumentException("识别配置、输入配置或输入类型无效。");
            Recognition.Validate(); Input.Validate();
            if (float.IsNaN(InputOpenTimeoutSeconds) || float.IsInfinity(InputOpenTimeoutSeconds) || InputOpenTimeoutSeconds < 1)
                throw new ArgumentException("输入启动超时必须是至少一秒的有限值。");
            if (OpenInputOnInitialize && SourceKind == InputKind.Video && string.IsNullOrWhiteSpace(Input.VideoPath))
                throw new ArgumentException("视频输入需要有效的视频路径。");
            if (OpenInputOnInitialize && SourceKind == InputKind.Rtsp &&
                (!Uri.TryCreate(Input.RtspUrl, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp"))
                throw new ArgumentException("RTSP 输入需要 rtsp:// 地址。");
        }
        internal HumanVisionSourceSettings ToSourceSettings()
        {
            var value = Input.ToSourceSettings(SourceKind);
            if (value is RtspSourceSettings rtsp) rtsp.Transport = RtspTcp ? RtspTransport.Tcp : RtspTransport.Udp;
            return value;
        }
    }

    /// <summary>生命周期状态；Ready 表示 Runtime 已就绪，Running 才表示输入正在流送。</summary>
    public enum HumanVisionSdkState { Stopped, Preparing, Ready, Opening, Running, Stopping, Error }
    /// <summary>区域观测状态；Unknown 包括尚无结果、停止和过期结果。</summary>
    public enum HumanVisionRegionOccupancy { Unknown, Empty, Occupied }
}
