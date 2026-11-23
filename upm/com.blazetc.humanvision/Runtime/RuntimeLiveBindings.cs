using System;
using System.Runtime.InteropServices;
using System.Text;
namespace HumanVision.Interop
{
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeSourceProgressNative
    {
        internal uint Size, Version;
        internal ulong SourceId, Generation, ConfigEpoch, DecodedFrames, AcquiredFrames, LivePublishedFrames,
            AnalysisPublishedFrames, AnalysisClaimedFrames, LiveDroppedFrames;
        internal long TimestampUs;
        internal uint State, SourceWidth, SourceHeight, LiveWidth, LiveHeight, LiveEnabled, LiveAvailable, Reserved;
        internal HumanVisionSourceProgress Managed => new HumanVisionSourceProgress {
            SourceId = SourceId, Generation = Generation, ConfigEpoch = ConfigEpoch, TimestampUs = TimestampUs,
            DecodedFrames = DecodedFrames, AcquiredFrames = AcquiredFrames, LivePublishedFrames = LivePublishedFrames,
            AnalysisPublishedFrames = AnalysisPublishedFrames, AnalysisClaimedFrames = AnalysisClaimedFrames, LiveDroppedFrames = LiveDroppedFrames,
            State = State, SourceWidth = SourceWidth, SourceHeight = SourceHeight, LiveWidth = LiveWidth, LiveHeight = LiveHeight,
            Streaming = State == 2 && SourceId != 0 && Generation != 0 && DecodedFrames > 0,
            LiveEnabled = LiveEnabled != 0, LiveAvailable = LiveAvailable != 0
        };
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveConfigNative { internal uint Size, Version, MaxWidth, MaxHeight, RotationDegrees, MirrorX, Enabled, Reserved; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveGeometryNative {
        internal uint Size, Version; internal ulong Generation;
        internal uint SourceWidth, SourceHeight, Width, Height, RotationDegrees, MirrorX;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveTargetsNative {
        internal uint Size, Version; internal IntPtr Texture0, Texture1, Texture2;
        internal uint Width, Height; internal ulong TargetGeneration;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveImageNative {
        internal uint Size, Version; internal ulong Sequence, Generation;
        internal uint Width, Height, StrideBytes, RgbaBytes;
        internal long ReceivedTimestampUs, DecodedTimestampUs, PresentationTimestampUs;
        internal ulong ClockId; internal uint ClockDomain, TimestampKind, PtsValid, RowOrigin, ColorSpace, DecodeMode;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveFrameNative {
        internal uint Size, Version; internal RuntimeLiveImageNative Frame;
        internal ulong TargetGeneration; internal uint Slot, SourceWidth, SourceHeight;
        internal int CropLeft, CropTop, CropRight, CropBottom; internal uint RotationDegrees, MirrorX;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveBindingNative {
        internal uint Size, Version; internal ulong SourceId, ConfigEpoch;
        internal long ClockOffsetUs; internal ulong ClockId; internal IntPtr Consumer, Api;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct RuntimeLiveTableNative {
        internal uint Size, Version;
        internal IntPtr Configure, GetGeometry, BindTargets, PrepareRenderEvent, GetRenderEventFunc,
            Poll, ReleaseSlot, RetireTargets, TargetsRetired, Close, TryRelease, GetLastError;
    }
    internal sealed class RuntimeLiveBindings
    {
        internal const int RenderEventId = 0x48564c;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int ConfigFn(IntPtr h, ref RuntimeLiveConfigNative value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int GeometryFn(IntPtr h, ref RuntimeLiveGeometryNative value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int BindFn(IntPtr h, ref RuntimeLiveTargetsNative value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int HandleFn(IntPtr h);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate IntPtr EventFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int PollFn(IntPtr h, ulong after, ref RuntimeLiveFrameNative value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int SlotFn(IntPtr h, uint slot, ulong generation, ulong sequence);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int ErrorFn(IntPtr h, StringBuilder text, uint capacity);
        internal readonly ConfigFn Configure;
        internal readonly GeometryFn GetGeometry;
        internal readonly BindFn BindTargets;
        internal readonly HandleFn PrepareRenderEvent, RetireTargets, TargetsRetired, Close, TryRelease;
        internal readonly EventFn GetRenderEventFunc;
        internal readonly PollFn Poll;
        internal readonly SlotFn ReleaseSlot;
        internal readonly ErrorFn GetLastError;
        internal RuntimeLiveBindings(IntPtr pointer) {
            var table = Marshal.PtrToStructure<RuntimeLiveTableNative>(pointer);
            if (table.Version != 1 || table.Size < Marshal.SizeOf<RuntimeLiveTableNative>()) throw new NotSupportedException("Native realtime preview contract is incompatible.");
            Configure = Function<ConfigFn>(table.Configure); GetGeometry = Function<GeometryFn>(table.GetGeometry);
            BindTargets = Function<BindFn>(table.BindTargets); PrepareRenderEvent = Function<HandleFn>(table.PrepareRenderEvent);
            GetRenderEventFunc = Function<EventFn>(table.GetRenderEventFunc); Poll = Function<PollFn>(table.Poll);
            ReleaseSlot = Function<SlotFn>(table.ReleaseSlot); RetireTargets = Function<HandleFn>(table.RetireTargets);
            TargetsRetired = Function<HandleFn>(table.TargetsRetired); Close = Function<HandleFn>(table.Close);
            TryRelease = Function<HandleFn>(table.TryRelease); GetLastError = Function<ErrorFn>(table.GetLastError);
        }
        private static T Function<T>(IntPtr pointer) where T : Delegate => pointer != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<T>(pointer) : throw new NotSupportedException("Native realtime preview function is missing.");
    }
}
