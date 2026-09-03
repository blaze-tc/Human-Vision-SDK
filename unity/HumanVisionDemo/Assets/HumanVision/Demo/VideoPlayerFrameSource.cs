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

        private readonly ReadbackSlot[] _slots = new ReadbackSlot[ReadbackPoolSize];
        private VideoPlayer _videoPlayer;
        private RenderTexture _renderTexture;
        private long _lastScheduledFrame = -1;
        private bool _acceptReadbacks;

        public event Action VideoLayoutChanged;

        public int SourceWidth { get; private set; }
        public int SourceHeight { get; private set; }
        public long ReadbackDrops { get; private set; }
        public long ReadbackErrors { get; private set; }
        public string CurrentVideoPath { get; private set; }
        public string LastError { get; private set; }
        public bool IsPlaying => _videoPlayer != null && _videoPlayer.isPlaying;
        public double VideoFrameRate => _videoPlayer != null ? _videoPlayer.frameRate : 0d;

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
        }

        private void OnDisable()
        {
            _acceptReadbacks = false;
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
            manager = visionManager;
            targetDisplay = display;
            aspectRatioFitter = fitter;
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
            LastError = string.Empty;
            _videoPlayer.source = VideoSource.Url;
            _videoPlayer.url = CurrentVideoPath;
            _videoPlayer.Prepare();
            return true;
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

            AllocateReadbackResources(width, height);
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

            int slotIndex = FindAvailableSlot();
            if (slotIndex < 0)
            {
                ReadbackDrops++;
                return;
            }

            ReadbackSlot slot = _slots[slotIndex];
            slot.Busy = true;
            slot.FrameId = frameIndex;
            slot.TimestampUs = ToTimestampUs(source, frameIndex);
            _lastScheduledFrame = frameIndex;
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

                IntPtr data = (IntPtr)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(slot.Buffer);
                manager.SubmitFrame(
                    data,
                    SourceWidth,
                    SourceHeight,
                    SourceWidth * 4,
                    HumanVisionPixelFormat.Rgba32,
                    slot.FrameId,
                    slot.TimestampUs,
                    slot.Buffer.Length);
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
            int byteCount = checked(width * height * 4);
            for (int index = 0; index < _slots.Length; index++)
            {
                _slots[index].Buffer = new NativeArray<byte>(
                    byteCount,
                    Allocator.Persistent,
                    NativeArrayOptions.UninitializedMemory);
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
            SourceWidth = 0;
            SourceHeight = 0;
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
            }

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
            internal AsyncGPUReadbackRequest Request;
            internal Action<AsyncGPUReadbackRequest> Completion;
            internal bool Busy;
            internal long FrameId;
            internal long TimestampUs;
        }
    }
}
