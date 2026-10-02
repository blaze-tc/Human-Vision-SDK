using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace HumanVision
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct AndroidGpuSourceRetirementNative
    {
        internal uint Size, Version;
        internal ulong Generation, CopyToken;
    }

    // A main-thread pollable fence for the source GPU read, independently of
    // the immutable AHB held by inference. Keeps a strong texture reference.
    public sealed class HumanVisionAndroidSourceRetirement
    {
        [DllImport("humanvision", EntryPoint = "HV_AndroidGpuPollSourceRetirement")]
        private static extern int PollSourceRetirement(ref AndroidGpuSourceRetirementNative token);
        private AndroidGpuSourceRetirementNative _token;
        private RenderTexture _source;
        internal HumanVisionAndroidSourceRetirement(AndroidGpuSourceRetirementNative token, RenderTexture source)
        { _token = token; _source = source; }
        public bool IsComplete
        {
            get
            {
                if (ReferenceEquals(_source, null)) return true;
                int result = PollSourceRetirement(ref _token);
                if (result == 1) return false;
                if (result != 0) throw new HumanVisionException("poll GPU source retirement", result, "Source copy completion is unproven; retain the texture.");
                _source = null;
                return true;
            }
        }
    }
}
