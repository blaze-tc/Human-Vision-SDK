using System;
using System.Runtime.InteropServices;

namespace HumanVision.Interop
{
    internal interface IHumanVisionNativeApi
    {
        HVResult Create(ref HVConfigNative config, out IntPtr handle);
        HVResult Reconfigure(IntPtr handle, ref HVConfigNative config);
        HVResult SubmitFrame(IntPtr handle, ref HVVideoFrameNative frame);
        HVResult GetLatestResultMeta(IntPtr handle, ref HVResultMetaNative metadata);
        HVResult GetBodies(IntPtr handle, IntPtr bodies, int capacity, out int written);
        HVResult GetStats(IntPtr handle, ref HVStatsNative stats);
        string GetLastError(IntPtr handle);
        void Destroy(IntPtr handle);
    }

    internal interface IHumanVisionHandNativeApi
    {
        HVResult GetHandJoints(IntPtr handle, long sequence, IntPtr joints, int capacity);
    }

    internal sealed class PInvokeHumanVisionNativeApi : IHumanVisionNativeApi, IHumanVisionHandNativeApi
    {
        internal static readonly PInvokeHumanVisionNativeApi Instance = new PInvokeHumanVisionNativeApi();

        private PInvokeHumanVisionNativeApi()
        {
        }

        public HVResult Create(ref HVConfigNative config, out IntPtr handle)
        {
            return NativeBindings.HV_Create(ref config, out handle);
        }

        public HVResult Reconfigure(IntPtr handle, ref HVConfigNative config)
        {
            return NativeBindings.HV_Reconfigure(handle, ref config);
        }

        public HVResult SubmitFrame(IntPtr handle, ref HVVideoFrameNative frame)
        {
            return NativeBindings.HV_SubmitFrame(handle, ref frame);
        }

        public HVResult GetLatestResultMeta(IntPtr handle, ref HVResultMetaNative metadata)
        {
            return NativeBindings.HV_GetLatestResultMeta(handle, ref metadata);
        }

        public HVResult GetBodies(IntPtr handle, IntPtr bodies, int capacity, out int written)
        {
            return NativeBindings.HV_GetBodies(handle, bodies, capacity, out written);
        }

        public HVResult GetHandJoints(IntPtr handle, long sequence, IntPtr joints, int capacity) =>
            NativeBindings.HV_GetHandJoints(handle, sequence, joints, capacity);

        public HVResult GetStats(IntPtr handle, ref HVStatsNative stats)
        {
            return NativeBindings.HV_GetStats(handle, ref stats);
        }

        public string GetLastError(IntPtr handle)
        {
            IntPtr message = NativeBindings.HV_GetLastError(handle);
            return message == IntPtr.Zero ? string.Empty : Marshal.PtrToStringAnsi(message);
        }

        public void Destroy(IntPtr handle)
        {
            NativeBindings.HV_Destroy(handle);
        }
    }
}
