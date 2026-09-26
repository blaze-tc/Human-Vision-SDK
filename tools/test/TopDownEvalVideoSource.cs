using System;
using System.Collections;
using System.IO;
using System.Security.Cryptography;
using HumanVision.Demo;
using UnityEngine;
using UnityEngine.Networking;
using UnityEngine.Video;

namespace HumanVision
{
    // Evaluation-only route in the integrated CameraDemo. VideoPlayer decodes
    // directly to a RenderTexture; the existing GPU bridge copies that texture
    // to AHB. No Unity full-frame GPU readback or CPU frame submission occurs.
    [DefaultExecutionOrder(-210)]
    public sealed class TopDownEvalVideoSource : MonoBehaviour
    {
        public const string VideoName = "video-2.mp4";
        public const string VideoSha256 = "REPLACE_VIDEO_SHA256";
        private HumanVisionCameraManager _camera;
        private HumanVisionManager _manager;
        private VideoPlayerFrameSource _bridge;
        private VideoPlayer _player;
        private RenderTexture _texture;
        private long _submitted;
        private bool _leaseActive;

        private void Awake()
        {
            _camera = GetComponent<HumanVisionCameraManager>();
            _manager = GetComponent<HumanVisionManager>();
            _bridge = GetComponent<VideoPlayerFrameSource>();
            _camera.startAutomatically = false;
        }

        private IEnumerator Start()
        {
            while (!_camera.IsReady) yield return null;
            var sourceUri = Application.streamingAssetsPath.TrimEnd('/') +
                "/HumanVision/Diagnostic/" + VideoName;
            var path = Path.Combine(Application.persistentDataPath, VideoName);
            using (var request = UnityWebRequest.Get(sourceUri))
            {
                yield return request.SendWebRequest();
                if (request.result != UnityWebRequest.Result.Success)
                    throw new IOException("Evaluation video asset unavailable: " + request.error);
                byte[] bytes = request.downloadHandler.data;
                using (var sha = SHA256.Create())
                {
                    string actual = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
                    if (!string.Equals(actual, VideoSha256, StringComparison.Ordinal))
                        throw new InvalidDataException("Evaluation video SHA-256 mismatch: " + actual);
                }
                File.WriteAllBytes(path, bytes);
            }
            Debug.Log("HV_TOPDOWN_VIDEO_SOURCE_ACTIVE name=" + VideoName + " sha256=" + VideoSha256);
            var decoder = new GameObject("TopDown evaluation GPU video decoder");
            decoder.transform.SetParent(transform, false);
            _player = decoder.AddComponent<VideoPlayer>();
            _player.playOnAwake = false;
            _player.waitForFirstFrame = true;
            _player.skipOnDrop = false;
            _player.isLooping = true;
            _player.audioOutputMode = VideoAudioOutputMode.None;
            _player.source = VideoSource.Url;
            _player.url = path;
            _player.renderMode = VideoRenderMode.RenderTexture;
            _player.sendFrameReadyEvents = true;
            _player.errorReceived += (source, error) =>
                Debug.LogError("HV_TOPDOWN_VIDEO_ERROR " + error);
            _player.prepareCompleted += OnPrepared;
            _player.frameReady += OnFrame;
            Debug.Log("HV_TOPDOWN_VIDEO_START name=" + VideoName + " sha256=" + VideoSha256);
            _player.Prepare();
        }

        private void OnPrepared(VideoPlayer source)
        {
            int width = checked((int)source.width), height = checked((int)source.height);
            if (width <= 0 || height <= 0) throw new InvalidOperationException("Video dimensions unavailable");
            _texture = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32,
                RenderTextureReadWrite.sRGB);
            _texture.Create();
            source.targetTexture = _texture;
            _bridge.ConfigureLiveInput(true, width, height);
            _manager.BeginAndroidGpuSourceLease(_texture);
            _leaseActive = true;
            Debug.Log("HV_TOPDOWN_VIDEO_READY width=" + width + " height=" + height +
                " fps=" + source.frameRate);
            source.Play();
        }

        private void OnFrame(VideoPlayer source, long frameIndex)
        {
            if (_texture == null) return;
            long timestampUs = (long)(Time.realtimeSinceStartupAsDouble * 1000000);
            bool accepted = _bridge.SubmitExternalTexture(_texture, timestampUs);
            ++_submitted;
            if (_submitted <= 6 || _submitted % 30 == 0)
                Debug.Log("HV_TOPDOWN_VIDEO_FRAME index=" + frameIndex +
                    " pts_s=" + source.time + " submitted=" + accepted +
                    " source_frame=" + _manager.SourceFrameId);
        }

        private void OnDisable() { ReleaseVideoResources(); }
        private void OnDestroy() { ReleaseVideoResources(); }

        private void ReleaseVideoResources()
        {
            if (_player != null) {
                _player.prepareCompleted -= OnPrepared;
                _player.frameReady -= OnFrame;
                _player.Stop();
                _player.targetTexture = null;
                Destroy(_player.gameObject);
                _player = null;
            }
            if (_leaseActive) {
                // EndGpuSourceLease synchronously drains outstanding render events
                // before the RenderTexture/AHB producer can be destroyed.
                _manager.EndAndroidGpuSourceLease();
                _leaseActive = false;
            }
            if (_texture != null) { _texture.Release(); Destroy(_texture); _texture = null; }
        }
    }
}
