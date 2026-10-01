using System;
using UnityEngine;
namespace HumanVision
{
    internal interface IHumanVisionSession : IDisposable
    {
        int MaxBodies { get; }
        HumanVisionBody[] Bodies { get; }
        int BodyCount { get; }
        long ResultSequence { get; }
        long SourceFrameId { get; }
        long SourceTimestampUs { get; }
        HumanVisionStats Stats { get; }
        bool SubmitFrame(IntPtr data, int width, int height, int strideBytes, HumanVisionPixelFormat format, long frameId, long timestampUs, int bytes);
        bool PollLatestResult();
        void SetRegions(Rect[] regions, long revision);
        bool CopyRegions(long sequence, int[] indices, out long revision);
        void RefreshStats();
        void ReconfigureMaxBodies(int maxBodies);
    }
}
