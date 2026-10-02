using UnityEngine;

namespace HumanVision.Input
{
    public enum InputSourceState { Stopped, Opening, Streaming, Reconnecting, Error, Closing }
    public enum InputKind { Video, WebCamera, Rtsp }

    /// <summary>Unity main-thread contract. Borrowed textures are valid only in the current generation.</summary>
    public interface IHumanVisionFrameSource
    {
        void Open(HumanVisionSourceSettings settings);
        void Close();
        InputSourceState State { get; }
        string LastError { get; }
        Texture CurrentTexture { get; }
        bool TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame);
        bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease);
    }
}
