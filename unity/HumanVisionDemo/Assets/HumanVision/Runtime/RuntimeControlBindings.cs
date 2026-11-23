using System;
using System.Runtime.InteropServices;
using System.Text;

namespace HumanVision.Interop
{
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeControllerConfigNative
    { internal uint Size, Version; internal RuntimeConfigNative Runtime; internal uint ExecutionPolicy, RetirementTimeoutMs; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeSourceNative
    {
        internal uint Size, Version, Kind, Flags;
        internal IntPtr Library, Url, HardwareCodec;
        internal ulong SourceId;
        internal uint Width, Height, TimeoutMs, ReconnectDelayMs, TransportTcp, RotationDegrees, MirrorX, Reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal unsafe struct RuntimeRequestStatusNative
    {
        internal uint Size, Version; internal ulong RequestId;
        internal uint Status, State; internal int Result; internal uint Reserved;
        internal fixed byte Message[512];
        internal string Error { get { fixed (byte* text = Message) return RuntimeControlBindings.Text(text, 512); } }
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal unsafe struct RuntimeCapabilitiesNative
    {
        internal uint Size, Version, Level, UsesGpuImageService;
        internal fixed byte Source[128], ImageService[128], Backend[128], FallbackReason[512];
        internal string SourceText { get { fixed (byte* text = Source) return RuntimeControlBindings.Text(text, 128); } }
        internal string ImageServiceText { get { fixed (byte* text = ImageService) return RuntimeControlBindings.Text(text, 128); } }
        internal string BackendText { get { fixed (byte* text = Backend) return RuntimeControlBindings.Text(text, 128); } }
        internal string FallbackText { get { fixed (byte* text = FallbackReason) return RuntimeControlBindings.Text(text, 512); } }
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeStatsV3Native
    {
        internal uint Size, Version, State, CapabilityLevel;
        internal ulong ConfigEpoch, SourceId, Generation, FrameId, NativeInputFrames, InputErrors, RuntimeErrors, JournalOverruns;
        internal long ClockOffsetUs, ClockUncertaintyUs;
        internal RuntimeStatsV2Native Runtime;
        internal uint SourceWidth, SourceHeight, AnalysisWidth, AnalysisHeight;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimePreviewNative
    {
        internal uint Size, Version, Width, Height, StrideBytes, RgbaBytes;
        internal ulong SourceId, Generation, FrameId, ConfigEpoch;
        internal long TimestampUs;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeObservationHeaderNative
    {
        internal uint Size, Version, Kind, BodyCount;
        internal ulong Sequence, SourceId, Generation, ConfigEpoch;
        internal long FrameId, RegionRevision, BodySequence, HandSequence, PublicationTimestampUs;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeObservationReadNative
    {
        internal uint Size, Version, Written, Reserved;
        internal ulong NextCursor, LostCount;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeControlTableNative
    {
        internal uint Size, Version;
        internal IntPtr CreateDeferred, RequestStart, RequestSource, RequestPause, RequestResume, RequestStop,
            RequestReconfigure, RequestClose, TryRelease, Copy, GetRequestStatus, GetCapabilities, GetStatsV3,
            ReadObservations, Submit, SetRegions, GetDiagnostics, CopyPreview, RequestSnapshot, CopySnapshot;
    }
    internal sealed class RuntimeControlBindings
    {
        internal const int Ok = 0, Busy = 1, NoData = 2, Unsupported = 3, Timeout = 4, BufferTooSmall = 5;
        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl)]
        private static extern int HV_QueryRuntimeControlApiV1(uint version, ref RuntimeControlTableNative table);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int CreateFn(ref RuntimeControllerConfigNative config, out IntPtr handle, StringBuilder error, uint capacity);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int RequestFn(IntPtr handle, out ulong request);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int SourceFn(IntPtr handle, ref RuntimeSourceNative source, out ulong request);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int ReleaseFn(IntPtr handle);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int CopyFn(IntPtr handle, long sampleTime, IntPtr bodies, uint capacity, out uint written, ref RuntimeStatsNative stats);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int StatusFn(IntPtr handle, ulong request, ref RuntimeRequestStatusNative status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int CapsFn(IntPtr handle, ref RuntimeCapabilitiesNative capabilities);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int StatsFn(IntPtr handle, ref RuntimeStatsV3Native stats);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int SubmitFn(IntPtr handle, ref HVVideoFrameNative frame);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int RegionsFn(IntPtr handle, [In] HVRectNative[] regions, uint count, long revision);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int DiagnosticsFn(IntPtr handle, StringBuilder text, uint capacity);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int PreviewFn(IntPtr handle, ulong after, IntPtr rgba, uint capacity, ref RuntimePreviewNative info);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int ObservationsFn(IntPtr handle, ulong after, IntPtr entries, uint capacity, ref RuntimeObservationReadNative read);
        internal readonly CreateFn CreateDeferred;
        internal readonly RequestFn RequestStart, RequestClose, RequestSnapshot;
        internal readonly SourceFn RequestSource;
        internal readonly ReleaseFn TryRelease;
        internal readonly CopyFn Copy;
        internal readonly StatusFn GetRequestStatus;
        internal readonly CapsFn GetCapabilities;
        internal readonly StatsFn GetStatsV3;
        internal readonly SubmitFn Submit;
        internal readonly RegionsFn SetRegions;
        internal readonly DiagnosticsFn GetDiagnostics;
        internal readonly PreviewFn CopyPreview, CopySnapshot;
        internal readonly ObservationsFn ReadObservations;
        private static RuntimeControlBindings instance;
        internal static RuntimeControlBindings Load()
        {
            if (instance != null) return instance;
            var table = new RuntimeControlTableNative { Size = (uint)Marshal.SizeOf<RuntimeControlTableNative>(), Version = 1 };
            int result;
            try { result = HV_QueryRuntimeControlApiV1(1, ref table); }
            catch (EntryPointNotFoundException e) { throw new NotSupportedException("The installed native SDK has no independent runtime API. Install the matching SDK/Input candidate together.", e); }
            if (result != 0 || table.Version != 1 || table.Size < Marshal.SizeOf<RuntimeControlTableNative>())
                throw new NotSupportedException("The native SDK independent runtime contract is incompatible (" + result + ").");
            return instance = new RuntimeControlBindings(table);
        }
        private RuntimeControlBindings(RuntimeControlTableNative t)
        {
            CreateDeferred = Function<CreateFn>(t.CreateDeferred); RequestStart = Function<RequestFn>(t.RequestStart);
            RequestSource = Function<SourceFn>(t.RequestSource); RequestClose = Function<RequestFn>(t.RequestClose);
            TryRelease = Function<ReleaseFn>(t.TryRelease); Copy = Function<CopyFn>(t.Copy);
            GetRequestStatus = Function<StatusFn>(t.GetRequestStatus); GetCapabilities = Function<CapsFn>(t.GetCapabilities);
            GetStatsV3 = Function<StatsFn>(t.GetStatsV3); Submit = Function<SubmitFn>(t.Submit);
            SetRegions = Function<RegionsFn>(t.SetRegions); GetDiagnostics = Function<DiagnosticsFn>(t.GetDiagnostics);
            CopyPreview = Function<PreviewFn>(t.CopyPreview); RequestSnapshot = Function<RequestFn>(t.RequestSnapshot);
            CopySnapshot = Function<PreviewFn>(t.CopySnapshot);
            ReadObservations = Function<ObservationsFn>(t.ReadObservations);
        }
        private static T Function<T>(IntPtr address) where T : Delegate
        {
            if (address == IntPtr.Zero) throw new NotSupportedException("Native runtime control function is missing.");
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        internal static unsafe string Text(byte* text, int capacity)
        { int length = 0; while (length < capacity && text[length] != 0) length++; return Encoding.UTF8.GetString(text, length); }
    }
}
