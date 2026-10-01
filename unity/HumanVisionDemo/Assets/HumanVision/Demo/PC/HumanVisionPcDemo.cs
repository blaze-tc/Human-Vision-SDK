using System;
using System.Collections;
using System.IO;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Video;

namespace HumanVision.Demo.PC
{
    public enum PcDemoSource { LocalVideo, WebCamera, RTSP }
    public enum PcDemoBackend { DirectML, CPU }

    // A reference integration: prepare data, select a profile, then open a source.
    [DisallowMultipleComponent]
    public sealed class HumanVisionPcDemo : MonoBehaviour
    {
        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private VideoPlayerFrameSource frameSource;
        [SerializeField] private HumanVisionLiveSource liveSource;
        [SerializeField] private RawImage preview;
        [SerializeField] private HumanVisionOverlay overlay;
        [SerializeField] private PcDemoSource source = PcDemoSource.LocalVideo;
        [SerializeField] private PcDemoBackend backend = PcDemoBackend.DirectML;
        [SerializeField, Range(1, 8)] private int maxBodies = 8;
        [SerializeField] private string videoPath = "";
        [SerializeField, Min(0)] private float startSeconds;
        [SerializeField] private string webcamName = "";
        [SerializeField] private string rtspUrl = "";
        [SerializeField] private bool startOnPlay = false;
        [SerializeField] private bool showPanel = true;

        private readonly PcStartGeneration _startGeneration = new PcStartGeneration();
        private readonly PcObservationRate _rate = new PcObservationRate();
        private Coroutine _preparation;
        private VideoPlayer _player;
        private PcDemoSource _activeSource;
        private double _activeStartSeconds;
        private string _status = "Stopped. Choose an input and press Start.";
        private string _startText = "0";
        private bool _showPreview = true, _showOverlay = true;
        private string _diagnostics = "";
        private double _nextDiagnostics;
        private static readonly string[] SourceLabels = { "Video file", "Web camera", "RTSP" };
        private static readonly string[] BackendLabels = { "DirectML GPU", "CPU" };

        public string Status => _status;
        public double FreshObservationFps => _rate.FramesPerSecond;

        public void Configure(HumanVisionManager visionManager, VideoPlayerFrameSource videoSource,
            HumanVisionLiveSource cameraSource, RawImage image, HumanVisionOverlay skeleton)
        {
            manager = visionManager; frameSource = videoSource; liveSource = cameraSource;
            preview = image; overlay = skeleton;
        }

        private void OnEnable()
        {
            _player = GetComponent<VideoPlayer>();
            if (_player != null) _player.prepareCompleted += SeekPreparedVideo;
            _startText = startSeconds.ToString(System.Globalization.CultureInfo.InvariantCulture);
            _rate.Reset(Time.realtimeSinceStartupAsDouble);
        }
        private void Start() { if (startOnPlay) StartDemo(); }
        private void Update()
        {
            _rate.Observe(manager != null && manager.IsInitialized ? manager.ResultSequence : 0,
                Time.realtimeSinceStartupAsDouble);
            if (manager != null && manager.IsInitialized && Time.realtimeSinceStartupAsDouble >= _nextDiagnostics) {
                _diagnostics = manager.RuntimeDiagnostics.Replace("Android mode:", "Runtime profile:");
                _nextDiagnostics = Time.realtimeSinceStartupAsDouble + .25;
            }
        }
        private void OnDisable()
        {
            StopDemo();
            if (_player != null) _player.prepareCompleted -= SeekPreparedVideo;
        }
        private void OnDestroy() { StopDemo(); }

        public void StartDemo()
        {
            StopDemo();
            if (!isActiveAndEnabled) return;
            if (Application.platform != RuntimePlatform.WindowsEditor && Application.platform != RuntimePlatform.WindowsPlayer)
            { _status = "PC Demo requires Windows x64. Use a Windows Editor or Windows Standalone build."; return; }
            if (manager == null || frameSource == null || liveSource == null)
            { _status = "Missing scene references. Create the scene using HumanVision/Create PC Demo."; return; }

            string profile;
            try {
                profile = PcDemoConfiguration.ProfileId(backend == PcDemoBackend.CPU, maxBodies);
                if (!Enum.IsDefined(typeof(PcDemoSource), source) || !Enum.IsDefined(typeof(PcDemoBackend), backend))
                    throw new ArgumentException("Choose a valid input and backend.");
                if (float.IsNaN(startSeconds) || float.IsInfinity(startSeconds) || startSeconds < 0)
                    throw new ArgumentException("Start seconds must be finite and nonnegative.");
                if (source == PcDemoSource.LocalVideo && !File.Exists(videoPath))
                    throw new FileNotFoundException("Enter an existing local video file path.", videoPath);
                if (source == PcDemoSource.RTSP && (!Uri.TryCreate(rtspUrl, UriKind.Absolute, out var uri) || uri.Scheme != "rtsp"))
                    throw new ArgumentException("Enter a valid rtsp:// camera URL.");
            } catch (Exception e) { _status = e.Message; return; }

            int generation = _startGeneration.Begin();
            int capacity = maxBodies;
            PcDemoSource selectedSource = source;
            string selectedVideo = videoPath;
            double selectedStart = startSeconds;
            var cameraSettings = new HumanVisionCameraSettings {
                source = source == PcDemoSource.RTSP ? HumanVisionCameraKind.Rtsp : HumanVisionCameraKind.WebCamera,
                deviceName = webcamName, rtspUrl = rtspUrl, people = capacity, mirror = false, rtspTcp = true
            };
            _status = "Preparing verified runtime data...";
            _preparation = StartCoroutine(HumanVisionRuntimeData.Prepare(root => {
                if (!_startGeneration.IsCurrent(generation) || !isActiveAndEnabled) return;
                _preparation = null;
                try {
                    if (!manager.TryInitialize(new HumanVisionConfig { RuntimeRoot = root, Profile = profile, MaxBodies = capacity }))
                    { _status = manager.LastError; return; }
                    _activeSource = selectedSource; _activeStartSeconds = selectedStart;
                    _rate.Reset(Time.realtimeSinceStartupAsDouble);
                    if (selectedSource == PcDemoSource.LocalVideo) {
                        if (!frameSource.PlayUrl(selectedVideo)) { _status = frameSource.LastError; manager.Shutdown(); return; }
                    } else liveSource.Open(cameraSettings);
                    _status = "Started: " + selectedSource + " / " + profile + " / capacity " + capacity;
                } catch (Exception e) { StopDemo(); _status = e.Message; }
            }, error => {
                if (!_startGeneration.IsCurrent(generation) || !isActiveAndEnabled) return;
                _preparation = null; _status = error;
            }));
        }

        public void StopDemo()
        {
            _startGeneration.Cancel();
            if (_preparation != null) { StopCoroutine(_preparation); _preparation = null; }
            if (liveSource != null) liveSource.Close();
            if (frameSource != null) frameSource.StopFrames();
            if (manager != null) manager.Shutdown();
            _rate.Reset(Time.realtimeSinceStartupAsDouble);
            _diagnostics = ""; _nextDiagnostics = 0;
            if (preview != null) preview.texture = null;
            if (overlay != null) overlay.SetVerticesDirty();
            _status = "Stopped. Choose an input and press Start.";
        }

        private void SeekPreparedVideo(VideoPlayer player)
        {
            if (!isActiveAndEnabled || manager == null || !manager.IsInitialized || _activeSource != PcDemoSource.LocalVideo) return;
            if (_activeStartSeconds > 0 && player.canSetTime)
                player.time = Math.Min(_activeStartSeconds, Math.Max(0, player.length - .1));
        }

        private void OnGUI()
        {
            float width = Mathf.Min(520, Screen.width - 20);
            GUIStyle label = new GUIStyle(GUI.skin.label) { wordWrap = true, fontSize = 15 };
            GUIStyle button = new GUIStyle(GUI.skin.button) { fontSize = 15 };
            GUILayout.BeginArea(new Rect(10, 10, width, Mathf.Max(100, Screen.height - 20)), GUI.skin.box);
            if (GUILayout.Button(showPanel ? "HumanVision PC  -  Collapse" : "HumanVision PC  -  Settings", button)) showPanel = !showPanel;
            if (showPanel) {
                source = (PcDemoSource)GUILayout.SelectionGrid((int)source, SourceLabels, 3, button);
                backend = (PcDemoBackend)GUILayout.SelectionGrid((int)backend, BackendLabels, 2, button);
                GUILayout.Label("Capacity: " + maxBodies + " people (1-8)", label);
                maxBodies = Mathf.RoundToInt(GUILayout.HorizontalSlider(maxBodies, 1, 8));
                if (source == PcDemoSource.LocalVideo) {
                    GUILayout.Label("Local video file path", label); videoPath = GUILayout.TextField(videoPath);
                    GUILayout.Label("Start seconds (0 = beginning)", label); _startText = GUILayout.TextField(_startText);
                } else if (source == PcDemoSource.WebCamera) {
                    GUILayout.Label("Camera device name (empty = first device)", label); webcamName = GUILayout.TextField(webcamName);
                } else { GUILayout.Label("RTSP URL (TCP)", label); rtspUrl = GUILayout.TextField(rtspUrl); }
                GUILayout.Label("Changes apply on Start / Restart.", label);
                GUILayout.BeginHorizontal();
                if (GUILayout.Button(manager != null && manager.IsInitialized ? "Restart" : "Start", button)) {
                    if (float.TryParse(_startText, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float value)) {
                        startSeconds = value; StartDemo();
                    } else _status = "Enter a numeric start time in seconds.";
                }
                if (GUILayout.Button("Stop", button)) StopDemo();
                GUILayout.EndHorizontal();
                _showPreview = GUILayout.Toggle(_showPreview, "Show video preview");
                _showOverlay = GUILayout.Toggle(_showOverlay, "Show skeleton / boxes / IDs");
                if (preview != null) preview.enabled = _showPreview;
                if (overlay != null) overlay.enabled = _showOverlay;
            }
            GUILayout.Label(_status, label);
            if (manager != null && manager.IsInitialized) {
                GUILayout.Label("Active profile: " + manager.ActiveRuntimeProfile, label);
                GUILayout.Label(string.Format("Fresh observations: {0:F2} FPS | bodies: {1}/{2}\nResult: {3} | source frame: {4}",
                    FreshObservationFps, manager.BodyCount, manager.MaxBodies, manager.ResultSequence, manager.SourceFrameId), label);
                GUILayout.Label(manager.SourceTimestampUs <= 0 ? "Source age: N/A (no observation yet)"
                    : "Source age: " + frameSource.ResultAgeMilliseconds.ToString("F1") + " ms", label);
                if (_activeSource != PcDemoSource.LocalVideo) GUILayout.Label(liveSource.Status, label);
                GUILayout.Label(_diagnostics, label);
                if (!string.IsNullOrEmpty(manager.LastError)) GUILayout.Label(manager.LastError, label);
                if (!string.IsNullOrEmpty(frameSource.LastError)) GUILayout.Label(frameSource.LastError, label);
            }
            GUILayout.EndArea();
        }
    }
}
