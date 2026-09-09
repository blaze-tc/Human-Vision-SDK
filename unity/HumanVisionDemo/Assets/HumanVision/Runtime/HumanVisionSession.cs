using System;
using HumanVision.Interop;

namespace HumanVision
{
    public enum HumanVisionPixelFormat
    {
        Rgba32 = 1,
        Bgra32 = 2,
        Rgb24 = 3,
        Bgr24 = 4
    }

    internal sealed class HumanVisionSession : IDisposable
    {
        private const int SnapshotReadAttempts = 2;

        private readonly IHumanVisionNativeApi _api;
        private readonly HumanVisionResultBuffer _result;
        private HumanVisionConfig _config;
        private IntPtr _handle;
        private bool _disposed;

        internal HumanVisionSession(HumanVisionConfig config)
            : this(config, PInvokeHumanVisionNativeApi.Instance)
        {
        }

        internal HumanVisionSession(HumanVisionConfig config, IHumanVisionNativeApi api)
        {
            _api = api ?? throw new ArgumentNullException(nameof(api));
            _config = config?.Clone() ?? throw new ArgumentNullException(nameof(config));
            _config.Validate();
            _result = new HumanVisionResultBuffer(_config.MaxBodies);

            try
            {
                using (NativeConfigLease lease = _config.CreateNativeLease())
                {
                    HVConfigNative nativeConfig = lease.Value;
                    HVResult result = _api.Create(ref nativeConfig, out _handle);
                    ThrowIfFailed("initialization", result, _handle);
                }
            }
            catch
            {
                _result.Dispose();
                if (_handle != IntPtr.Zero)
                {
                    _api.Destroy(_handle);
                    _handle = IntPtr.Zero;
                }

                throw;
            }
        }

        internal IntPtr Handle => _handle;
        internal int MaxBodies => _config.MaxBodies;
        internal int Capacity => _result.Capacity;
        internal HumanVisionBody[] Bodies => _result.Bodies;
        internal int BodyCount => _result.BodyCount;
        internal long ResultSequence => _result.ResultSequence;
        internal long SourceFrameId => _result.SourceFrameId;
        internal long SourceTimestampUs => _result.SourceTimestampUs;
        internal HumanVisionStats Stats { get; private set; }

        internal bool SubmitFrame(
            IntPtr data,
            int width,
            int height,
            int strideBytes,
            HumanVisionPixelFormat pixelFormat,
            long frameId,
            long timestampUs,
            int dataBytes)
        {
            ThrowIfDisposed();
            ValidateFrame(data, width, height, strideBytes, pixelFormat, dataBytes);

            var frame = new HVVideoFrameNative
            {
                StructSize = NativeBindings.VideoFrameSize,
                Width = width,
                Height = height,
                StrideBytes = strideBytes,
                PixelFormat = (HVPixelFormat)pixelFormat,
                FrameId = frameId,
                TimestampUs = timestampUs,
                Data = data,
                DataBytes = dataBytes
            };

            HVResult result = _api.SubmitFrame(_handle, ref frame);
            ThrowIfFailed("frame submission", result, _handle);
            return true;
        }

        internal bool PollLatestResult()
        {
            ThrowIfDisposed();
            for (int attempt = 0; attempt < SnapshotReadAttempts; attempt++)
            {
                HVResultMetaNative before = EmptyMetadata();
                HVResult metaResult = _api.GetLatestResultMeta(_handle, ref before);
                if (metaResult == HVResult.NoNewResult)
                {
                    return false;
                }

                ThrowIfFailed("result metadata query", metaResult, _handle);
                if (before.ResultSequence <= _result.ResultSequence)
                {
                    return false;
                }

                if (before.BodyCount < 0)
                {
                    throw new HumanVisionException(
                        "result metadata query",
                        (int)HVResult.Internal,
                        "Native result reported a negative body count.");
                }

                _result.EnsureCapacity(before.BodyCount > 0 ? before.BodyCount : 1);
                HVResult bodiesResult = _api.GetBodies(
                    _handle,
                    _result.NativeBodies,
                    _result.Capacity,
                    out int written);
                ThrowIfFailed("body result copy", bodiesResult, _handle);

                bool hasHands = _api is IHumanVisionHandNativeApi;
                if (hasHands) {
                    var handResult = ((IHumanVisionHandNativeApi)_api).GetHandJoints(_handle, before.ResultSequence,
                        _result.NativeHands, checked(_result.Capacity * 6));
                    if (handResult == HVResult.NoNewResult) continue;
                    ThrowIfFailed("hand snapshot copy", handResult, _handle);
                }
                HVResultMetaNative after = EmptyMetadata();
                HVResult afterResult = _api.GetLatestResultMeta(_handle, ref after);
                ThrowIfFailed("result metadata verification", afterResult, _handle);

                bool stable = before.ResultSequence == after.ResultSequence &&
                              before.BodyCount == after.BodyCount &&
                              written == before.BodyCount;
                if (!stable)
                {
                    continue;
                }

                _result.CopyFromNative(
                    written,
                    before.ResultSequence,
                    before.SourceFrameId,
                    before.SourceTimestampUs, hasHands);
                RefreshStats();
                return true;
            }

            return false;
        }

        internal void SetRegions(UnityEngine.Rect[] regions, long revision)
        {
            ThrowIfDisposed();
            var native = new HVRectNative[regions.Length];
            for (int i = 0; i < regions.Length; i++) native[i] = new HVRectNative {
                X = regions[i].x, Y = regions[i].y, Width = regions[i].width, Height = regions[i].height };
            ThrowIfFailed("region configuration", NativeBindings.HV_SetRegions(_handle, native, native.Length, revision), _handle);
        }

        internal bool CopyRegions(long sequence, int[] indices, out long revision)
        {
            ThrowIfDisposed();
            HVResult result = NativeBindings.HV_GetRegionAssignments(_handle, sequence, indices, indices.Length, out revision);
            if (result == HVResult.NoNewResult) return false;
            ThrowIfFailed("region assignments", result, _handle);
            return true;
        }

        internal void RefreshStats()
        {
            ThrowIfDisposed();
            var native = new HVStatsNative { StructSize = NativeBindings.StatsSize };
            HVResult result = _api.GetStats(_handle, ref native);
            ThrowIfFailed("statistics query", result, _handle);
            Stats = new HumanVisionStats(
                native.InputFps,
                native.InferenceFps,
                native.DetectionMs,
                native.PoseMs,
                native.TrackingMs,
                native.TotalMs,
                native.SubmittedFrames,
                native.ProcessedFrames,
                native.DroppedFrames);
        }

        internal void ReconfigureMaxBodies(int maxBodies)
        {
            ThrowIfDisposed();
            HumanVisionConfig updated = _config.Clone();
            updated.MaxBodies = maxBodies;
            updated.Validate();

            using (NativeConfigLease lease = updated.CreateNativeLease())
            {
                HVConfigNative nativeConfig = lease.Value;
                HVResult result = _api.Reconfigure(_handle, ref nativeConfig);
                ThrowIfFailed("runtime reconfiguration", result, _handle);
            }

            _result.EnsureCapacity(maxBodies);
            _config = updated;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _result.Dispose();
            if (_handle != IntPtr.Zero)
            {
                _api.Destroy(_handle);
                _handle = IntPtr.Zero;
            }

            _disposed = true;
        }

        private static HVResultMetaNative EmptyMetadata()
        {
            return new HVResultMetaNative { StructSize = NativeBindings.ResultMetaSize };
        }

        private static void ValidateFrame(
            IntPtr data,
            int width,
            int height,
            int strideBytes,
            HumanVisionPixelFormat pixelFormat,
            int dataBytes)
        {
            if (data == IntPtr.Zero)
            {
                throw new ArgumentException("Frame data pointer must not be null.", nameof(data));
            }

            if (width <= 0 || height <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(width), "Frame dimensions must be positive.");
            }

            int bytesPerPixel;
            switch (pixelFormat)
            {
                case HumanVisionPixelFormat.Rgba32:
                case HumanVisionPixelFormat.Bgra32:
                    bytesPerPixel = 4;
                    break;
                case HumanVisionPixelFormat.Rgb24:
                case HumanVisionPixelFormat.Bgr24:
                    bytesPerPixel = 3;
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(pixelFormat), pixelFormat, "Unsupported pixel format.");
            }

            int minimumStride = checked(width * bytesPerPixel);
            if (strideBytes < minimumStride)
            {
                throw new ArgumentOutOfRangeException(nameof(strideBytes), "Frame stride is smaller than one row.");
            }

            int requiredBytes = checked(strideBytes * height);
            if (dataBytes < requiredBytes)
            {
                throw new ArgumentOutOfRangeException(nameof(dataBytes), "Frame buffer does not contain all rows.");
            }
        }

        private void ThrowIfFailed(string operation, HVResult result, IntPtr errorHandle)
        {
            if (result == HVResult.Ok)
            {
                return;
            }

            throw new HumanVisionException(operation, (int)result, _api.GetLastError(errorHandle));
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(HumanVisionSession));
            }
        }
    }
}
