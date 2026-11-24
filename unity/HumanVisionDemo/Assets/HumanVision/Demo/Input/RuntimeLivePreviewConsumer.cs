using System;
using System.Runtime.InteropServices;
using System.Text;
using HumanVision.Input;
using HumanVision.Interop;
using UnityEngine;
using UnityEngine.Rendering;
namespace HumanVision.Demo
{
    internal sealed class RuntimeLivePreviewConsumer : INativeLiveConsumerApi
    {
        private readonly RuntimeLiveBindings api;
        private readonly HumanVisionManager manager;
        private readonly StringBuilder error = new StringBuilder(1024);
        private IntPtr consumer, renderEvent;
        private string lastError = "";
        private readonly CommandBuffer commands = new CommandBuffer { name = "HumanVision independent live preview" };
        internal readonly ulong SourceId;
        internal RuntimeLivePreviewConsumer(HumanVisionManager owner, RuntimeLiveBindingNative binding) {
            manager = owner; SourceId = binding.SourceId; consumer = binding.Consumer;
            api = new RuntimeLiveBindings(binding.Api); renderEvent = api.GetRenderEventFunc();
            if (renderEvent == IntPtr.Zero) throw new NotSupportedException("Realtime preview render callback is unavailable.");
        }
        public InputSourceState State => manager != null && manager.TryGetSourceProgress(out var source) ? (InputSourceState)source.State : InputSourceState.Stopped;
        public string LastError => lastError;
        public bool TryGetGeometry(out NativeLiveGeometry geometry) {
            geometry = default; var value = new RuntimeLiveGeometryNative { Size = (uint)Marshal.SizeOf<RuntimeLiveGeometryNative>(), Version = 1 };
            int result = api.GetGeometry(consumer, ref value); if (!Ready(result)) return false;
            geometry = new NativeLiveGeometry { Generation = value.Generation, SourceWidth = value.SourceWidth, SourceHeight = value.SourceHeight,
                Width = value.Width, Height = value.Height, RotationDegrees = value.RotationDegrees, MirrorX = value.MirrorX }; return true;
        }
        public int BindTargets(IntPtr[] textures, uint width, uint height, ulong generation) {
            var value = new RuntimeLiveTargetsNative { Size = (uint)Marshal.SizeOf<RuntimeLiveTargetsNative>(), Version = 1,
                Texture0 = textures[0], Texture1 = textures[1], Texture2 = textures[2], Width = width, Height = height, TargetGeneration = generation };
            int status = api.BindTargets(consumer, ref value);
            Ready(status);
            return status;
        }
        public bool TryPoll(ulong after, out NativeLiveTextureFrame frame) {
            frame = default; var value = new RuntimeLiveFrameNative { Size = (uint)Marshal.SizeOf<RuntimeLiveFrameNative>(), Version = 1 };
            if (!Ready(api.Poll(consumer, after, ref value))) return false;
            frame = new NativeLiveTextureFrame { Sequence = value.Frame.Sequence, Generation = value.Frame.Generation,
                TargetGeneration = value.TargetGeneration, Slot = value.Slot, Width = value.Frame.Width, Height = value.Frame.Height,
                SourceWidth = value.SourceWidth, SourceHeight = value.SourceHeight, RotationDegrees = value.RotationDegrees, MirrorX = value.MirrorX,
                RowOrigin = value.Frame.RowOrigin, ColorSpace = value.Frame.ColorSpace, ClockId = value.Frame.ClockId, DecodedTimestampUs = value.Frame.DecodedTimestampUs,
                PtsValid = value.Frame.PtsValid, PresentationTimestampUs = value.Frame.PresentationTimestampUs }; return true;
        }
        public int ReleaseSlot(uint slot, ulong targetGeneration, ulong sequence) => api.ReleaseSlot(consumer, slot, targetGeneration, sequence);
        public int RetireTargets() => consumer == IntPtr.Zero ? 0 : api.RetireTargets(consumer);
        public bool TargetsRetired => consumer == IntPtr.Zero || Ready(api.TargetsRetired(consumer));
        public int Close() => consumer == IntPtr.Zero ? 0 : api.Close(consumer);
        public bool TryRelease() {
            if (consumer == IntPtr.Zero) return true;
            if (!Ready(api.TryRelease(consumer))) return false;
            consumer = IntPtr.Zero; commands.Release(); return true;
        }
        public void PumpRenderEvent() {
            if (consumer == IntPtr.Zero) return;
            if (!Ready(api.PrepareRenderEvent(consumer))) return;
            // Every reserved callback is issued, even while targets/decoder retire.
            commands.Clear(); commands.IssuePluginEventAndData(renderEvent, RuntimeLiveBindings.RenderEventId, consumer);
            Graphics.ExecuteCommandBuffer(commands);
        }
        private bool Ready(int status) {
            if (status == 0) return true;
            if (status == 1 || status == 2) return false;
            error.Length = 0; if (consumer != IntPtr.Zero) api.GetLastError(consumer, error, 1024);
            lastError = error.Length > 0 ? error.ToString() : "Realtime preview operation failed (" + status + ").";
            throw new InvalidOperationException(lastError);
        }
    }
}
