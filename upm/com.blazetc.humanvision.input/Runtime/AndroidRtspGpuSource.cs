using System;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
namespace HumanVision.Input
{
    // Main-thread owner of the preview and optional analysis textures. The native converter is nonblocking.
    internal sealed class AndroidRtspGpuSource
    {
        private const int SlotCount = 3;
        private readonly SourceRetirement retirement = new SourceRetirement();
        private readonly GpuOutput preview, analysis;
        private readonly RtspAnalysisSlotLedger analysisTimings;
        private readonly InputRetirementRegistration registration;
        private readonly byte[] error = new byte[1024];
        private IntPtr handle, renderEvent;
        private ulong nativeGeneration;
        private long frameId, frameIdBase;
        private int maximumWidth, maximumHeight;
        private Vector2Int analysisSize, decodedSize;
        private bool closing, rebinding, mirror;

        private sealed class GpuOutput
        {
            internal readonly SourceGeneration Timeline;
            internal readonly RenderTexture[] Textures = new RenderTexture[SlotCount];
            internal readonly IntPtr[] Pointers = new IntPtr[SlotCount];
            internal readonly ulong[] Tokens = new ulong[SlotCount], Sequences = new ulong[SlotCount];
            internal readonly bool[] Destroy = new bool[SlotCount];
            internal uint Width, Height;
            internal ulong Sequence;
            internal int LatestSlot = -1;

            internal GpuOutput(SourceGeneration timeline) => Timeline = timeline;
        }

        internal InputSourceState State { get; private set; } = InputSourceState.Stopped;
        internal string LastError { get; private set; } = string.Empty;
        internal Texture CurrentTexture => closing ? null : preview.Timeline.CurrentTexture;
        internal bool Pending => handle != IntPtr.Zero || retirement.PendingResourceCount != 0;
        internal IntPtr Handle => handle;
        internal Vector2Int DecodedInputSize => State == InputSourceState.Streaming ? decodedSize : Vector2Int.zero;
        internal bool HasSeparateAnalysis => handle != IntPtr.Zero && !closing && analysisSize != Vector2Int.zero;

        internal AndroidRtspGpuSource()
        {
            preview = new GpuOutput(new SourceGeneration(retirement));
            analysis = new GpuOutput(new SourceGeneration(retirement, preview.Timeline.SourceId));
            analysisTimings = new RtspAnalysisSlotLedger(analysis.Timeline.SourceId);
            registration = new InputRetirementRegistration(retirement, Progress, () => Pending);
        }

        internal bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) => preview.Timeline.TryGetLatestFrame(after, out frame);
        internal bool TryGetDecodedGeometry(in HumanVisionTextureFrame frame, out Vector2Int geometry, out ulong generation)
        {
            geometry = default;
            generation = 0;
            if (closing || rebinding || nativeGeneration == 0 || decodedSize == Vector2Int.zero ||
                !preview.Timeline.TryGetLatestFrame(-1, out var latest) ||
                frame.SourceId != latest.SourceId || frame.Generation != latest.Generation || frame.FrameId != latest.FrameId ||
                frame.ResourceToken != latest.ResourceToken || frame.Texture != latest.Texture)
                return false;
            geometry = decodedSize;
            generation = nativeGeneration;
            return true;
        }
        internal bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) => preview.Timeline.TryAcquireSourceCopyLease(in frame, out lease);
        internal bool TryGetLatestAnalysisFrame(long after, out HumanVisionTextureFrame frame) => analysis.Timeline.TryGetLatestFrame(after, out frame);
        internal bool TryAcquireAnalysisCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) => analysis.Timeline.TryAcquireSourceCopyLease(in frame, out lease);

        internal void RecordAnalysisSnapshotCompletion(ulong sourceId, ulong generation, long frameId, long observedUs,
            RtspAnalysisSnapshotStatus status)
        {
            retirement.CheckThread();
            analysisTimings.RecordSnapshot(sourceId, generation, frameId, observedUs, status);
        }

        internal void RecordAnalysisReadbackCompletion(ulong sourceId, ulong generation, long frameId, long observedUs, bool succeeded)
        {
            retirement.CheckThread();
            analysisTimings.RecordReadback(sourceId, generation, frameId, observedUs, succeeded);
        }

        internal int CopyAnalysisSlotTimings(RtspAnalysisSlotTiming[] destination)
        {
            retirement.CheckThread();
            int count = analysisTimings.CopyTo(destination);
            if (!HasSeparateAnalysis || rebinding || State != InputSourceState.Streaming) return count;
            for (int i = 0; i < count; ++i) {
                ref var row = ref destination[i];
                if (row.Generation != analysis.Timeline.Generation || row.NativeGeneration != nativeGeneration ||
                    row.NativeSequence != analysis.Sequences[row.Slot]) continue;
                row.SlotOutstanding = true;
                row.SlotIsLatest = row.Slot == analysis.LatestSlot;
                row.SlotHasPendingCopy = retirement.HasPendingCopies(analysis.Textures[row.Slot]);
            }
            return count;
        }

        internal void ConfigureAnalysisOutput(Vector2Int size)
        {
            if (Pending) throw new InvalidOperationException("Analysis output must be configured before opening the RTSP source.");
            analysisSize = size;
        }

        internal bool AcknowledgeAnalysisFrame(in HumanVisionTextureFrame frame)
        {
            retirement.CheckThread();
            if (!HasSeparateAnalysis || rebinding || frame.SourceId != analysis.Timeline.SourceId ||
                frame.Generation != analysis.Timeline.Generation || frame.FrameId <= frameIdBase)
                return false;
            ulong nativeSequence = checked((ulong)(frame.FrameId - frameIdBase));
            for (int i = 0; i < SlotCount; ++i) {
                if (analysis.Sequences[i] == nativeSequence && analysis.Tokens[i] == frame.ResourceToken &&
                    analysis.Textures[i] == frame.Texture)
                    return NativeInputBindings.HV_Input_AcknowledgeGpuAnalysisFrame(handle, nativeGeneration, nativeSequence) == 0;
            }
            return false;
        }

        internal void Open(HumanVisionSourceSettings settings, int timeout, int delay)
        {
            try { OpenCore(settings, timeout, delay); }
            catch (Exception ex) { Fail(ex is InvalidOperationException ? ex.Message : "Android RTSP initialization failed; verify input plugin and hardware capabilities."); throw; }
        }
        internal void Fail(string message)
        {
            Close(); State = InputSourceState.Error; LastError = message;
        }
        private void OpenCore(HumanVisionSourceSettings settings, int timeout, int delay)
        {
            if (Pending) throw new InvalidOperationException("RTSP GPU source is still retiring; wait for Closing to finish before reopening.");
            if (SystemInfo.graphicsDeviceType != GraphicsDeviceType.Vulkan) throw new InvalidOperationException("Android RTSP requires Vulkan and qualified AHB/sync-fd support.");
#if UNITY_ANDROID && !UNITY_EDITOR
            string selected = HardwareVideoDecoderSelection.SelectAvcCodec();
            NativeInputBindings.HV_Input_SelectHardwareCodec(selected);
#endif
            var url = NativeInputBindings.Utf8(settings.Location);
            try {
                var options = new NativeInputBindings.Options { Size = (uint)Marshal.SizeOf<NativeInputBindings.Options>(), Version = 1,
                    Url = url, MaxWidth = (uint)settings.RequestedWidth, MaxHeight = (uint)settings.RequestedHeight,
                    TimeoutMs = (uint)timeout, ReconnectDelayMs = (uint)delay, TransportTcp = 1 };
                int result = NativeInputBindings.HV_Input_Open(ref options, out handle);
                if (result != 0) throw new InvalidOperationException(settings.RequestedWidth == 0 ? "Original-resolution RTSP GPU output is unavailable (" + result + "); install an Input plugin supporting source-size 0/0." : "RTSP GPU initialization failed (" + result + "); a prior input owner may still be retiring.");
            } finally { Marshal.FreeHGlobal(url); }
            renderEvent = NativeInputBindings.HV_Input_GetRenderEventFunc();
            maximumWidth = settings.RequestedWidth; maximumHeight = settings.RequestedHeight;
            nativeGeneration = 0; decodedSize = default; closing = rebinding = false; mirror = settings.DisplayMirror;
            State = InputSourceState.Opening; LastError = "Waiting for RTSP connection, first keyframe and actual decoder geometry.";
            BeginOutputGeneration();
        }
        internal void Tick()
        {
            retirement.Poll(); Progress();
            if (handle == IntPtr.Zero || closing) return;
            NativeInputBindings.HV_Input_GetState(handle, out var state);
            if (state == 4 || state == 5 || state == 0) {
                NativeInputBindings.HV_Input_GetLastError(handle, error, (uint)error.Length);
                int size = Array.IndexOf(error, (byte)0);
                string message = size > 0 ? Encoding.UTF8.GetString(error, 0, size) : "RTSP decoder stopped before a usable GPU frame was published.";
                Close(); State = InputSourceState.Error; LastError = message; return;
            }
            if (state == 3) {
                State = InputSourceState.Reconnecting;
                LastError = "RTSP disconnected; waiting for reconnect and a new keyframe.";
                // A reconnect supersedes any in-progress resize. Closing the
                // timelines means the replacement binding must begin them again.
                rebinding = false;
                CloseTimelines();
                IssueRenderEvent();
                return;
            }
            if (!rebinding && NativeInputBindings.HV_Input_GetGpuGeometry(handle, out var w, out var h, out var generation) == 0 &&
                (generation != nativeGeneration || w != (uint)decodedSize.x || h != (uint)decodedSize.y)) {
                rebinding = true;
                NativeInputBindings.HV_Input_RetireGpuTargets(handle);
                BeginOutputGeneration();
            }
            if (rebinding) {
                if (NativeInputBindings.HV_Input_GpuTargetsRetired(handle) == 0) {
                    IssueRenderEvent();
                    return;
                }
                DestroyRetiredTextures();
                if (retirement.PendingResourceCount != 0) return;
                // During retirement the decoder keeps only its latest image.
                // Read it again so this rebuild does not bind superseded geometry.
                if (NativeInputBindings.HV_Input_GetGpuGeometry(handle, out w, out h, out generation) != 0)
                    return;
                if (w == 0 || h == 0 || w > 4096 || h > 4096)
                    throw new InvalidOperationException("Actual decoder output geometry is unsupported.");
                var output = SelectOutputSize((int)w, (int)h, maximumWidth, maximumHeight);
                preview.Width = (uint)output.x; preview.Height = (uint)output.y;
                analysis.Width = (uint)analysisSize.x; analysis.Height = (uint)analysisSize.y;
                if (HasSeparateAnalysis) {
                    AllocateTargets(analysis, "analysis");
                    if (NativeInputBindings.HV_Input_BindGpuAnalysisTargets(handle, analysis.Pointers, analysis.Width,
                            analysis.Height, generation) != 0)
                        throw new InvalidOperationException("RTSP GPU analysis target binding failed after actual target retirement.");
                }
                AllocateTargets(preview, "preview");
                int bound = NativeInputBindings.HV_Input_BindGpuTargets(handle, preview.Pointers, preview.Width, preview.Height,
                    generation, 0, mirror ? 1 : 0);
                if (bound != 0) throw new InvalidOperationException("RTSP GPU target binding failed after actual target retirement.");
                nativeGeneration = generation; decodedSize = new Vector2Int((int)w, (int)h); rebinding = false;
                Debug.Log("HumanVision RTSP GPU geometry: decoded=" + w + "x" + h +
                    " requestedMaximum=" + maximumWidth + "x" + maximumHeight +
                    " publishedOutput=" + preview.Width + "x" + preview.Height + " nativeGeneration=" + generation +
                    (HasSeparateAnalysis ? " analysisOutput=" + analysis.Width + "x" + analysis.Height : ""));
            }
            if (HasSeparateAnalysis) {
                if (NativeInputBindings.HV_Input_PollGpuAnalysisFrame(handle, analysis.Sequence, out var modelFrame) == 0)
                    Publish(analysis, in modelFrame);
                ReleaseUnusedSlots(analysis, isAnalysis: true);
            }
            if (NativeInputBindings.HV_Input_PollGpuFrame(handle, preview.Sequence, out var previewFrame) == 0)
                Publish(preview, in previewFrame);
            ReleaseUnusedSlots(preview, isAnalysis: false);
            // Make completed frames visible and return unleased slots before the next conversion opportunity.
            PumpGpu();
        }

        private void BeginOutputGeneration()
        {
            preview.Timeline.BeginGeneration();
            analysisTimings.BeginGeneration(analysis.Timeline.BeginGeneration());
            preview.LatestSlot = analysis.LatestSlot = -1;
            preview.Sequence = analysis.Sequence = 0;
            frameIdBase = frameId;
        }

        private void CloseTimelines()
        {
            analysisTimings.Close();
            preview.Timeline.Close();
            analysis.Timeline.Close();
        }

        private void AllocateTargets(GpuOutput output, string name)
        {
            for (int i = 0; i < SlotCount; ++i) {
                int index = i;
                var texture = new RenderTexture((int)output.Width, (int)output.Height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear)
                    { name = "HumanVision RTSP GPU " + name + " slot " + i, enableRandomWrite = true, filterMode = FilterMode.Bilinear, wrapMode = TextureWrapMode.Clamp };
                output.Textures[i] = texture;
                output.Tokens[i] = retirement.Register(output.Timeline.Generation, texture, () => { output.Destroy[index] = true; });
                if (!texture.Create()) throw new InvalidOperationException("Could not create the RTSP GPU " + name + " texture.");
                // Unity initializes its owned image before AccessTexture/private queue writes.
                var previous = RenderTexture.active;
                try { RenderTexture.active = texture; GL.Clear(false, true, Color.clear); }
                finally { RenderTexture.active = previous; }
                output.Pointers[i] = texture.GetNativeTexturePtr();
                if (output.Pointers[i] == IntPtr.Zero) throw new InvalidOperationException("The RTSP GPU " + name + " texture has no native image.");
                output.Sequences[i] = 0;
            }
        }

        private void Publish(GpuOutput output, in NativeInputBindings.GpuFrame gpu)
        {
            var info = gpu.Frame;
            if (info.Generation != nativeGeneration || info.Sequence <= output.Sequence || gpu.Slot >= SlotCount ||
                info.Width != output.Width || info.Height != output.Height || info.DecodeMode != 2 || info.RowOrigin != 0)
                throw new InvalidOperationException("RTSP GPU frame metadata contract mismatch.");
            int slot = (int)gpu.Slot;
            // On the split path, both textures use the decoder's ID. The base spans reconnects even if its counter restarts.
            long logicalFrameId = HasSeparateAnalysis ? checked(frameIdBase + (long)info.Sequence) : checked(frameId + 1);
            var frame = new HumanVisionTextureFrame(output.Timeline.SourceId, output.Timeline.Generation, logicalFrameId,
                output.Textures[slot], InputMonotonicClock.NowUs, info.PtsValid == 1 ? info.PresentationTimestampUs : -1,
                0, mirror, FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Unknown, FrameTimestampKind.LocalDecode,
                output.Tokens[slot], info.DecodedTimestampUs, FrameClockDomain.SourceLocalMonotonic, info.ClockId);
            if (!output.Timeline.TryPublish(in frame))
                throw new InvalidOperationException("RTSP GPU frame publication rejected invalid metadata.");
            if (output == analysis)
                analysisTimings.Publish(frame.Generation, frame.FrameId, info.Generation, info.Sequence, slot, frame.PublishedTimestampUs);
            frameId = Math.Max(frameId, logicalFrameId);
            output.LatestSlot = slot; output.Sequences[slot] = info.Sequence; output.Sequence = info.Sequence;
            State = InputSourceState.Streaming; LastError = string.Empty;
        }

        private void ReleaseUnusedSlots(GpuOutput output, bool isAnalysis)
        {
            for (int i = 0; i < SlotCount; ++i) {
                ulong sequence = output.Sequences[i];
                if (i == output.LatestSlot || sequence == 0 || retirement.HasPendingCopies(output.Textures[i])) continue;
                int result;
                if (isAnalysis) {
                    result = NativeInputBindings.HV_Input_ReleaseGpuAnalysisSlot(handle, (uint)i, sequence);
                    analysisTimings.RecordRelease(output.Timeline.Generation, checked(frameIdBase + (long)sequence),
                        nativeGeneration, sequence, i, InputMonotonicClock.NowUs, result);
                } else result = NativeInputBindings.HV_Input_ReleaseGpuSlot(handle, (uint)i, sequence);
                // Preserve the native identity so a failed release can be retried on this binding.
                if (result == 0) output.Sequences[i] = 0;
            }
        }
        // Only schedule native render work. Publication and frame IDs still
        // advance once from Tick after the actual conversion fence is complete.
        internal void PumpGpu()
        {
            if (handle != IntPtr.Zero && !closing && !rebinding)
                IssueRenderEvent();
        }
        private void IssueRenderEvent() => GL.IssuePluginEvent(renderEvent, 0x485649);
        internal void Close()
        {
            if (handle == IntPtr.Zero) return;
            closing = true; rebinding = false; State = InputSourceState.Closing; LastError = "Retiring queued GPU source copies.";
            NativeInputBindings.HV_Input_Close(handle);
            CloseTimelines(); InputRetirementPump.Watch(registration);
        }
        /// <summary>
        /// 0/0 使用解码可见原尺寸；显式正上限等比缩小、不拉伸、不放大；整除最多舍去不足一个像素。
        /// 请求 640×480 而源为 16:9 时输出 640×360，关节与预览共享该输出坐标系。
        /// 使用整数比例比较，避免浮点舍入把标准 16:9 分辨率变成少一像素的尺寸。
        /// </summary>
        internal static Vector2Int SelectOutputSize(int sourceWidth, int sourceHeight, int maximumWidth, int maximumHeight)
        {
            if (sourceWidth < 1 || sourceHeight < 1 || !RtspFrameSource.ValidOutputBounds(maximumWidth, maximumHeight))
                throw new ArgumentOutOfRangeException(nameof(sourceWidth), "RTSP source dimensions must be positive; output bounds must be 0/0 or a positive pair.");
            if (maximumWidth == 0) {
                if (sourceWidth > 4096 || sourceHeight > 4096)
                    throw new InvalidOperationException("Original-resolution RTSP preview exceeds the supported 4096-pixel per-edge texture capability; no smaller preview was substituted.");
                return new Vector2Int(sourceWidth, sourceHeight);
            }
            int w = Math.Min(sourceWidth, maximumWidth), h = Math.Min(sourceHeight, maximumHeight);
            if ((long)w * sourceHeight <= (long)h * sourceWidth)
                h = Math.Max(1, (int)((long)sourceHeight * w / sourceWidth));
            else
                w = Math.Max(1, (int)((long)sourceWidth * h / sourceHeight));
            return new Vector2Int(w, h);
        }
        private void DestroyRetiredTextures()
        {
            DestroyRetiredTextures(preview);
            DestroyRetiredTextures(analysis);
        }
        private static void DestroyRetiredTextures(GpuOutput output)
        {
            for (int i = 0; i < SlotCount; ++i) if (output.Destroy[i]) {
                FrameTextureNormalizer.Destroy(output.Textures[i]);
                output.Textures[i] = null;
                output.Pointers[i] = IntPtr.Zero;
                output.Tokens[i] = 0;
                output.Destroy[i] = false;
            }
        }
        private void Progress()
        {
            if (handle == IntPtr.Zero || !closing) return;
            IssueRenderEvent();
            if (NativeInputBindings.HV_Input_GpuTargetsRetired(handle) != 0) DestroyRetiredTextures();
            if (retirement.PendingResourceCount == 0 && NativeInputBindings.HV_Input_GpuRetired(handle) != 0 && NativeInputBindings.HV_Input_Release(handle) == 0) {
                handle = IntPtr.Zero; DestroyRetiredTextures();
                if (State != InputSourceState.Error) { State = InputSourceState.Stopped; LastError = string.Empty; }
            }
        }
    }
}
