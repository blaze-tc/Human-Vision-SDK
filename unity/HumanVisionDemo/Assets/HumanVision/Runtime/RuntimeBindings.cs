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
        internal static extern void HV_RuntimeDestroy(IntPtr handle);
    }
}
