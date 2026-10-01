namespace HumanVision
{
    public readonly struct HumanVisionStats
    {
        internal HumanVisionStats(
            float inputFps,
            float inferenceFps,
            float detectionMs,
            float poseMs,
            float trackingMs,
            float totalMs,
            long submittedFrames,
            long processedFrames,
            long droppedFrames)
        {
            InputFps = inputFps;
            InferenceFps = inferenceFps;
            DetectionMs = detectionMs;
            PoseMs = poseMs;
            TrackingMs = trackingMs;
            TotalMs = totalMs;
            SubmittedFrames = submittedFrames;
            ProcessedFrames = processedFrames;
            DroppedFrames = droppedFrames;
        }

        public float InputFps { get; }
        public float InferenceFps { get; }
        public float DetectionMs { get; }
        public float PoseMs { get; }
        public float TrackingMs { get; }
        public float TotalMs { get; }
        public long SubmittedFrames { get; }
        public long ProcessedFrames { get; }
        public long DroppedFrames { get; }
    }
}
