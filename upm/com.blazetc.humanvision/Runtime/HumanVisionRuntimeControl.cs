using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using HumanVision.Interop;

namespace HumanVision
{
    // This handle is owned only by the new control table. Never pass it to HV_Runtime*.
    internal sealed class HumanVisionRuntimeControl
    {
        private readonly object gate = new object();
        private readonly RuntimeControlBindings api;
        private IntPtr controller;
        private ulong startRequest;
        private bool closeRequested, released;
        private readonly int retirementSlot;
        private long nextCapabilitiesAt;
        private string actualSource, actualImageService, actualBackend, fallbackReason;
        private bool usesGpuImageService;
        private IntPtr observationBuffer;
        private ulong copyCursor, progressCursor;
        private RuntimeObservationHeaderNative copyIdentity, progressIdentity;
        internal RuntimeStatsV3Native LastStats { get; private set; }
        internal bool Released { get { lock (gate) return released; } }
        internal string LastError { get; private set; } = "";
        internal HumanVisionRuntimeControl(HumanVisionConfig config)
        {
            api = RuntimeControlBindings.Load();
            retirementSlot = NativeControllerRetirement.Reserve(this);
            IntPtr root = IntPtr.Zero, profile = IntPtr.Zero, library = IntPtr.Zero, url = IntPtr.Zero, codec = IntPtr.Zero;
            try {
                root = Utf8(config.RuntimeRoot); profile = Utf8(config.Profile);
                var value = new RuntimeControllerConfigNative {
                    Size = (uint)Marshal.SizeOf<RuntimeControllerConfigNative>(), Version = 1,
                    Runtime = new RuntimeConfigNative { Size = (uint)Marshal.SizeOf<RuntimeConfigNative>(), Version = RuntimeBindings.Version,
                        Root = root, Profile = profile, People = config.MaxBodies },
                    ExecutionPolicy = (uint)config.ExecutionPolicy, RetirementTimeoutMs = 5000
                };
                var error = new StringBuilder(2048);
                int result = api.CreateDeferred(ref value, out controller, error, 2048);
                if (result != 0 || controller == IntPtr.Zero) throw new HumanVisionException("deferred runtime creation", result, error.ToString());
                if (config.NativeSource != null) {
                    var source = config.NativeSource;
                    library = Utf8(source.Library); url = Utf8(source.Url); codec = Utf8(source.HardwareCodec);
                    var native = new RuntimeSourceNative {
                        Size = (uint)Marshal.SizeOf<RuntimeSourceNative>(), Version = 1, Kind = 1,
                        Flags = source.Preview ? 1u : 0u, Library = library, Url = url, HardwareCodec = codec,
                        SourceId = source.SourceId, Width = source.Width, Height = source.Height,
                        TimeoutMs = source.TimeoutMs, ReconnectDelayMs = source.ReconnectDelayMs,
                        TransportTcp = source.Tcp ? 1u : 0u, RotationDegrees = source.RotationDegrees, MirrorX = source.Mirror ? 1u : 0u
                    };
                    Check(api.RequestSource(controller, ref native, out _), "native input binding");
                }
                Check(api.RequestStart(controller, out startRequest), "runtime start request");
                observationBuffer = Marshal.AllocHGlobal(88 + 8 * (72 + 32 * 48));
            }
            catch {
                if (controller != IntPtr.Zero) RequestClose();
                else { released = true; NativeControllerRetirement.Remove(retirementSlot, this); }
                throw;
            }
            finally {
                Free(root); Free(profile); Free(library); Free(url); Free(codec);
            }
        }
        private static IntPtr Utf8(string value)
        {
            if (string.IsNullOrEmpty(value)) return IntPtr.Zero;
            var bytes = Encoding.UTF8.GetBytes(value + "\0"); var pointer = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, pointer, bytes.Length); return pointer;
        }
        private static void Free(IntPtr value) { if (value != IntPtr.Zero) Marshal.FreeHGlobal(value); }
        private void Check(int result, string operation)
        {
            if (result == RuntimeControlBindings.Ok) return;
            var error = new StringBuilder(2048);
            if (controller != IntPtr.Zero && !released) api.GetDiagnostics(controller, error, 2048);
            LastError = error.Length == 0 ? "Invalid or unavailable native runtime control state." : error.ToString();
            throw new HumanVisionException(operation, result, LastError);
        }
        internal bool PollStarted(out string error)
        {
            lock (gate) {
                error = LastError;
                if (released || closeRequested) return false;
                var status = new RuntimeRequestStatusNative { Size = (uint)Marshal.SizeOf<RuntimeRequestStatusNative>(), Version = 1 };
                Check(api.GetRequestStatus(controller, startRequest, ref status), "runtime start status");
                if (status.Status == 2) { error = ""; return true; }
                if (status.Status >= 3) error = LastError = status.Error;
                return false;
            }
        }
        internal int Copy(long sampleTime, IntPtr bodies, uint capacity, out uint written, ref RuntimeStatsNative stats)
        {
            lock (gate) {
                written = 0; if (released || closeRequested) return RuntimeControlBindings.NoData;
                int result = api.Copy(controller, sampleTime, bodies, capacity, out written, ref stats);
                if (result < 0) Check(result, "controller snapshot"); return result;
            }
        }
        internal void Submit(ref HVVideoFrameNative frame)
        { lock (gate) { if (released || closeRequested) throw new ObjectDisposedException(nameof(HumanVisionRuntimeControl)); Check(api.Submit(controller, ref frame), "controller input submission"); } }
        internal void SetRegions(HVRectNative[] regions, long revision)
        { lock (gate) { Check(api.SetRegions(controller, regions, (uint)regions.Length, revision), "controller regions"); } }
        internal bool TryGetStats(out RuntimeStatsV3Native stats)
        {
            lock (gate) {
                stats = new RuntimeStatsV3Native { Size = (uint)Marshal.SizeOf<RuntimeStatsV3Native>(), Version = 3 };
                if (released || closeRequested) return false;
                Check(api.GetStatsV3(controller, ref stats), "native progress"); LastStats = stats; return true;
            }
        }
        internal bool TryGetProgress(out HumanVisionRuntimeProgress progress)
        {
            lock (gate) {
                progress = default;
                if (!TryGetStats(out var stats)) return false;
                long now = RuntimeBindings.HV_RuntimeClockUs();
                if (now >= nextCapabilitiesAt || actualSource == null) {
                    var caps = new RuntimeCapabilitiesNative { Size = (uint)Marshal.SizeOf<RuntimeCapabilitiesNative>(), Version = 1 };
                    Check(api.GetCapabilities(controller, ref caps), "native capabilities");
                    actualSource = caps.SourceText; actualImageService = caps.ImageServiceText;
                    actualBackend = SemanticBackend(caps.BackendText); fallbackReason = caps.FallbackText;
                    usesGpuImageService = caps.UsesGpuImageService != 0; nextCapabilitiesAt = now + 250000;
                }
                long capture = stats.Runtime.CaptureTimestampUs;
                var identity = progressIdentity;
                if (stats.Runtime.SourceFrameId > 0 && !FindIdentity(stats.Runtime.SourceFrameId, 0, ref progressCursor, ref identity)) return false;
                if (stats.Runtime.SourceFrameId > 0 && stats.SourceId != 0 &&
                    (identity.SourceId != stats.SourceId || identity.Generation != stats.Generation || identity.ConfigEpoch != stats.ConfigEpoch)) return false;
                progressIdentity = identity;
                progress = new HumanVisionRuntimeProgress {
                    SourceId = stats.Runtime.SourceFrameId > 0 ? identity.SourceId : stats.SourceId,
                    Generation = stats.Runtime.SourceFrameId > 0 ? identity.Generation : stats.Generation,
                    ConfigEpoch = stats.Runtime.SourceFrameId > 0 && stats.SourceId != 0 ? identity.ConfigEpoch : stats.ConfigEpoch,
                    FrameId = stats.Runtime.SourceFrameId, InputFrameId = stats.FrameId,
                    NativeInputFrames = stats.NativeInputFrames, NativeFreshFrames = stats.Runtime.FreshObservationFrames,
                    InputErrors = stats.InputErrors, RuntimeErrors = stats.RuntimeErrors, ResultOverruns = stats.JournalOverruns,
                    ResultAgeMicroseconds = stats.Runtime.FreshObservationFrames > 0 && capture > 0 && now >= capture ? now - capture : -1,
                    ActualSource = actualSource, ActualImageService = actualImageService, ActualBackend = actualBackend,
                    FallbackReason = fallbackReason, UsesGpuImageService = usesGpuImageService, State = stats.State,
                    Streaming = stats.State == 2 && stats.NativeInputFrames > 0,
                    ExecutionMode = stats.CapabilityLevel >= 3 ? "native" : "compatible",
                    CapabilityLevel = stats.CapabilityLevel >= 3 ? "IndependentPipeline" : stats.CapabilityLevel == 2 ? "IndependentInput" : "AsyncProcessing",
                    RegionRevision = identity.RegionRevision
                };
                return true;
            }
        }
        internal bool TryGetCanonicalIdentity(long frameId, long sequence, out RuntimeObservationHeaderNative identity)
        {
            lock (gate) {
                identity = copyIdentity;
                if (released || closeRequested || !FindIdentity(frameId, sequence, ref copyCursor, ref identity)) return false;
                copyIdentity = identity; return true;
            }
        }
        private bool FindIdentity(long frameId, long sequence, ref ulong cursor, ref RuntimeObservationHeaderNative identity)
        {
            if (identity.Kind == 0 && identity.Size != 0 && identity.FrameId == frameId && (sequence == 0 || identity.BodySequence == sequence)) return true;
            for (int i = 0; i < 64; i++) {
                var read = new RuntimeObservationReadNative { Size = (uint)Marshal.SizeOf<RuntimeObservationReadNative>(), Version = 1 };
                int result = api.ReadObservations(controller, cursor, observationBuffer, 1, ref read);
                if (result == RuntimeControlBindings.NoData || read.Written == 0) break;
                Check(result, "canonical identity journal"); cursor = read.NextCursor;
                var entry = Marshal.PtrToStructure<RuntimeObservationHeaderNative>(observationBuffer);
                if (entry.Kind != 0) continue;
                identity = entry;
                if (entry.FrameId == frameId && (sequence == 0 || entry.BodySequence == sequence)) return true;
            }
            return false;
        }
        private static string SemanticBackend(string backend)
        {
            if (string.IsNullOrEmpty(backend) || backend == "uninitialized") return null;
            if (backend.IndexOf("rknn", StringComparison.OrdinalIgnoreCase) >= 0) return "npu";
            if (backend.IndexOf("ncnn", StringComparison.OrdinalIgnoreCase) >= 0 || backend.IndexOf("directml", StringComparison.OrdinalIgnoreCase) >= 0 || backend.IndexOf("dml", StringComparison.OrdinalIgnoreCase) >= 0) return "gpu";
            if (backend.IndexOf("cpu", StringComparison.OrdinalIgnoreCase) >= 0 || backend.IndexOf("xnnpack", StringComparison.OrdinalIgnoreCase) >= 0) return "cpu";
            return null;
        }
        internal string Diagnostics
        {
            get { lock (gate) { if (released) return LastError; var text = new StringBuilder(4096); Check(api.GetDiagnostics(controller, text, 4096), "controller diagnostics"); return text.ToString(); } }
        }
        internal int CopyPreview(ulong after, IntPtr buffer, uint capacity, ref RuntimePreviewNative info)
        { lock (gate) { return released || closeRequested ? RuntimeControlBindings.NoData : api.CopyPreview(controller, after, buffer, capacity, ref info); } }
        internal int RequestSnapshot(out ulong request)
        { lock (gate) { request = 0; return released || closeRequested ? RuntimeControlBindings.NoData : api.RequestSnapshot(controller, out request); } }
        internal int CopySnapshot(ulong request, IntPtr buffer, uint capacity, ref RuntimePreviewNative info)
        { lock (gate) { return released || closeRequested ? RuntimeControlBindings.NoData : api.CopySnapshot(controller, request, buffer, capacity, ref info); } }
        internal void RequestClose()
        {
            lock (gate) {
                if (released || closeRequested) return;
                Check(api.RequestClose(controller, out _), "runtime close request"); closeRequested = true;
            }
        }
        internal bool PollRetired()
        {
            lock (gate) {
                if (released) return true;
                if (!closeRequested) return false;
                int result = api.TryRelease(controller);
                if (result == RuntimeControlBindings.Busy) return false;
                Check(result, "runtime retirement"); controller = IntPtr.Zero; released = true;
                if (observationBuffer != IntPtr.Zero) { Marshal.FreeHGlobal(observationBuffer); observationBuffer = IntPtr.Zero; }
                return true;
            }
        }
    }

    // Emergency disposal also progresses outside the Unity frame loop. The bounded
    // registry keeps native owners alive through main-thread stalls and scene unload.
    // Domain teardown may retain an unretired native owner until process exit; it
    // never forces a driver fence, unloads its library, or frees an uncertain handle.
    internal static class NativeControllerRetirement
    {
        private static readonly object gate = new object();
        private static readonly HumanVisionRuntimeControl[] owners = new HumanVisionRuntimeControl[64];
        private static readonly Timer timer = new Timer(Poll, null, 50, 50);
        internal static void RequestCloseAll()
        {
            lock (gate) foreach (var owner in owners) if (owner != null) {
                try { owner.RequestClose(); } catch { /* Keep ownership when a close request cannot be confirmed. */ }
            }
        }
        internal static int Reserve(HumanVisionRuntimeControl owner)
        {
            lock (gate) {
                for (int i = 0; i < owners.Length; i++) if (owners[i] == null) { owners[i] = owner; return i; }
                throw new InvalidOperationException("Native runtime retirement capacity is exhausted; wait for prior owners or restart after a terminal driver fault.");
            }
        }
        internal static void Remove(int index, HumanVisionRuntimeControl owner)
        { lock (gate) { if (owners[index] == owner) owners[index] = null; } }
        private static void Poll(object state)
        {
            lock (gate) for (int i = 0; i < owners.Length; i++) {
                var owner = owners[i]; if (owner == null) continue;
                try { if (owner.PollRetired()) owners[i] = null; }
                catch { /* Retain ownership. A failed query is never proof of retirement. */ }
            }
        }
    }
}
