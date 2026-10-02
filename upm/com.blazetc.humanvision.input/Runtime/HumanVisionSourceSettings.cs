using System;

namespace HumanVision.Input
{
    /// <summary>Capture requests, never a declaration of the actual output geometry.</summary>
    [Serializable]
    public class HumanVisionSourceSettings
    {
        public InputKind Kind;
        public string Location = string.Empty;
        public string DeviceName = string.Empty;
        public int RequestedWidth = 1280;
        public int RequestedHeight = 720;
        public int RequestedFramesPerSecond = 30;
        public bool DisplayMirror;
    }
}
