using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanVision
{
    // One drawer owns all screen panels; collapsing it leaves only the edge tab.
    public sealed class HumanVisionSceneControls : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        public RawImage preview;
        public string targetScene;
        public bool settingsScene;
        public bool panelsOpen = true;
        private float _progress = 1, _nextUpdate;
        private Vector2 _scroll;
        private string _diagnostics = "";
        private long _lastProcessed;
        private float _lastStatsTime;
        private HumanVisionRegionSettingsUI _settings;
        private HumanVisionRaisedHandDetector _gesture;
        private void Start()
        {
            _settings = GetComponent<HumanVisionRegionSettingsUI>();
            if (!settingsScene) {
                _gesture = GetComponent<HumanVisionRaisedHandDetector>();
                if (_gesture == null) _gesture = gameObject.AddComponent<HumanVisionRaisedHandDetector>();
                _gesture.manager = manager;
            }
        }
        private void Update() { _progress = Mathf.MoveTowards(_progress, panelsOpen ? 1 : 0, Time.unscaledDeltaTime * 6); }
        private void Geometry(out Rect panel, out Rect tab)
        {
            var safe = HumanVisionMobileGui.SafePixels;
            float width = safe.width / HumanVisionMobileGui.Scale, height = safe.height / HumanVisionMobileGui.Scale;
            float panelWidth = Mathf.Min(390, width - 62);
            panel = new Rect(width - panelWidth * _progress, 0, panelWidth, height);
            tab = new Rect(Mathf.Min(width - 52, panel.x - 52), 12, 50, 54);
        }
        public bool IsPointerOverControls(Vector2 pixel)
        {
            Geometry(out var panel, out var tab);
            var safe = HumanVisionMobileGui.SafePixels;
            var point = (pixel - safe.position) / HumanVisionMobileGui.Scale;
            return tab.Contains(point) || (_progress > 0 && panel.Contains(point));
        }
        private void OnGUI()
        {
            if (manager == null) return;
            if (Time.unscaledTime >= _nextUpdate) {
                _nextUpdate = Time.unscaledTime + .5f;
                var pipeline = manager.GetComponent<HumanVisionManager>();
                var bridge = manager.GetComponent<Demo.VideoPlayerFrameSource>();
                float elapsed = Time.unscaledTime - _lastStatsTime;
                float recentFps = elapsed > 0 ? Mathf.Max(0, pipeline.Stats.ProcessedFrames - _lastProcessed) / elapsed : 0;
                _lastProcessed = pipeline.Stats.ProcessedFrames; _lastStatsTime = Time.unscaledTime;
                _diagnostics = string.Format("Render {0:F0} / Raw body {1:F1} FPS\nResult age {2:F0} ms | Bodies {3} / Visible {4}\nPreprocess {5:F0} / Inference {6:F0} ms\n{7}",
                    1f / Mathf.Max(.001f, Time.smoothDeltaTime), recentFps,
                    bridge.ResultAgeMilliseconds, pipeline.BodyCount, manager.GetUsersCount(),
                    pipeline.Stats.DetectionMs, pipeline.Stats.PoseMs,
                    string.IsNullOrEmpty(pipeline.LastError) ? manager.InputStatus : pipeline.LastError);
                if (pipeline.BodyCount == 0) _diagnostics += "\nNo valid pose (result age is not skeleton age)";
                if (!string.IsNullOrEmpty(bridge.LastError)) _diagnostics = bridge.LastError;
                else if (!manager.IsReady) _diagnostics = manager.Status;
                else if (pipeline.UsesRuntimeProfile)
                    _diagnostics += "\nHand jobs " + pipeline.HandInferenceFps.ToString("F1") + " FPS (shared budget)\nSampled bodies " + pipeline.SampledBodyCount + "\n" + pipeline.RuntimeDiagnostics;
            }
            using (var ui = new HumanVisionMobileGui.Scope()) {
                Geometry(out var panel, out var tab);
                if (GUI.Button(tab, panelsOpen ? ">" : "<")) panelsOpen = !panelsOpen;
                // Keep layout controls alive throughout the slide to avoid mismatched GUILayout events.
                GUILayout.BeginArea(panel, GUI.skin.box);
                _scroll = GUILayout.BeginScrollView(_scroll);
                GUILayout.Label(settingsScene ? "CAMERA SETTINGS" : "HUMAN VISION");
                GUILayout.BeginHorizontal();
                if (GUILayout.Button(settingsScene ? "Camera" : "Settings")) {
                    if (!string.IsNullOrEmpty(targetScene)) { manager.StopCamera(); SceneManager.LoadScene(targetScene); }
                }
                if (GUILayout.Button("Hide panel")) panelsOpen = false;
                GUILayout.EndHorizontal();
                GUILayout.BeginHorizontal();
                if (GUILayout.Button("Start")) manager.StartCamera();
                if (GUILayout.Button("Stop")) manager.StopCamera();
                GUILayout.EndHorizontal();
                if (preview != null && GUILayout.Button(preview.enabled ? "Camera image: on" : "Camera image: off")) preview.enabled = !preview.enabled;
                if (_settings != null) _settings.DrawSettings();
                if (_gesture != null) GUILayout.Label("RAISED HAND / REGION " + _gesture.regionIndex + "\n" + _gesture.Status);
                GUILayout.Label(_diagnostics);
                GUILayout.Label(manager.Status);
                GUILayout.EndScrollView();
                GUILayout.EndArea();
            }
        }
    }
}
