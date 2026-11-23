using System;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

namespace HumanVision.Input
{
    /// <summary>Resolves the already installed input plugin for a native composition owner.</summary>
    public static class NativeInputRuntimeLibrary
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern uint GetModuleFileNameW(IntPtr module, StringBuilder path, uint capacity);
        /// <summary>Loads only the plugin/clock, not a device or stream. Call from the Unity control thread.</summary>
        public static string ResolvePath()
        {
            if (Application.platform == RuntimePlatform.Android) return "libhumanvision_input.so";
            if (Application.platform != RuntimePlatform.WindowsEditor && Application.platform != RuntimePlatform.WindowsPlayer)
                throw new NotSupportedException("Independent native input is currently available on Windows and Android.");
            var clock = NativeInputBindings.ClockInfo.Create();
            if (NativeInputBindings.HV_Input_QueryClock(ref clock) != 0) throw new InvalidOperationException("Native Input clock is unavailable.");
            IntPtr module = GetModuleHandleW("humanvision_input.dll");
            var path = new StringBuilder(32768);
            uint length = module == IntPtr.Zero ? 0 : GetModuleFileNameW(module, path, (uint)path.Capacity);
            if (length == 0 || length >= path.Capacity) throw new InvalidOperationException("The loaded native Input DLL path could not be resolved.");
            return path.ToString();
        }
    }
}
