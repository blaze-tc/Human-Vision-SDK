using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;
using HumanVision.Interop;

namespace HumanVision
{
    internal interface IAndroidFrameSubmission
    {
        bool SubmitGpuFrame(Texture texture, long timestampUs, int rotationDegrees, bool mirrored);
        bool SubmitCpuFrame(Texture texture, long timestampUs);
    }

    internal static class HumanVisionAndroidFrameRoute
    {
        internal enum FramePath { Gpu, Cpu }
        internal static bool UsesGpu(string profile) => profile == "android-ncnn-vulkan";

        internal static FramePath Select(string profile)
        {
            if (UsesGpu(profile)) return FramePath.Gpu;
            if (profile == "android-ort-xnnpack" || profile == "android-ort-cpu") return FramePath.Cpu;
            throw new InvalidOperationException("Unsupported Android runtime profile '" + profile + "'. Select a mode in Project Settings > Human Vision > Android Runtime.");
        }

        internal static bool Submit(string profile, IAndroidFrameSubmission target, Texture texture,
            long timestampUs, int rotationDegrees, bool mirrored)
        {
            if (target == null) throw new ArgumentNullException(nameof(target));
            return Select(profile) == FramePath.Gpu
                ? target.SubmitGpuFrame(texture, timestampUs, rotationDegrees, mirrored)
                : target.SubmitCpuFrame(texture, timestampUs);
        }

    }

    internal static class HumanVisionAndroidGpuResult
    {
        internal const int NoNewResult = 1;
        internal static bool IsPressureDrop(int result) => result == NoNewResult;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct AndroidGpuSubmissionNative
    {
        internal uint Size, Version;
        internal IntPtr Texture;
        internal int Width, Height;
        internal long FrameId, TimestampUs;
        internal uint RotationDegrees, Mirrored;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal unsafe struct AndroidGpuBridgeStatusNative
    {
        internal uint Size, Version, CopyPath, AhbFormat;
        internal ulong AhbUsage, AhbFormatFeatures;
        internal ulong SubmittedFrames, ImportedFrames, DroppedNoSlot, DroppedGeneration;
        internal fixed byte UnityDeviceUuid[16], NcnnDeviceUuid[16], UnityDriverUuid[16], NcnnDriverUuid[16];

        internal bool DeviceMatches
        {
            get
            {
                fixed (byte* unity = UnityDeviceUuid, ncnn = NcnnDeviceUuid)
                {
                    bool nonzero = false;
                    for (int i = 0; i < 16; i++) { if (unity[i] != ncnn[i]) return false; nonzero |= unity[i] != 0; }
                    return nonzero;
                }
            }
        }
        internal bool DriverMatches
        {
            get
            {
                fixed (byte* unity = UnityDriverUuid, ncnn = NcnnDriverUuid)
                {
                    bool nonzero = false;
                    for (int i = 0; i < 16; i++) { if (unity[i] != ncnn[i]) return false; nonzero |= unity[i] != 0; }
                    return nonzero;
                }
            }
        }
    }

    internal sealed class HumanVisionAndroidGpuFrameBridge
    {
        private const uint Version = 1;
        private readonly IntPtr _runtime;
        private readonly IntPtr _renderEvent;
        private readonly CommandBuffer _commands;
        private IntPtr _leasedTexture;
        internal HumanVisionAndroidGpuFrameBridge(IntPtr runtime)
        {
            _runtime = runtime;
            if (Application.platform == RuntimePlatform.Android && SystemInfo.graphicsDeviceType != GraphicsDeviceType.Vulkan)
                throw new InvalidOperationException("android-ncnn-vulkan requires Vulkan. Rebuild with Vulkan first in Project Settings.");
            _renderEvent = RuntimeBindings.HV_GetAndroidGpuRenderEventAndDataFunction();
            if (_renderEvent == IntPtr.Zero)
                throw new InvalidOperationException("android-ncnn-vulkan GPU render bridge is unavailable. Rebuild the Android ARM64 native plugin with Vulkan support.");
            if (Marshal.SizeOf<AndroidGpuSubmissionNative>() != 48 || Marshal.SizeOf<AndroidGpuBridgeStatusNative>() != 128)
                throw new InvalidOperationException("Android GPU bridge ABI layout mismatch.");
            _commands = new CommandBuffer { name = "HumanVision Android GPU frame" };
        }

        internal void Begin(RenderTexture texture)
        {
            if (texture == null || !texture.IsCreated()) throw new ArgumentException("GPU source texture must be created.");
            IntPtr pointer = texture.GetNativeTexturePtr();
            if (pointer == IntPtr.Zero) throw new InvalidOperationException("Unity returned a null Vulkan texture pointer.");
            if (_leasedTexture == pointer) return;
            End();
            Check(RuntimeBindings.HV_RuntimeBeginAndroidGpuSourceLease(_runtime, pointer), "begin GPU source lease");
            _leasedTexture = pointer;
        }

        internal void End()
        {
            if (_leasedTexture == IntPtr.Zero) return;
            Check(RuntimeBindings.HV_RuntimeEndAndroidGpuSourceLease(_runtime), "end GPU source lease and drain");
            _leasedTexture = IntPtr.Zero;
        }

        internal bool Submit(RenderTexture texture, int rotationDegrees, bool mirrored, long frameId, long timestampUs)
        {
            if (texture == null || _leasedTexture == IntPtr.Zero) throw new InvalidOperationException("GPU source lease is not active.");
            var submission = new AndroidGpuSubmissionNative {
                Size = 48, Version = Version, Texture = _leasedTexture,
                Width = texture.width, Height = texture.height,
                FrameId = frameId, TimestampUs = timestampUs,
                RotationDegrees = (uint)rotationDegrees, Mirrored = mirrored ? 1u : 0u
            };
            int result = RuntimeBindings.HV_RuntimePrepareAndroidGpuFrame(_runtime, ref submission, out IntPtr eventData);
            if (HumanVisionAndroidGpuResult.IsPressureDrop(result)) return false; // Bounded bridge pressure, counted natively.
            Check(result, "prepare GPU camera frame");
            if (eventData == IntPtr.Zero) throw new InvalidOperationException("GPU frame prepare returned no render event data.");
            _commands.Clear();
            _commands.IssuePluginEventAndData(_renderEvent, 0, eventData);
            Graphics.ExecuteCommandBuffer(_commands);
            return true;
        }

        internal string Diagnostics
        {
            get
            {
                var status = new AndroidGpuBridgeStatusNative { Size = 128, Version = Version };
                Check(RuntimeBindings.HV_RuntimeGetAndroidGpuBridgeStatus(_runtime, ref status), "GPU bridge status");
                string path = status.CopyPath == 1 ? "blit" : status.CopyPath == 2 ? "color attachment" : "unavailable";
                return "Android mode: android-ncnn-vulkan; GPU copy path: " + path +
                    "; AHB format: " + status.AhbFormat + "; usage: 0x" + status.AhbUsage.ToString("X") +
                    "; features: 0x" + status.AhbFormatFeatures.ToString("X") +
                    "; bridge submitted/imported: " + status.SubmittedFrames + "/" + status.ImportedFrames +
                    "; no-slot/generation drops: " + status.DroppedNoSlot + "/" + status.DroppedGeneration +
                    "; device/driver UUID match: " + status.DeviceMatches + "/" + status.DriverMatches;
            }
        }

        private void Check(int result, string operation)
        {
            if (result == 0) return;
            var error = new System.Text.StringBuilder(1024);
            RuntimeBindings.HV_RuntimeGetError(_runtime, error, 1024);
            throw new HumanVisionException(operation, result, error.Length == 0 ? "Android GPU bridge failed." : error.ToString());
        }
        internal void Dispose() { End(); _commands.Release(); }
    }
}
