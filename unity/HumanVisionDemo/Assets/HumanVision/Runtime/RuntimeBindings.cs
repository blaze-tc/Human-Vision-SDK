using System;
using System.Runtime.InteropServices;
using System.Text;
namespace HumanVision.Interop
{
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeConfigNative
    { public uint Size, Version; public IntPtr Root, Profile; public int People; public uint Reserved; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeStatsNative
    {
        public uint Size, Version;
        public long BodySequence, HandSequence, Frame, Timestamp, Revision, Dropped;
        public float BodyFps, HandFps, PreprocessMs, InferenceMs, PostprocessMs;
        public uint Reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeStatsV2Native
    {
        public uint Size, Version;
        public ulong FreshObservationFrames, OutputSamples;
        public ulong SourceFramesSeen, SourceRateLimitedDrops;
        public ulong GpuCaptureRequested, GpuCaptureSubmitted;
        public ulong GpuCopyErrors, GpuImportErrors;
        public ulong GpuBridgeNoFreeSlotDrops, GpuBridgeSupersededReadyDrops;
        public ulong PoseJobDrops, DetectorAttempted, DetectorCompleted;
        public ulong DetectorLate, DetectorDiscarded, MissedDetectorDeadlines;
        public ulong PoseValidationFailures;
        public long SourceFrameId, CaptureTimestampUs, PublicationTimestampUs;
        public float GpuCaptureFps, FreshObservationFps, OutputSamplingFps;
        public float AgeP50Ms, AgeP95Ms, PoseAgeP50Ms, PoseAgeP95Ms;
        public float SensorCaptureAgeP50Ms, SensorCaptureAgeP95Ms;
        public float ScheduledDetectorFrameAgeP50Ms, ScheduledDetectorFrameAgeP95Ms;
        public float DetectorAgeMs, DetectorCompletionLagMs;
        public float PosePerBodyP50Ms, PosePerBodyP95Ms;
        public uint DetectorIntervalFrames, CopyPath, CaptureProvenance, Reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct CanonicalHeaderNative
    {
        public uint Size, Version; public long TrackId; public int Region, Lifecycle;
        public long Revision, Frame, Timestamp; public HVRectNative Box; public float Confidence; public uint Reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct CanonicalJointNative
    {
        public uint Size, Version; public float X, Y, NX, NY, Confidence;
        public byte Valid, Derived; public ushort Reserved; public long Timestamp;
        public float PredictionMs; public uint Reserved2;
    }
    internal static class RuntimeBindings
    {
        internal const uint Version = 0x00040000;
        private const string Library = "humanvision";
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeCreate(ref RuntimeConfigNative config, out IntPtr handle, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeSubmit(IntPtr handle, ref HVVideoFrameNative frame);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeCopy(IntPtr handle, long sampleTime, IntPtr bodies, uint capacity, out uint written, ref RuntimeStatsNative stats);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeSetRegions(IntPtr handle, [In] HVRectNative[] regions, uint count, long revision);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeGetError(IntPtr handle, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeGetDiagnostics(IntPtr handle, StringBuilder text, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeGetStatsV2(IntPtr handle, ref RuntimeStatsV2Native stats);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeRecordSourceFrameV2(IntPtr handle, uint rateLimited);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeSetCaptureProvenanceV2(IntPtr handle, uint provenance);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimePrepareAndroidGpuFrame(IntPtr handle, ref AndroidGpuSubmissionNative frame, out IntPtr renderEventData);
        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl, EntryPoint = "HV_RuntimePrepareAndroidGpuFrame")]
        internal static extern int HV_RuntimePrepareAndroidGpuFrameClock(IntPtr handle, ref AndroidGpuSubmissionClockNative frame, out IntPtr renderEventData);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern long HV_RuntimeClockUs();
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeBeginAndroidGpuSourceLease(IntPtr handle, IntPtr unityTexture);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeEndAndroidGpuSourceLease(IntPtr handle);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr HV_GetAndroidGpuRenderEventAndDataFunction();
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int HV_RuntimeGetAndroidGpuBridgeStatus(IntPtr handle, ref AndroidGpuBridgeStatusNative status);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void HV_RuntimeDestroy(IntPtr handle);
    }
}
