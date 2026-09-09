using System;
using System.Runtime.InteropServices;

namespace HumanVision.Interop
{
    internal enum HVResult
    {
        Ok = 0,
        NoNewResult = 1,
        InvalidArgument = -1,
        NotInitialized = -2,
        ModelLoad = -3,
        UnsupportedFormat = -4,
        Internal = -5
    }

    internal enum HVBackend
    {
        Auto = 0,
        OnnxCpu = 1
    }

    internal enum HVPixelFormat
    {
        Rgba32 = 1,
        Bgra32 = 2,
        Rgb24 = 3,
        Bgr24 = 4
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVConfigNative
    {
        public int StructSize;
        public int MaxBodies;
        public float DetectionThreshold;
        public float PoseThreshold;
        public int DetectionInterval;
        public int EnableTracking;
        public HVBackend Backend;
        public IntPtr DetectorModelPathUtf8;
        public IntPtr PoseModelPathUtf8;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVVideoFrameNative
    {
        public int StructSize;
        public int Width;
        public int Height;
        public int StrideBytes;
        public HVPixelFormat PixelFormat;
        public long FrameId;
        public long TimestampUs;
        public IntPtr Data;
        public int DataBytes;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVJointNative
    {
        public float X;
        public float Y;
        public float NormalizedX;
        public float NormalizedY;
        public float Confidence;
        public byte Valid;
        public byte Reserved0;
        public byte Reserved1;
        public byte Reserved2;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVRectNative
    {
        public float X;
        public float Y;
        public float Width;
        public float Height;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVBodyNative
    {
        public int StructSize;
        public int TrackId;
        public HVRectNative BoundingBox;
        public float DetectionConfidence;
        public HVJointNative Joint0;
        public HVJointNative Joint1;
        public HVJointNative Joint2;
        public HVJointNative Joint3;
        public HVJointNative Joint4;
        public HVJointNative Joint5;
        public HVJointNative Joint6;
        public HVJointNative Joint7;
        public HVJointNative Joint8;
        public HVJointNative Joint9;
        public HVJointNative Joint10;
        public HVJointNative Joint11;
        public HVJointNative Joint12;
        public HVJointNative Joint13;
        public HVJointNative Joint14;
        public HVJointNative Joint15;
        public HVJointNative Joint16;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVResultMetaNative
    {
        public int StructSize;
        public long ResultSequence;
        public long SourceFrameId;
        public long SourceTimestampUs;
        public int BodyCount;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct HVStatsNative
    {
        public int StructSize;
        public float InputFps;
        public float InferenceFps;
        public float DetectionMs;
        public float PoseMs;
        public float TrackingMs;
        public float TotalMs;
        public long SubmittedFrames;
        public long ProcessedFrames;
        public long DroppedFrames;
    }

    internal static class NativeBindings
    {
        internal const string LibraryName = "humanvision";
        internal static readonly int ConfigSize = Marshal.SizeOf<HVConfigNative>();
        internal static readonly int VideoFrameSize = Marshal.SizeOf<HVVideoFrameNative>();
        internal static readonly int BodySize = Marshal.SizeOf<HVBodyNative>();
        internal static readonly int ResultMetaSize = Marshal.SizeOf<HVResultMetaNative>();
        internal static readonly int StatsSize = Marshal.SizeOf<HVStatsNative>();

        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl)]
        internal static extern HVResult HV_GetHandJoints(IntPtr handle, long sequence, IntPtr joints, int capacity);

        [DllImport(LibraryName, EntryPoint = nameof(HV_GetVersionString),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr HV_GetVersionString();

        [DllImport(LibraryName, EntryPoint = nameof(HV_Create),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern HVResult HV_Create(ref HVConfigNative config, out IntPtr handle);

        [DllImport(LibraryName, EntryPoint = nameof(HV_Reconfigure),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern HVResult HV_Reconfigure(IntPtr handle, ref HVConfigNative config);

        [DllImport(LibraryName, EntryPoint = nameof(HV_SubmitFrame),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern HVResult HV_SubmitFrame(IntPtr handle, ref HVVideoFrameNative frame);

        [DllImport(LibraryName, EntryPoint = nameof(HV_GetLatestResultMeta),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern HVResult HV_GetLatestResultMeta(IntPtr handle, ref HVResultMetaNative metadata);

        [DllImport(LibraryName, EntryPoint = nameof(HV_GetBodyCount),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int HV_GetBodyCount(IntPtr handle);

        [DllImport(LibraryName, EntryPoint = nameof(HV_GetBodies),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern HVResult HV_GetBodies(
            IntPtr handle,
            IntPtr bodies,
            int capacity,
            out int written);

        [DllImport(LibraryName, EntryPoint = nameof(HV_GetStats),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern HVResult HV_GetStats(IntPtr handle, ref HVStatsNative stats);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern HVResult HV_SetRegions(IntPtr handle, [In] HVRectNative[] regions, int count, long revision);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern HVResult HV_GetRegionAssignments(IntPtr handle, long sequence,
            [Out] int[] indices, int capacity, out long revision);

        [DllImport(LibraryName, EntryPoint = nameof(HV_GetLastError),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr HV_GetLastError(IntPtr handle);

        [DllImport(LibraryName, EntryPoint = nameof(HV_Destroy),
            CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void HV_Destroy(IntPtr handle);
    }
}
