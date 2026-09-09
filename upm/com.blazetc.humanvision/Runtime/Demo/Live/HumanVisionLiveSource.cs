using System;
using System.Collections;
using System.Runtime.InteropServices;
using HumanVision.Demo;
using UnityEngine;

namespace HumanVision
{
    [RequireComponent(typeof(VideoPlayerFrameSource))]
    public sealed class HumanVisionLiveSource : MonoBehaviour
    {
        public string Status { get; private set; } = "Stopped";
        public bool HasRecentFrame => _running && Time.realtimeSinceStartup - _lastFrameTime < 1f;
        private VideoPlayerFrameSource _bridge;
        private WebCamTexture _webcam;
        private Texture2D _rtspTexture;
        private RenderTexture _oriented;
        private Material _orientation;
        private HumanVisionCameraSettings _settings;
        private byte[] _rgba;
        private GCHandle _pin;
        private IntPtr _rtsp;
        private long _rtspSequence;
        private float _lastFrameTime;
        private bool _running, _invalidated;
        private bool _resumeAfterPause;
        private Coroutine _startRoutine;
        private int _lastRotation = -1;
        private bool _lastFlipY;
        [Tooltip("Allow the phone to rotate between portrait and landscape while the camera is open.")]
        public bool autoRotateScreen = true;

        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr HV_RtspOpen([MarshalAs(UnmanagedType.LPUTF8Str)] string url, int width, int height, int tcp, int timeoutMs);
        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl)]
        private static extern int HV_RtspCopyFrame(IntPtr handle, long afterSequence, IntPtr rgba, int capacity,
            out int width, out int height, out long sequence, out long timestamp);
        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl)]
        private static extern int HV_RtspState(IntPtr handle);
        [DllImport("humanvision", CallingConvention = CallingConvention.Cdecl)]
        private static extern void HV_RtspClose(IntPtr handle);

        [Tooltip("Android: show the current camera frame independently of slower inference.")]
        public bool smoothAndroidPreview = true;
        [Range(320, 1920)] public int androidAnalysisWidth = 640;
        [Range(240, 1080)] public int androidAnalysisHeight = 640;

        private void Awake() { _bridge = GetComponent<VideoPlayerFrameSource>(); }
        public void Open(HumanVisionCameraSettings settings)
        {
            Close();
            settings.Validate();
            bool mobile = Application.platform == RuntimePlatform.Android;
            if (mobile && autoRotateScreen) {
                Screen.autorotateToPortrait = true;
                Screen.autorotateToPortraitUpsideDown = true;
                Screen.autorotateToLandscapeLeft = true;
                Screen.autorotateToLandscapeRight = true;
                Screen.orientation = ScreenOrientation.AutoRotation;
            }
            _lastRotation = -1;
            _bridge.ConfigureLiveInput(mobile && smoothAndroidPreview,
                mobile ? androidAnalysisWidth : 1280, mobile ? androidAnalysisHeight : 720);
            _settings = JsonUtility.FromJson<HumanVisionCameraSettings>(JsonUtility.ToJson(settings));
            _startRoutine = StartCoroutine(OpenRoutine());
        }
        private IEnumerator OpenRoutine()
        {
            Status = "Opening camera";
            if (_settings.source == HumanVisionCameraKind.WebCamera) {
#if UNITY_ANDROID && !UNITY_EDITOR
                if (!UnityEngine.Android.Permission.HasUserAuthorizedPermission(UnityEngine.Android.Permission.Camera)) {
                    bool completed = false;
                    var callbacks = new UnityEngine.Android.PermissionCallbacks();
                    callbacks.PermissionGranted += _ => completed = true;
                    callbacks.PermissionDenied += _ => completed = true;
                    callbacks.PermissionDeniedAndDontAskAgain += _ => completed = true;
                    UnityEngine.Android.Permission.RequestUserPermission(UnityEngine.Android.Permission.Camera, callbacks);
                    while (!completed) yield return null;
                }
                if (!UnityEngine.Android.Permission.HasUserAuthorizedPermission(UnityEngine.Android.Permission.Camera)) {
                    Status = "Camera permission denied"; yield break;
                }
#else
                yield return Application.RequestUserAuthorization(UserAuthorization.WebCam);
                if (!Application.HasUserAuthorization(UserAuthorization.WebCam)) { Status = "Camera permission denied"; yield break; }
#endif
                var devices = WebCamTexture.devices;
                if (devices.Length == 0) { Status = "No camera reported by the operating system"; yield break; }
                string device = _settings.deviceName;
                if (string.IsNullOrEmpty(device)) device = devices[0].name;
                bool found = false;
                foreach (var item in devices) if (item.name == device) found = true;
                if (!found) { Status = "Selected camera is unavailable"; yield break; }
                _webcam = new WebCamTexture(device, _settings.width, _settings.height, _settings.framesPerSecond);
                _webcam.Play();
            } else {
                if (!Uri.TryCreate(_settings.rtspUrl, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp") {
                    Status = "Enter a valid rtsp:// camera URL"; yield break;
                }
                try {
                    _rgba = new byte[checked(_settings.width * _settings.height * 4)];
                    _pin = GCHandle.Alloc(_rgba, GCHandleType.Pinned);
                    _rtsp = HV_RtspOpen(_settings.rtspUrl, _settings.width, _settings.height, _settings.rtspTcp ? 1 : 0, 5000);
                    if (_rtsp == IntPtr.Zero) throw new InvalidOperationException("RTSP initialization failed");
                } catch (Exception e) { Status = e.Message; CloseResources(); yield break; }
            }
            Shader shader = Resources.Load<Shader>("HumanVisionCameraOrientation");
            if (shader == null) { Status = "Camera orientation shader is missing"; CloseResources(); yield break; }
            _orientation = new Material(shader);
            _running = true;
            _lastFrameTime = Time.realtimeSinceStartup;
            _startRoutine = null;
        }
        private void Update()
        {
            if (!_running) return;
            try {
                Texture input = null;
                int rotation = 0;
                bool flipY = false;
                long timestamp = (long)(Time.realtimeSinceStartupAsDouble * 1000000);
                if (_webcam != null) {
                    if (_webcam.didUpdateThisFrame && _webcam.width > 16) {
                        input = _webcam; rotation = _webcam.videoRotationAngle; flipY = _webcam.videoVerticallyMirrored;
                    }
                } else if (_rtsp != IntPtr.Zero) {
                    int state = HV_RtspState(_rtsp);
                    Status = state == 2 ? "RTSP streaming" : state == 3 ? "RTSP reconnecting" : "RTSP connecting";
                    if (HV_RtspCopyFrame(_rtsp, _rtspSequence, _pin.AddrOfPinnedObject(), _rgba.Length,
                        out int width, out int height, out long sequence, out timestamp) == 1) {
                        if (_rtspTexture == null || _rtspTexture.width != width || _rtspTexture.height != height) {
                            if (_rtspTexture != null) Destroy(_rtspTexture);
                            _rtspTexture = new Texture2D(width, height, TextureFormat.RGBA32, false);
                        }
                        _rtspTexture.LoadRawTextureData(_pin.AddrOfPinnedObject(), width * height * 4);
                        _rtspTexture.Apply(false, false);
                        _rtspSequence = sequence;
                        input = _rtspTexture;
                        flipY = true; // FFmpeg bytes are top-down; Texture2D storage is bottom-up.
                    }
                }
                if (input != null) {
                    // Discard results from the old coordinate system, including 180-degree turns.
                    if (_lastRotation != rotation || _lastFlipY != flipY) {
                        _bridge.StopFrames();
                        _lastRotation = rotation; _lastFlipY = flipY;
                    }
                    int w = rotation % 180 == 0 ? input.width : input.height;
                    int h = rotation % 180 == 0 ? input.height : input.width;
                    if (_oriented == null || _oriented.width != w || _oriented.height != h) {
                        if (_oriented != null) { _oriented.Release(); Destroy(_oriented); }
                        _oriented = new RenderTexture(w, h, 0, RenderTextureFormat.ARGB32);
                        _oriented.Create();
                    }
                    _orientation.SetFloat("_Rotation", rotation / 90);
                    _orientation.SetFloat("_FlipY", flipY ? 1 : 0);
                    _orientation.SetFloat("_Mirror", _settings.mirror ? 1 : 0);
                    Graphics.Blit(input, _oriented, _orientation);
                    _bridge.SubmitExternalTexture(_oriented, timestamp);
                    _lastFrameTime = Time.realtimeSinceStartup;
                    _invalidated = false;
                    Status = _webcam != null ? "WebCamera streaming" : "RTSP streaming";
                } else if (!HasRecentFrame && !_invalidated) {
                    _bridge.StopFrames(); _invalidated = true;
                    if (_webcam != null) Status = "Waiting for camera frames";
                }
            } catch (Exception e) { Status = e.Message; CloseResources(); }
        }
        public void Close()
        {
            if (_startRoutine != null) { StopCoroutine(_startRoutine); _startRoutine = null; }
            CloseResources(); Status = "Stopped";
        }
        private void CloseResources()
        {
            _running = false;
            if (_bridge != null) _bridge.StopFrames();
            if (_webcam != null) { _webcam.Stop(); Destroy(_webcam); _webcam = null; }
            if (_rtsp != IntPtr.Zero) {
                IntPtr closing = _rtsp; _rtsp = IntPtr.Zero;
                // Native close interrupts IO and owns asynchronous worker teardown.
                HV_RtspClose(closing);
            }
            if (_pin.IsAllocated) _pin.Free();
            _rgba = null; _rtspSequence = 0;
            if (_rtspTexture != null) { Destroy(_rtspTexture); _rtspTexture = null; }
            if (_oriented != null) { _oriented.Release(); Destroy(_oriented); _oriented = null; }
            if (_orientation != null) { Destroy(_orientation); _orientation = null; }
        }
        private void OnDisable() { Close(); }
        private void OnApplicationPause(bool paused)
        {
            if (paused) { _resumeAfterPause = _running; if (_running) Close(); }
            else if (_resumeAfterPause && isActiveAndEnabled) { _resumeAfterPause = false; Open(_settings); }
        }
    }
}
