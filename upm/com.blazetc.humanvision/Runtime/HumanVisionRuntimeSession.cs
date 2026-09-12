using System;
using System.Runtime.InteropServices;
using System.Text;
using HumanVision.Interop;
using UnityEngine;
namespace HumanVision
{
    // All managed copies run on Unity's main thread. Native input, body and hand
    // workers are independent; no inference wait occurs in Submit/Poll.
    internal sealed class HumanVisionRuntimeSession : IHumanVisionSession
    {
        private static readonly int[] LegacyMap = {27,28,30,29,31,5,12,6,13,7,14,18,22,19,23,20,24};
        private static readonly int[] HandMap = {8,9,10,15,16,17};
        private HumanVisionConfig _config;
        private IntPtr _handle, _buffer;
        private RuntimeStatsNative _native;
        private readonly int[] _regions = new int[8];
        private long _lastHand, _submitted;
        private static readonly int HeaderBytes = Marshal.SizeOf<CanonicalHeaderNative>();
        private static readonly int JointBytes = Marshal.SizeOf<CanonicalJointNative>();
        private static readonly int BodyBytes = HeaderBytes + 32 * JointBytes;
        public int MaxBodies => _config.MaxBodies;
        public HumanVisionBody[] Bodies { get; } = AllocateBodies();
        internal HumanVisionBody[] SampledBodies { get; } = AllocateBodies();
        internal int SampledCount { get; private set; }
        public int BodyCount { get; private set; }
        public long ResultSequence => _native.BodySequence;
        public long SourceFrameId => _native.Frame;
        public long SourceTimestampUs => _native.Timestamp;
        public HumanVisionStats Stats { get; private set; }
        internal float HandFps => _native.HandFps;
        internal string Diagnostics {
            get { var text = new StringBuilder(4096); Check(RuntimeBindings.HV_RuntimeGetDiagnostics(_handle, text, 4096), "diagnostics"); return text.ToString(); }
        }
        internal HumanVisionRuntimeSession(HumanVisionConfig config)
        {
            _config = config.Clone(); _config.Validate();
            if (HeaderBytes != 72 || JointBytes != 48) throw new InvalidOperationException("Unsupported canonical ABI layout.");
            _buffer = Marshal.AllocHGlobal(BodyBytes * 8);
            try { _handle = Create(_config); } catch { Dispose(); throw; }
        }
        private static HumanVisionBody[] AllocateBodies()
        { var bodies = new HumanVisionBody[8]; for (int i = 0; i < 8; i++) bodies[i] = new HumanVisionBody(); return bodies; }
        private static IntPtr Utf8(string text)
        { byte[] bytes = Encoding.UTF8.GetBytes(text + "\0"); var ptr = Marshal.AllocHGlobal(bytes.Length); Marshal.Copy(bytes, 0, ptr, bytes.Length); return ptr; }
        private static IntPtr Create(HumanVisionConfig config)
        {
            IntPtr root = Utf8(config.RuntimeRoot), profile = IntPtr.Zero;
            try {
                profile = Utf8(config.Profile);
                var native = new RuntimeConfigNative { Size = (uint)Marshal.SizeOf<RuntimeConfigNative>(), Version = RuntimeBindings.Version, Root = root, Profile = profile, People = config.MaxBodies };
                var error = new StringBuilder(2048);
                int result = RuntimeBindings.HV_RuntimeCreate(ref native, out var handle, error, 2048);
                if (result != 0) throw new HumanVisionException("runtime initialization", result, error.ToString());
                return handle;
            } finally { Marshal.FreeHGlobal(root); if (profile != IntPtr.Zero) Marshal.FreeHGlobal(profile); }
        }
        private void Check(int result, string operation)
        {
            if (result == 0) return;
            var error = new StringBuilder(2048); RuntimeBindings.HV_RuntimeGetError(_handle, error, 2048);
            throw new HumanVisionException(operation, result, error.Length > 0 ? error.ToString() : "Invalid runtime argument or state.");
        }
        public bool SubmitFrame(IntPtr data, int width, int height, int strideBytes, HumanVisionPixelFormat format, long frameId, long timestampUs, int bytes)
        {
            var frame = new HVVideoFrameNative { StructSize = NativeBindings.VideoFrameSize, Data = data, Width = width, Height = height,
                StrideBytes = strideBytes, PixelFormat = (HVPixelFormat)format, FrameId = frameId, TimestampUs = timestampUs, DataBytes = bytes };
            Check(RuntimeBindings.HV_RuntimeSubmit(_handle, ref frame), "frame submission"); _submitted++; return true;
        }
        public bool PollLatestResult()
        {
            long previous = _native.BodySequence;
            _native.Size = (uint)Marshal.SizeOf<RuntimeStatsNative>(); _native.Version = RuntimeBindings.Version;
            Check(RuntimeBindings.HV_RuntimeCopy(_handle, 0, _buffer, 8, out uint count, ref _native), "raw snapshot");
            bool changed = previous != _native.BodySequence || _lastHand != _native.HandSequence;
            BodyCount = (int)count; CopyBodies(Bodies, BodyCount, true); _lastHand = _native.HandSequence;
            var sampledStats = _native;
            Check(RuntimeBindings.HV_RuntimeCopy(_handle, (long)(Time.realtimeSinceStartupAsDouble * 1000000), _buffer, 8, out count, ref sampledStats), "sampled snapshot");
            SampledCount = (int)count; CopyBodies(SampledBodies, SampledCount, false);
            RefreshStats(); return changed;
        }
        private unsafe void CopyBodies(HumanVisionBody[] target, int count, bool assignments)
        {
            for (int i = 0; i < count; i++) {
                byte* address = (byte*)_buffer + i * BodyBytes;
                var header = *(CanonicalHeaderNative*)address; var body = target[i];
                body.StableTrackId = header.TrackId; body.TrackId = checked((int)header.TrackId);
                body.ObservationTimestampUs = header.Timestamp; body.RegionIndex = header.Region;
                body.BoundingBoxPixels = new Rect(header.Box.X, header.Box.Y, header.Box.Width, header.Box.Height); body.DetectionConfidence = header.Confidence;
                if (assignments) _regions[i] = header.Region;
                for (int j = 0; j < 32; j++) {
                    var point = *(CanonicalJointNative*)(address + HeaderBytes + j * JointBytes);
                    var position = new HumanVisionJoint(new Vector2(point.X, point.Y), new Vector2(point.NX, point.NY), point.Confidence, point.Valid != 0, point.Derived != 0);
                    body.CanonicalJoints[j] = new HumanVisionCanonicalJoint(position, point.Timestamp, point.PredictionMs);
                }
                for (int j = 0; j < LegacyMap.Length; j++) body.Joints[j] = body.CanonicalJoints[LegacyMap[j]].Position;
                for (int j = 0; j < HandMap.Length; j++) body.HandJoints[j] = body.CanonicalJoints[HandMap[j]].Position;
            }
        }
        public void SetRegions(Rect[] regions, long revision)
        {
            var native = new HVRectNative[regions.Length];
            for (int i = 0; i < regions.Length; i++) native[i] = new HVRectNative { X=regions[i].x,Y=regions[i].y,Width=regions[i].width,Height=regions[i].height };
            Check(RuntimeBindings.HV_RuntimeSetRegions(_handle, native, (uint)native.Length, revision), "region configuration");
            BodyCount = SampledCount = 0; _native.Revision = revision;
        }
        public bool CopyRegions(long sequence, int[] indices, out long revision)
        { revision = _native.Revision; if (sequence != ResultSequence || indices.Length < BodyCount) return false; Array.Copy(_regions, indices, BodyCount); return true; }
        public void RefreshStats()
        { Stats = new HumanVisionStats(0, _native.BodyFps, _native.PreprocessMs, _native.InferenceMs, _native.PostprocessMs,
            _native.PreprocessMs + _native.InferenceMs + _native.PostprocessMs, _submitted, _native.BodySequence, _native.Dropped); }
        public void ReconfigureMaxBodies(int maxBodies)
        {
            if (maxBodies == MaxBodies) return;
            var updated = _config.Clone(); updated.MaxBodies = maxBodies; updated.Validate();
            IntPtr replacement = Create(updated); RuntimeBindings.HV_RuntimeDestroy(_handle); _handle = replacement; _config = updated;
            BodyCount = SampledCount = 0; _native = default; _lastHand = 0;
        }
        public void Dispose()
        { if (_handle != IntPtr.Zero) { RuntimeBindings.HV_RuntimeDestroy(_handle); _handle = IntPtr.Zero; }
          if (_buffer != IntPtr.Zero) { Marshal.FreeHGlobal(_buffer); _buffer = IntPtr.Zero; } }
    }
}
