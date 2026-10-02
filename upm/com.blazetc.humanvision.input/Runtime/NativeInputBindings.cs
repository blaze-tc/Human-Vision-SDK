using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("HumanVision.Input.Tests")]
[assembly: InternalsVisibleTo("HumanVision.Input.PlayMode.Tests")]

namespace HumanVision.Input
{
    internal static class NativeInputBindings
    {
        private const string Library = "humanvision_input";
        [StructLayout(LayoutKind.Sequential)] internal struct Options
        {
            public uint Size, Version;
            public IntPtr Url;
            public uint MaxWidth, MaxHeight, TimeoutMs, ReconnectDelayMs, TransportTcp;
        }
        [StructLayout(LayoutKind.Sequential)] internal struct FrameInfo
        {
            public uint Size, Version;
            public ulong Sequence, Generation;
            public uint Width, Height, StrideBytes, RgbaBytes;
            public long ReceivedTimestampUs, DecodedTimestampUs, PresentationTimestampUs;
            public ulong ClockId;
            public uint ClockDomain, TimestampKind, PtsValid, RowOrigin, ColorSpace, DecodeMode;
            public static FrameInfo Create() => new FrameInfo { Size = 96, Version = 1 };
        }
        [StructLayout(LayoutKind.Sequential)] internal struct ClockInfo
        {
            public uint Size, Version;
            public ulong ClockId;
            public long NowUs, OriginTicks, TicksPerSecond;
            public uint ClockDomain, Reserved;
            public static ClockInfo Create() => new ClockInfo { Size = 48, Version = 1 };
        }
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_QueryClock(ref ClockInfo info);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_Open(ref Options options, out IntPtr handle);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_Close(IntPtr handle);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_Release(IntPtr handle);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_GetState(IntPtr handle, out uint state);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_GetLastError(IntPtr handle, [Out] byte[] utf8, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_PollFrame(IntPtr handle, ulong afterSequence, ref FrameInfo info);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int HV_Input_CopyRgba(IntPtr handle, ulong sequence, IntPtr rgba, uint capacity, ref FrameInfo info);
        internal static IntPtr Utf8(string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value + "\0");
            var pointer = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, pointer, bytes.Length);
            return pointer;
        }
    }
}
