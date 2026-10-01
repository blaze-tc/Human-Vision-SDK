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
        [SerializeField, Range(.75f, 2f)] private float uiScale = 1f;

        private GUISkin _guiSkin;
        private Vector2 _panelScroll;
        private float _contentWidth;
        private GUILayoutOption[] _contentWidthOptions, _controlWidthOptions, _halfWidthOptions;
        private GUILayoutOption[] _sourceChoiceOptions, _backendChoiceOptions, _sourceChoiceGroupOptions, _backendChoiceGroupOptions;
        private int _sourceColumns, _backendColumns;
        private static readonly GUILayoutOption MinimumControlHeight = GUILayout.MinHeight(44);

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
                _diagnostics = PcDiagnostics.Format(manager.RuntimeDiagnostics);
                _nextDiagnostics = Time.realtimeSinceStartupAsDouble + .25;
            }
        }
        private void OnDisable()
        {
            StopDemo();
            if (_player != null) _player.prepareCompleted -= SeekPreparedVideo;
        }
        private void OnDestroy() { StopDemo(); if (_guiSkin != null) Destroy(_guiSkin); }

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

        private void PrepareGuiSkin()
        {
            if (_guiSkin != null) return;
            _guiSkin = Instantiate(GUI.skin);
            _guiSkin.hideFlags = HideFlags.HideAndDontSave;
            foreach (GUIStyle style in new[] { _guiSkin.label, _guiSkin.button, _guiSkin.toggle, _guiSkin.textField }) {
                style.fontSize = 18;
                style.wordWrap = true;
                style.padding = new RectOffset(10, 10, 8, 8);
                style.normal.textColor = Color.white;
            }
            _guiSkin.label.fixedHeight = 0;
            _guiSkin.button.fixedHeight = 0;
            _guiSkin.button.margin = new RectOffset(0, 0, 2, 2);
            _guiSkin.textField.fixedHeight = 42;
            _guiSkin.textField.wordWrap = false;
            // Preserve a visible selected state and a full-width click target.
            _guiSkin.toggle = new GUIStyle(_guiSkin.button) { fixedHeight = 0, alignment = TextAnchor.MiddleLeft };
            _guiSkin.box.padding = new RectOffset(8, 8, 8, 8);
            _guiSkin.horizontalSlider.fixedHeight = 36;
            _guiSkin.horizontalSlider.padding = new RectOffset(12, 12, 12, 12);
            _guiSkin.horizontalSliderThumb.fixedWidth = 26;
            _guiSkin.horizontalSliderThumb.fixedHeight = 32;
            _guiSkin.verticalScrollbar.fixedWidth = 22;
            _guiSkin.verticalScrollbarThumb.fixedWidth = 22;
        }

        private void PrepareContentWidth(float width)
        {
            if (_contentWidthOptions != null && Mathf.Approximately(_contentWidth, width)) return;
            _contentWidth = width;
            _contentWidthOptions = new[] { GUILayout.Width(width) };
            _controlWidthOptions = new[] { GUILayout.Width(width), MinimumControlHeight };
            _halfWidthOptions = new[] { GUILayout.Width(PcGuiLayout.ChoiceCellWidth(width, 2)), MinimumControlHeight };
            _sourceColumns = width >= 440 ? 3 : 1;
            _backendColumns = width >= 340 ? 2 : 1;
            _sourceChoiceOptions = new[] { GUILayout.Width(PcGuiLayout.ChoiceCellWidth(width, _sourceColumns)), MinimumControlHeight };
            _backendChoiceOptions = new[] { GUILayout.Width(PcGuiLayout.ChoiceCellWidth(width, _backendColumns)), MinimumControlHeight };
            _sourceChoiceGroupOptions = new[] { GUILayout.Width(width), GUILayout.MinHeight(PcGuiLayout.ChoiceMinimumHeight(SourceLabels.Length, _sourceColumns)) };
            _backendChoiceGroupOptions = new[] { GUILayout.Width(width), GUILayout.MinHeight(PcGuiLayout.ChoiceMinimumHeight(BackendLabels.Length, _backendColumns)) };
        }

        private int Choices(int selected, string[] labels, int columns, GUILayoutOption[] groupOptions, GUILayoutOption[] cellOptions)
        {
            GUILayout.BeginVertical(groupOptions);
            for (int first = 0; first < labels.Length; first += columns) {
                GUILayout.BeginHorizontal(_contentWidthOptions);
                for (int i = first; i < Mathf.Min(first + columns, labels.Length); i++) {
                    if (i != first) GUILayout.Space(4);
                    // Individual controls reserve their own height; wrapped captions
                    // may grow beyond44units without compressing neighbouring rows.
                    if (GUILayout.Toggle(selected == i, labels[i], GUI.skin.button, cellOptions)) selected = i;
                }
                GUILayout.EndHorizontal();
            }
            GUILayout.EndVertical();
            return selected;
        }

        private void Label(string text) { GUILayout.Label(text, _contentWidthOptions); }

        private void OnGUI()
        {
            Matrix4x4 incomingMatrix = GUI.matrix;
            GUISkin incomingSkin = GUI.skin;
            Color incomingColor = GUI.color, incomingBackground = GUI.backgroundColor, incomingContent = GUI.contentColor;
            bool incomingEnabled = GUI.enabled;
            try {
                PrepareGuiSkin();
                GUI.skin = _guiSkin;
                GUI.color = GUI.backgroundColor = GUI.contentColor = Color.white;
                GUI.enabled = true;
                float scale = PcGuiLayout.Scale(Screen.width, Screen.height, Screen.dpi, uiScale);
                Rect panel = PcGuiLayout.PanelPixels(Screen.width, Screen.height, Screen.safeArea, Screen.dpi, uiScale, showPanel);
                // Unity applies this matrix to IMGUI hit testing as well as drawing.
                // Do not rescale Event.current.mousePosition a second time.
                GUI.matrix = Matrix4x4.TRS(new Vector3(panel.x, panel.y, 0), Quaternion.identity, new Vector3(scale, scale, 1));
                GUILayout.BeginArea(new Rect(0, 0, panel.width / scale, panel.height / scale), GUI.skin.box);
                bool expanded = showPanel;
                bool narrow = panel.width / scale < 340;
                string title = narrow ? (expanded ? "PC  -  Collapse" : "PC  -  Settings")
                    : (expanded ? "HumanVision PC  -  Collapse" : "HumanVision PC  -  Settings");
                if (GUILayout.Button(title, MinimumControlHeight)) {
                    showPanel = !showPanel;
                }
                if (expanded) {
                    PrepareContentWidth(PcGuiLayout.ScrollContentWidth(panel.width, scale));
                    _panelScroll = GUILayout.BeginScrollView(_panelScroll, false, true, GUIStyle.none, GUI.skin.verticalScrollbar,
                        GUILayout.Height(PcGuiLayout.ScrollViewportHeight(panel.height, scale)));
                    GUILayout.BeginVertical(_contentWidthOptions);
                    // Narrow windows stack choices to keep every label readable.
                    source = (PcDemoSource)Choices((int)source, SourceLabels, _sourceColumns, _sourceChoiceGroupOptions, _sourceChoiceOptions);
                    backend = (PcDemoBackend)Choices((int)backend, BackendLabels, _backendColumns, _backendChoiceGroupOptions, _backendChoiceOptions);
                    Label("Capacity: " + maxBodies + " people (1-8)");
                    maxBodies = Mathf.RoundToInt(GUILayout.HorizontalSlider(maxBodies, 1, 8, _contentWidthOptions));
                    if (source == PcDemoSource.LocalVideo) {
                        Label("Local video file path"); videoPath = GUILayout.TextField(videoPath, _contentWidthOptions);
                        Label("Start seconds (0 = beginning)"); _startText = GUILayout.TextField(_startText, _contentWidthOptions);
                    } else if (source == PcDemoSource.WebCamera) {
                        Label("Camera device name (empty = first device)"); webcamName = GUILayout.TextField(webcamName, _contentWidthOptions);
                    } else { Label("RTSP URL (TCP)"); rtspUrl = GUILayout.TextField(rtspUrl, _contentWidthOptions); }
                    Label("Changes apply on Start / Restart.");
                    GUILayout.BeginHorizontal(_contentWidthOptions);
                    if (GUILayout.Button(manager != null && manager.IsInitialized ? "Restart" : "Start", _halfWidthOptions)) {
                        if (float.TryParse(_startText, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float value)) {
                            startSeconds = value; StartDemo();
                        } else _status = "Enter a numeric start time in seconds.";
                    }
                    GUILayout.Space(4);
                    if (GUILayout.Button("Stop", _halfWidthOptions)) StopDemo();
                    GUILayout.EndHorizontal();
                    _showPreview = GUILayout.Toggle(_showPreview, "Show video preview", _controlWidthOptions);
                    _showOverlay = GUILayout.Toggle(_showOverlay, "Show skeleton / boxes / IDs", _controlWidthOptions);
                    if (preview != null) preview.enabled = _showPreview;
                    if (overlay != null) overlay.enabled = _showOverlay;
                    Label(_status);
                    if (manager != null && manager.IsInitialized) {
                        Label("Active profile: " + manager.ActiveRuntimeProfile);
                        Label(string.Format("Fresh observations: {0:F2} FPS | bodies: {1}/{2}\nResult: {3} | source frame: {4}",
                            FreshObservationFps, manager.BodyCount, manager.MaxBodies, manager.ResultSequence, manager.SourceFrameId));
                        Label(manager.SourceTimestampUs <= 0 ? "Source age: N/A (no observation yet)"
                            : "Source age: " + frameSource.ResultAgeMilliseconds.ToString("F1") + " ms");
                        if (_activeSource != PcDemoSource.LocalVideo) Label(liveSource.Status);
                        Label(_diagnostics);
                        if (!string.IsNullOrEmpty(manager.LastError)) Label(manager.LastError);
                        if (!string.IsNullOrEmpty(frameSource.LastError)) Label(frameSource.LastError);
                    }
                    GUILayout.EndVertical();
                    GUILayout.EndScrollView();
                }
                GUILayout.EndArea();
            } finally {
                GUI.matrix = incomingMatrix;
                GUI.skin = incomingSkin;
                GUI.color = incomingColor; GUI.backgroundColor = incomingBackground; GUI.contentColor = incomingContent;
                GUI.enabled = incomingEnabled;
            }
        }
    }
}
