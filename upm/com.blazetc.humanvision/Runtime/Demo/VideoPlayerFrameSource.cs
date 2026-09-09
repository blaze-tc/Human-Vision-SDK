using System;
using System.IO;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.UI;
using UnityEngine.Video;

namespace HumanVision.Demo
{
    internal static class AnalysisRenderTextureGeometry
    {
        internal static Vector2Int CalculateTargetSize(
            int sourceWidth,
            int sourceHeight,
            int maxWidth,
            int maxHeight)
        {
            if (sourceWidth <= 0 || sourceHeight <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(sourceWidth), "Source dimensions must be positive.");
            }

            if (maxWidth <= 0 || maxHeight <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(maxWidth), "Maximum dimensions must be positive.");
            }

            if (sourceWidth <= maxWidth && sourceHeight <= maxHeight)
            {
                return new Vector2Int(sourceWidth, sourceHeight);
            }

            double scale = Math.Min((double)maxWidth / sourceWidth, (double)maxHeight / sourceHeight);
            return new Vector2Int(
                Math.Max(1, (int)Math.Floor(sourceWidth * scale)),
                Math.Max(1, (int)Math.Floor(sourceHeight * scale)));
        }
    }

    internal static class PresentationFramePolicy
    {
        internal static bool TryGetDelayedFrameId(
            long latestFrameId,
            long minimumFrameId,
            int delayFrames,
            out long presentationFrameId)
        {
            presentationFrameId = -1;
            if (latestFrameId < 0 || delayFrames < 0)
            {
                return false;
            }

            long candidate = latestFrameId - delayFrames;
            if (candidate < minimumFrameId)
            {
                return false;
            }

            presentationFrameId = candidate;
            return true;
        }

        internal static bool TryGetSynchronizedFrameId(
            long latestCapturedFrameId,
            long latestResultFrameId,
            long currentPresentationFrameId,
            long minimumFrameId,
            int maxPoseSkewFrames,
            out long presentationFrameId)
        {
            presentationFrameId = -1;
            if (maxPoseSkewFrames < 0 || latestCapturedFrameId < minimumFrameId ||
                latestResultFrameId < minimumFrameId)
            {
                return false;
            }

            // Advancing video while holding an older pose causes visible sliding.
            // Present the actual source frame; throughput must come from inference.
            if (latestResultFrameId > latestCapturedFrameId) return false;
            presentationFrameId = latestResultFrameId;
            return presentationFrameId >= minimumFrameId;
        }

        internal static bool IsUsable(
            long latestSubmittedFrameId,
            long resultFrameId,
            long minimumResultFrameId,
            int maxLagFrames)
        {
            if (maxLagFrames < 0 || latestSubmittedFrameId < 0 ||
                resultFrameId < minimumResultFrameId || resultFrameId > latestSubmittedFrameId)
            {
                return false;
            }

            return latestSubmittedFrameId - resultFrameId <= maxLagFrames;
        }
    }

    internal static class ReadbackRowNormalizer
    {
        internal static unsafe void CopyBottomUpToTopDown(
            NativeArray<byte> source,
            NativeArray<byte> destination,
            int height,
            int strideBytes)
        {
            if (!source.IsCreated)
            {
                throw new ArgumentException("The source readback buffer is not created.", nameof(source));
            }

            if (!destination.IsCreated)
            {
                throw new ArgumentException("The destination readback buffer is not created.", nameof(destination));
            }

            if (height <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(height));
            }

            if (strideBytes <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(strideBytes));
            }

            int byteCount = checked(height * strideBytes);
            if (source.Length < byteCount || destination.Length < byteCount)
            {
                throw new ArgumentException("The readback buffers are smaller than the requested frame layout.");
            }

            byte* sourcePointer = (byte*)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(source);
            byte* destinationPointer = (byte*)NativeArrayUnsafeUtility.GetUnsafePtr(destination);
            for (int destinationRow = 0; destinationRow < height; destinationRow++)
            {
                int sourceRow = height - destinationRow - 1;
                UnsafeUtility.MemCpy(
                    destinationPointer + destinationRow * strideBytes,
                    sourcePointer + sourceRow * strideBytes,
                    strideBytes);
            }
        }
    }

    [DefaultExecutionOrder(-50)]
    [DisallowMultipleComponent]
    [RequireComponent(typeof(VideoPlayer))]
    public sealed class VideoPlayerFrameSource : MonoBehaviour
    {
        private const int ReadbackPoolSize = 3;

        [Header("Scene references")]
        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private RawImage targetDisplay;
        [SerializeField] private AspectRatioFitter aspectRatioFitter;

        [Header("Playback")]
        [SerializeField] private bool loop = true;

        [Header("Real-time analysis")]
        [SerializeField, Min(1)] private int maxAnalysisWidth = 1280;
        [SerializeField, Min(1)] private int maxAnalysisHeight = 720;
        [SerializeField, Min(0)] private int presentationDelayFrames = 6;
        [SerializeField, Min(0)] private int maxPresentationPoseSkewFrames = 4;
        [SerializeField, Min(0)] private int maxOverlayLagFrames = 10;

        private readonly ReadbackSlot[] _slots = new ReadbackSlot[ReadbackPoolSize];
        private PresentationSlot[] _presentationSlots = Array.Empty<PresentationSlot>();
        private VideoPlayer _videoPlayer;
        private RenderTexture _renderTexture;
        private long _lastScheduledFrame = -1;
        private long _nextSubmissionFrameId;
        private long _latestSubmittedFrameId = -1;
        private long _minimumUsableResultFrameId;
        private long _presentationFrameId = -1;
        private long _latestResultFrameId = -1;
        private bool _acceptReadbacks;
        private bool _normalizeReadbackRows;
        private bool _livePreview;
        private bool _externalInput, _rowOrderReady, _rowProbePending;
        private bool _externalRowsBottomUp;
        private Texture _liveTexture;
        private double _nextLiveSubmitTime;
        private long _livePendingFrameId = -1;
        private double _livePendingTime;
        public bool LivePreview => _livePreview;
        [Tooltip("Maximum source age of a visible live skeleton. This does not increase inference FPS.")]
        [Range(100, 10000)] public float maxLiveResultAgeMilliseconds = 3000;
        public double ResultAgeMilliseconds => manager == null || manager.SourceTimestampUs <= 0 ? 0 :
            Math.Max(0, Time.realtimeSinceStartupAsDouble * 1000 - manager.SourceTimestampUs / 1000d);

        public void ConfigureLiveInput(bool smoothPreview, int analysisWidth = 1280, int analysisHeight = 720)
        {
            StopCurrentVideo();
            _livePreview = smoothPreview;
            _externalInput = true;
            LastError = string.Empty;
            if (!_rowOrderReady && !_rowProbePending && SystemInfo.supportsAsyncGPUReadback) BeginRowOrderProbe();
            maxAnalysisWidth = Math.Max(64, analysisWidth); maxAnalysisHeight = Math.Max(64, analysisHeight);
            _liveTexture = null; _nextLiveSubmitTime = 0; _livePendingFrameId = -1;
        }

        private void PresentLiveTexture(Texture texture)
        {
            _liveTexture = texture;
            if (targetDisplay != null) { targetDisplay.texture = texture; targetDisplay.color = Color.white; }
            if (aspectRatioFitter != null) aspectRatioFitter.aspectRatio = (float)texture.width / texture.height;
        }

        public event Action VideoLayoutChanged;
        public event Action PresentationFrameChanged;

        public int SourceWidth { get; private set; }
        public int SourceHeight { get; private set; }
        public long ReadbackDrops { get; private set; }
        public long ReadbackErrors { get; private set; }
        public string CurrentVideoPath { get; private set; }
        public string LastError { get; private set; }
        public bool IsPlaying => _videoPlayer != null && _videoPlayer.isPlaying;
        public bool IsStillImage { get; private set; }
        public double VideoFrameRate => !IsStillImage && _videoPlayer != null ? _videoPlayer.frameRate : 0d;
        public int PresentationDelayFrames => presentationDelayFrames;
        public long LatestSubmittedFrameId => _latestSubmittedFrameId;
        public long PresentationFrameId => _presentationFrameId;
        public Texture PresentationTexture => _livePreview ? _liveTexture : (targetDisplay != null ? targetDisplay.texture : _renderTexture);

        public void StopFrames() { StopCurrentVideo(); _liveTexture = null; _livePendingFrameId = -1; _nextLiveSubmitTime = 0; }

        // Measure actual GPU row order once for live input, instead of assuming Vulkan and
        // OpenGL return the same layout. The marker never enters preview or inference.
        private void BeginRowOrderProbe()
        {
            _rowProbePending = true;
            var marker = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
            marker.filterMode = FilterMode.Point;
            marker.SetPixels32(new[] { new Color32(255, 0, 0, 255), new Color32(255, 0, 0, 255),
                new Color32(0, 0, 255, 255), new Color32(0, 0, 255, 255) });
            marker.Apply();
            var source = RenderTexture.GetTemporary(2, 2, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            var target = RenderTexture.GetTemporary(2, 2, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            Graphics.Blit(marker, source);
            Graphics.Blit(source, target);
            AsyncGPUReadback.Request(target, 0, TextureFormat.RGBA32, request => {
                try {
                    if (this == null) return;
                    if (request.hasError) { SetError("Camera GPU row-order readback failed. Restart camera or change graphics API."); return; }
                    var pixels = request.GetData<byte>();
                    _externalRowsBottomUp = pixels[0] > pixels[2]; // Red is the bottom row.
                    _rowOrderReady = true;
                } finally {
                    _rowProbePending = false;
                    RenderTexture.ReleaseTemporary(source); RenderTexture.ReleaseTemporary(target);
                    Destroy(marker);
                }
            });
        }

        public bool SubmitExternalTexture(Texture texture, long timestampUs)
        {
            if (texture == null || manager == null || !manager.IsInitialized) return false;
            if (_livePreview) PresentLiveTexture(texture);
            if (!SystemInfo.supportsAsyncGPUReadback) { SetError("Async GPU readback is unavailable."); return false; }
            if (!_rowOrderReady) return false;
            if (_livePreview) {
                double now = Time.realtimeSinceStartupAsDouble;
                // Keep preview independent; read back only a frame the worker can use.
                if (now < _nextLiveSubmitTime) return false;
                // Keep the native latest-frame slot fresh while inference is busy.
                // One GPU request at a time still bounds readback memory and work.
                for (int i = 0; i < _slots.Length; i++) if (_slots[i].Busy) return false;
            }
            var size = AnalysisRenderTextureGeometry.CalculateTargetSize(texture.width, texture.height,
                maxAnalysisWidth, maxAnalysisHeight);
            if (_renderTexture == null || SourceWidth != size.x || SourceHeight != size.y) {
                StopCurrentVideo();
                AllocateReadbackResources(size.x, size.y);
                VideoLayoutChanged?.Invoke();
            }
            _acceptReadbacks = true;
            int index = FindAvailableSlot();
            if (index < 0) { ReadbackDrops++; return false; }
            Graphics.Blit(texture, _renderTexture);
            ReadbackSlot slot = _slots[index];
            slot.Busy = true;
            slot.FrameId = _nextSubmissionFrameId++;
            slot.TimestampUs = timestampUs;
            if (_livePreview) {
                PresentLiveTexture(texture);
                _presentationFrameId = slot.FrameId;
                _livePendingFrameId = slot.FrameId; _livePendingTime = Time.realtimeSinceStartupAsDouble;
                _nextLiveSubmitTime = _livePendingTime + 1.0 / 30.0;
            } else CapturePresentationFrame(slot.FrameId);
            slot.Request = AsyncGPUReadback.RequestIntoNativeArray(ref slot.Buffer, _renderTexture,
                0, TextureFormat.RGBA32, slot.Completion);
            return true;
        }

        public bool CanPresentResult(long resultFrameId)
        {
            if (_livePreview) return _acceptReadbacks && resultFrameId >= _minimumUsableResultFrameId &&
                manager != null && manager.ResultSequence > 0 && ResultAgeMilliseconds <= maxLiveResultAgeMilliseconds;
            return _presentationFrameId >= _minimumUsableResultFrameId &&
                PresentationFramePolicy.IsUsable(_latestSubmittedFrameId, resultFrameId,
                    _minimumUsableResultFrameId, maxOverlayLagFrames * 2) &&
                PresentationFramePolicy.IsUsable(
                _presentationFrameId,
                resultFrameId,
                _minimumUsableResultFrameId,
                maxOverlayLagFrames);
        }

        private void Awake()
        {
            _videoPlayer = GetComponent<VideoPlayer>();
            _videoPlayer.playOnAwake = false;
            _videoPlayer.waitForFirstFrame = true;
            _videoPlayer.skipOnDrop = true;
            _videoPlayer.isLooping = loop;
            _videoPlayer.audioOutputMode = VideoAudioOutputMode.None;
            _videoPlayer.sendFrameReadyEvents = true;

            for (int index = 0; index < _slots.Length; index++)
            {
                int capturedIndex = index;
                _slots[index] = new ReadbackSlot
                {
                    Completion = request => CompleteReadback(capturedIndex, request)
                };
            }
        }

        private void OnEnable()
        {
            _videoPlayer.prepareCompleted += OnPrepared;
            _videoPlayer.errorReceived += OnVideoError;
            _videoPlayer.frameReady += OnFrameReady;
            SubscribeManager();
        }

        private void OnDisable()
        {
            _acceptReadbacks = false;
            UnsubscribeManager();
            if (_videoPlayer != null)
            {
                _videoPlayer.prepareCompleted -= OnPrepared;
                _videoPlayer.errorReceived -= OnVideoError;
                _videoPlayer.frameReady -= OnFrameReady;
                _videoPlayer.Stop();
                _videoPlayer.targetTexture = null;
            }

            ReleaseReadbackResources();
        }

        public void Configure(
            HumanVisionManager visionManager,
            RawImage display,
            AspectRatioFitter fitter)
        {
            UnsubscribeManager();
            manager = visionManager;
            targetDisplay = display;
            aspectRatioFitter = fitter;
            SubscribeManager();
        }

        public bool PlayRelativeVideo(string relativeStreamingAssetsPath)
        {
            if (string.IsNullOrWhiteSpace(relativeStreamingAssetsPath))
            {
                SetError("A relative StreamingAssets video path is required.");
                return false;
            }

            string absolutePath = Path.Combine(Application.streamingAssetsPath, relativeStreamingAssetsPath);
            return PlayUrl(absolutePath);
        }

        public bool PlayUrl(string path)
        {
            if (_videoPlayer == null)
            {
                SetError("VideoPlayerFrameSource has not completed Awake.");
                return false;
            }

            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                SetError("Video file was not found: " + path);
                return false;
            }

            StopCurrentVideo();
            CurrentVideoPath = Path.GetFullPath(path);
            _externalInput = false; _livePreview = false;
            LastError = string.Empty;
            string extension = Path.GetExtension(path).ToLowerInvariant();
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
                return PrepareStillImage(path);
            _videoPlayer.source = VideoSource.Url;
            _videoPlayer.url = CurrentVideoPath;
            _videoPlayer.Prepare();
            return true;
        }

        private bool PrepareStillImage(string path)
        {
            if (!SystemInfo.supportsAsyncGPUReadback || manager == null || !manager.IsInitialized)
            {
                SetError("Image input requires an initialized SDK and asynchronous GPU readback.");
                return false;
            }
            var image = new Texture2D(2, 2, TextureFormat.RGBA32, false);
            try
            {
                if (!image.LoadImage(File.ReadAllBytes(path)))
                {
                    SetError("Could not decode image: " + path);
                    return false;
                }
                Vector2Int size = AnalysisRenderTextureGeometry.CalculateTargetSize(
                    image.width, image.height, maxAnalysisWidth, maxAnalysisHeight);
                AllocateReadbackResources(size.x, size.y);
                Graphics.Blit(image, _renderTexture);
                IsStillImage = true;
                _acceptReadbacks = true;
                ReadbackSlot slot = _slots[0];
                slot.Busy = true;
                slot.FrameId = _nextSubmissionFrameId++;
                slot.TimestampUs = 0;
                CapturePresentationFrame(slot.FrameId);
                slot.Request = AsyncGPUReadback.RequestIntoNativeArray(
                    ref slot.Buffer, _renderTexture, 0, TextureFormat.RGBA32, slot.Completion);
                VideoLayoutChanged?.Invoke();
                return true;
            }
            catch (Exception error)
            {
                SetError("Image input failed: " + error.Message);
                return false;
            }
            finally
            {
                Destroy(image);
            }
        }

        private void OnPrepared(VideoPlayer source)
        {
            int width = checked((int)source.width);
            int height = checked((int)source.height);
            if (width <= 0 || height <= 0)
            {
                SetError("Prepared video reported invalid dimensions.");
                return;
            }

            if (!SystemInfo.supportsAsyncGPUReadback)
            {
                SetError("This graphics device does not support AsyncGPUReadback.");
                return;
            }

            Vector2Int targetSize = AnalysisRenderTextureGeometry.CalculateTargetSize(
                width,
                height,
                maxAnalysisWidth,
                maxAnalysisHeight);
            AllocateReadbackResources(targetSize.x, targetSize.y);
            _acceptReadbacks = true;
            source.Play();
            VideoLayoutChanged?.Invoke();
        }

        private void OnVideoError(VideoPlayer source, string message)
        {
            SetError("VideoPlayer error: " + message);
        }

        private void OnFrameReady(VideoPlayer source, long frameIndex)
        {
            if (!_acceptReadbacks || manager == null || !manager.IsInitialized || _renderTexture == null)
            {
                return;
            }

            if (frameIndex == _lastScheduledFrame)
            {
                return;
            }

            if (_lastScheduledFrame >= 0 && frameIndex < _lastScheduledFrame)
            {
                _minimumUsableResultFrameId = _nextSubmissionFrameId;
                ResetPresentationHistory();
                PresentationFrameChanged?.Invoke();
            }

            int slotIndex = FindAvailableSlot();
            if (slotIndex < 0)
            {
                ReadbackDrops++;
                return;
            }

            ReadbackSlot slot = _slots[slotIndex];
            slot.Busy = true;
            slot.FrameId = _nextSubmissionFrameId++;
            slot.TimestampUs = ToTimestampUs(source, frameIndex);
            _lastScheduledFrame = frameIndex;
            CapturePresentationFrame(slot.FrameId);
            slot.Request = AsyncGPUReadback.RequestIntoNativeArray(
                ref slot.Buffer,
                _renderTexture,
                0,
                TextureFormat.RGBA32,
                slot.Completion);
        }

        private unsafe void CompleteReadback(int slotIndex, AsyncGPUReadbackRequest request)
        {
            ReadbackSlot slot = _slots[slotIndex];
            try
            {
                if (request.hasError)
                {
                    ReadbackErrors++;
                    SetError("Async GPU frame readback failed.");
                    return;
                }

                if (!_acceptReadbacks || manager == null || !manager.IsInitialized || !slot.Buffer.IsCreated)
                {
                    return;
                }

                if (slot.FrameId < _minimumUsableResultFrameId)
                {
                    return;
                }

                NativeArray<byte> submissionBuffer = slot.Buffer;
                if (_normalizeReadbackRows)
                {
                    ReadbackRowNormalizer.CopyBottomUpToTopDown(
                        slot.Buffer,
                        slot.TopLeftBuffer,
                        SourceHeight,
                        SourceWidth * 4);
                    submissionBuffer = slot.TopLeftBuffer;
                }

                IntPtr data = (IntPtr)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(submissionBuffer);
                if (manager.SubmitFrame(
                    data,
                    SourceWidth,
                    SourceHeight,
                    SourceWidth * 4,
                    HumanVisionPixelFormat.Rgba32,
                    slot.FrameId,
                    slot.TimestampUs,
                    slot.Buffer.Length))
                {
                    _latestSubmittedFrameId = Math.Max(_latestSubmittedFrameId, slot.FrameId);
                    PresentationFrameChanged?.Invoke();
                }
            }
            finally
            {
                slot.Busy = false;
            }
        }

        private void AllocateReadbackResources(int width, int height)
        {
            ReleaseReadbackResources();
            SourceWidth = width;
            SourceHeight = height;
            _normalizeReadbackRows = _externalInput ? _externalRowsBottomUp : SystemInfo.graphicsUVStartsAtTop;
            int byteCount = checked(width * height * 4);
            for (int index = 0; index < _slots.Length; index++)
            {
                _slots[index].Buffer = new NativeArray<byte>(
                    byteCount,
                    Allocator.Persistent,
                    NativeArrayOptions.UninitializedMemory);
                if (_normalizeReadbackRows)
                {
                    _slots[index].TopLeftBuffer = new NativeArray<byte>(
                        byteCount,
                        Allocator.Persistent,
                        NativeArrayOptions.UninitializedMemory);
                }
                _slots[index].Busy = false;
            }

            _renderTexture = new RenderTexture(
                width,
                height,
                0,
                RenderTextureFormat.ARGB32,
                RenderTextureReadWrite.sRGB)
            {
                name = "HumanVision Video Frame",
                useMipMap = false,
                autoGenerateMips = false
            };
            _renderTexture.Create();
            int presentationSlotCount = _livePreview ? 0 : Math.Max(
                presentationDelayFrames,
                maxOverlayLagFrames * 2) + 4;
            _presentationSlots = new PresentationSlot[presentationSlotCount];
            for (int index = 0; index < _presentationSlots.Length; index++)
            {
                var texture = new RenderTexture(
                    width,
                    height,
                    0,
                    RenderTextureFormat.ARGB32,
                    RenderTextureReadWrite.sRGB)
                {
                    name = "HumanVision Presentation Frame " + index,
                    useMipMap = false,
                    autoGenerateMips = false
                };
                texture.Create();
                _presentationSlots[index] = new PresentationSlot { Texture = texture, FrameId = -1 };
            }

            _videoPlayer.renderMode = VideoRenderMode.RenderTexture;
            _videoPlayer.targetTexture = _renderTexture;

            if (targetDisplay != null)
            {
                targetDisplay.texture = _renderTexture;
                targetDisplay.color = Color.white;
            }

            if (aspectRatioFitter != null)
            {
                aspectRatioFitter.aspectMode = AspectRatioFitter.AspectMode.FitInParent;
                aspectRatioFitter.aspectRatio = (float)width / height;
            }
        }

        private void CapturePresentationFrame(long frameId)
        {
            if (_presentationSlots.Length == 0)
            {
                return;
            }

            PresentationSlot captureSlot = _presentationSlots[(int)(frameId % _presentationSlots.Length)];
            if (captureSlot.FrameId == _presentationFrameId) _presentationFrameId = -1;
            Graphics.Blit(_renderTexture, captureSlot.Texture);
            captureSlot.FrameId = frameId;

            bool hasPresentationFrame = PresentationFramePolicy.TryGetSynchronizedFrameId(
                frameId,
                _latestResultFrameId,
                _presentationFrameId,
                _minimumUsableResultFrameId,
                maxPresentationPoseSkewFrames,
                out long presentationFrameId);
            if (!hasPresentationFrame)
            {
                hasPresentationFrame = PresentationFramePolicy.TryGetDelayedFrameId(
                    frameId,
                    _minimumUsableResultFrameId,
                    presentationDelayFrames,
                    out presentationFrameId);
            }

            if (hasPresentationFrame)
            {
                PresentCapturedFrame(presentationFrameId);
            }
            else
            {
                _presentationFrameId = -1;
                if (targetDisplay != null)
                {
                    targetDisplay.texture = _renderTexture;
                }
            }
            PresentationFrameChanged?.Invoke();
        }

        private bool PresentCapturedFrame(long frameId)
        {
            if (frameId < 0 || _presentationSlots.Length == 0)
            {
                return false;
            }

            PresentationSlot displaySlot = _presentationSlots[(int)(frameId % _presentationSlots.Length)];
            if (displaySlot.FrameId != frameId)
            {
                return false;
            }

            _presentationFrameId = frameId;
            if (targetDisplay != null)
            {
                targetDisplay.texture = displaySlot.Texture;
            }
            return true;
        }

        private void OnManagerResultUpdated(long sequence)
        {
            if (_livePreview) { PresentationFrameChanged?.Invoke(); return; }
            if (manager == null || manager.SourceFrameId < _minimumUsableResultFrameId)
            {
                return;
            }

            _latestResultFrameId = manager.SourceFrameId;
            long latestCapturedFrameId = _nextSubmissionFrameId - 1;
            if (PresentationFramePolicy.TryGetSynchronizedFrameId(
                    latestCapturedFrameId,
                    _latestResultFrameId,
                    _presentationFrameId,
                    _minimumUsableResultFrameId,
                    maxPresentationPoseSkewFrames,
                    out long presentationFrameId))
            {
                PresentCapturedFrame(presentationFrameId);
            }
            PresentationFrameChanged?.Invoke();
        }

        private void SubscribeManager()
        {
            if (isActiveAndEnabled && manager != null)
            {
                manager.ResultUpdated -= OnManagerResultUpdated;
                manager.ResultUpdated += OnManagerResultUpdated;
            }
        }

        private void UnsubscribeManager()
        {
            if (manager != null)
            {
                manager.ResultUpdated -= OnManagerResultUpdated;
            }
        }

        private void ResetPresentationHistory()
        {
            _presentationFrameId = -1;
            _latestResultFrameId = -1;
            for (int index = 0; index < _presentationSlots.Length; index++)
            {
                _presentationSlots[index].FrameId = -1;
            }

            if (targetDisplay != null && _renderTexture != null)
            {
                targetDisplay.texture = _renderTexture;
            }
        }

        private void StopCurrentVideo()
        {
            _acceptReadbacks = false;
            if (_videoPlayer != null)
            {
                _videoPlayer.Stop();
                _videoPlayer.targetTexture = null;
            }

            ReleaseReadbackResources();
            _lastScheduledFrame = -1;
            _latestSubmittedFrameId = -1;
            _minimumUsableResultFrameId = _nextSubmissionFrameId;
            _presentationFrameId = -1;
            _latestResultFrameId = -1;
            SourceWidth = 0;
            SourceHeight = 0;
            IsStillImage = false;
            PresentationFrameChanged?.Invoke();
        }

        private void ReleaseReadbackResources()
        {
            bool hasOutstandingRequest = false;
            for (int index = 0; index < _slots.Length; index++)
            {
                hasOutstandingRequest |= _slots[index] != null && _slots[index].Busy;
            }

            if (hasOutstandingRequest)
            {
                AsyncGPUReadback.WaitAllRequests();
            }

            for (int index = 0; index < _slots.Length; index++)
            {
                ReadbackSlot slot = _slots[index];
                if (slot == null)
                {
                    continue;
                }

                slot.Busy = false;
                if (slot.Buffer.IsCreated)
                {
                    slot.Buffer.Dispose();
                }

                if (slot.TopLeftBuffer.IsCreated)
                {
                    slot.TopLeftBuffer.Dispose();
                }
            }

            if (targetDisplay != null)
            {
                targetDisplay.texture = null;
            }

            for (int index = 0; index < _presentationSlots.Length; index++)
            {
                PresentationSlot slot = _presentationSlots[index];
                if (slot?.Texture == null)
                {
                    continue;
                }

                slot.Texture.Release();
                Destroy(slot.Texture);
            }
            _presentationSlots = Array.Empty<PresentationSlot>();

            if (_renderTexture != null)
            {
                _renderTexture.Release();
                Destroy(_renderTexture);
                _renderTexture = null;
            }
        }

        private int FindAvailableSlot()
        {
            for (int index = 0; index < _slots.Length; index++)
            {
                if (!_slots[index].Busy && _slots[index].Buffer.IsCreated)
                {
                    return index;
                }
            }

            return -1;
        }

        private static long ToTimestampUs(VideoPlayer player, long frameIndex)
        {
            double seconds = player.clockTime;
            if (double.IsNaN(seconds) || double.IsInfinity(seconds) || seconds < 0d)
            {
                seconds = player.frameRate > 0d ? frameIndex / player.frameRate : 0d;
            }

            return checked((long)(seconds * 1_000_000d));
        }

        private void SetError(string message)
        {
            if (string.Equals(LastError, message, StringComparison.Ordinal))
            {
                return;
            }

            LastError = message;
            Debug.LogError(message, this);
        }

        private sealed class ReadbackSlot
        {
            internal NativeArray<byte> Buffer;
            internal NativeArray<byte> TopLeftBuffer;
            internal AsyncGPUReadbackRequest Request;
            internal Action<AsyncGPUReadbackRequest> Completion;
            internal bool Busy;
            internal long FrameId;
            internal long TimestampUs;
        }

        private sealed class PresentationSlot
        {
            internal RenderTexture Texture;
            internal long FrameId;
        }
    }
}
