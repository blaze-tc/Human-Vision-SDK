using System;

namespace HumanVision.Input
{
    public enum RtspTransport { Tcp, Udp }
    /// <summary>RTSP output bounds: 0/0 preserves visible source size; positive pairs retain explicit local caps.</summary>
    [Serializable]
    public sealed class RtspSourceSettings : HumanVisionSourceSettings
    {
        public RtspTransport Transport = RtspTransport.Tcp;
        public int OpenTimeoutMs = 5000;
        public int ReconnectDelayMs = 500;
        public RtspSourceSettings() { Kind = InputKind.Rtsp; }
    }
}
