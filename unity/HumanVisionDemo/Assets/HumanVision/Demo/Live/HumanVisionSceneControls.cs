using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanVision
{
    public sealed class HumanVisionSceneControls : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        public RawImage preview;
        public string targetScene;
        public bool settingsScene;
        private float _nextUpdate;
        private string _diagnostics = "";
        private void Start()
        {
            // Also upgrades camera scenes created with an earlier package version.
            if (!settingsScene) {
                var gesture = GetComponent<HumanVisionRaisedHandDetector>();
                if (gesture == null) gesture = gameObject.AddComponent<HumanVisionRaisedHandDetector>();
                gesture.manager = manager;
            }
        }
        private void OnGUI()
        {
            if (manager == null) return;
            if (Time.unscaledTime >= _nextUpdate) {
                _nextUpdate = Time.unscaledTime + .5f;
                var pipeline = manager.GetComponent<HumanVisionManager>();
                var bridge = manager.GetComponent<Demo.VideoPlayerFrameSource>();
                _diagnostics = string.Format("Render {0:F0} / Pose {1:F1} FPS | Age {2:F0} ms\nBodies {3} / Visible {4} | {5}",
                    1f / Mathf.Max(.001f, Time.smoothDeltaTime), pipeline.Stats.InferenceFps,
                    bridge.ResultAgeMilliseconds, pipeline.BodyCount, manager.GetUsersCount(),
                    string.IsNullOrEmpty(pipeline.LastError) ? manager.InputStatus : pipeline.LastError);
                if (pipeline.BodyCount > 0 && manager.GetUsersCount() == 0)
                    _diagnostics += " | Hidden: old pose, changed view or region";
                if (!string.IsNullOrEmpty(bridge.LastError)) _diagnostics = bridge.LastError;
                else if (!manager.IsReady) _diagnostics = manager.Status;
            }
            using (var ui = new HumanVisionMobileGui.Scope()) {
                GUILayout.BeginArea(new Rect(10, ui.Height - 150, ui.Width - 20, 140), GUI.skin.box);
                GUILayout.BeginHorizontal();
                if (GUILayout.Button(settingsScene ? "Camera" : "Settings")) {
                    if (!string.IsNullOrEmpty(targetScene)) { manager.StopCamera(); SceneManager.LoadScene(targetScene); }
                }
                if (GUILayout.Button("Start")) manager.StartCamera();
                if (GUILayout.Button("Stop")) manager.StopCamera();
                if (preview != null && GUILayout.Button(preview.enabled ? "Image: on" : "Image: off")) preview.enabled = !preview.enabled;
                GUILayout.EndHorizontal();
                GUILayout.Label(_diagnostics, GUILayout.Height(70));
                GUILayout.EndArea();
            }
        }
    }
}
