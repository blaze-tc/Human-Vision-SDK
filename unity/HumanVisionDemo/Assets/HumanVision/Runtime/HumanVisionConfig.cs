using System;
using System.Runtime.InteropServices;
using System.Text;
using HumanVision.Interop;
using UnityEngine;

namespace HumanVision
{
    [Serializable]
    public sealed class HumanVisionConfig
    {
        [Min(1)] public int MaxBodies = 4;
        [Range(0f, 1f)] public float DetectionThreshold = 0.35f;
        [Range(0f, 1f)] public float PoseThreshold = 0.30f;
        [Min(1)] public int DetectionInterval = 1;
        public bool EnableTracking = true;
        public bool UseHardwareAcceleration = true;
        public string DetectorModelPath;
        public string PoseModelPath;

        public void Validate()
        {
            if (MaxBodies < 1)
            {
                throw new ArgumentException("MaxBodies must be at least 1.", nameof(MaxBodies));
            }

            ValidateThreshold(DetectionThreshold, nameof(DetectionThreshold));
            ValidateThreshold(PoseThreshold, nameof(PoseThreshold));

            if (DetectionInterval < 1)
            {
                throw new ArgumentException("DetectionInterval must be at least 1.", nameof(DetectionInterval));
            }

            if (string.IsNullOrWhiteSpace(DetectorModelPath))
            {
                throw new ArgumentException("DetectorModelPath is required.", nameof(DetectorModelPath));
            }

            if (string.IsNullOrWhiteSpace(PoseModelPath))
            {
                throw new ArgumentException("PoseModelPath is required.", nameof(PoseModelPath));
            }
        }

        public HumanVisionConfig Clone()
        {
            return (HumanVisionConfig)MemberwiseClone();
        }

        internal NativeConfigLease CreateNativeLease()
        {
            Validate();
            return new NativeConfigLease(this);
        }

        private static void ValidateThreshold(float value, string fieldName)
        {
            if (float.IsNaN(value) || float.IsInfinity(value) || value < 0f || value > 1f)
            {
                throw new ArgumentException(fieldName + " must be finite and within [0, 1].", fieldName);
            }
        }
    }

    internal sealed class NativeConfigLease : IDisposable
    {
        private IntPtr _detectorPath;
        private IntPtr _posePath;

        internal NativeConfigLease(HumanVisionConfig config)
        {
            _detectorPath = AllocateUtf8(config.DetectorModelPath);
            _posePath = AllocateUtf8(config.PoseModelPath);
            Value = new HVConfigNative
            {
                StructSize = NativeBindings.ConfigSize,
                MaxBodies = config.MaxBodies,
                DetectionThreshold = config.DetectionThreshold,
                PoseThreshold = config.PoseThreshold,
                DetectionInterval = config.DetectionInterval,
                EnableTracking = config.EnableTracking ? 1 : 0,
                Backend = config.UseHardwareAcceleration ? HVBackend.Auto : HVBackend.OnnxCpu,
                DetectorModelPathUtf8 = _detectorPath,
                PoseModelPathUtf8 = _posePath
            };
        }

        internal HVConfigNative Value;

        private static IntPtr AllocateUtf8(string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value + '\0');
            IntPtr pointer = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, pointer, bytes.Length);
            return pointer;
        }

        public void Dispose()
        {
            if (_detectorPath != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_detectorPath);
                _detectorPath = IntPtr.Zero;
            }

            if (_posePath != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_posePath);
                _posePath = IntPtr.Zero;
            }
        }
    }
}
