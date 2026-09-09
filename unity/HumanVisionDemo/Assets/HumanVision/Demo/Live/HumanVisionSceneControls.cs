using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanVision
{
    // Scene navigation/diagnostics is separate from camera acquisition and region editing.
    public sealed class HumanVisionSceneControls : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        public RawImage preview;
        public string targetScene;
        public bool settingsScene;
        private float _nextUpdate;
        private string _diagnostics = "";
        private void OnGUI()
        {
            if (manager == null) return;
            if (Time.unscaledTime >= _nextUpdate) {
                _nextUpdate = Time.unscaledTime + .5f;
                var pipeline = manager.GetComponent<HumanVisionManager>();
                var bridge = manager.GetComponent<Demo.VideoPlayerFrameSource>();
                _diagnostics = string.Format("Render {0:F0} FPS | Inference {1:F1} FPS | {2:F0} ms | Result age {3:F0} ms | {4}",
                    1f / Mathf.Max(.001f, Time.smoothDeltaTime), pipeline.Stats.InferenceFps, pipeline.Stats.TotalMs,
                    bridge.ResultAgeMilliseconds, manager.InputStatus);
            }
            float y = Screen.height - 72;
            if (GUI.Button(new Rect(12, y, 140, 30), settingsScene ? "Back to camera" : "Open settings")) {
                if (!string.IsNullOrEmpty(targetScene)) { manager.StopCamera(); SceneManager.LoadScene(targetScene); }
            }
            if (GUI.Button(new Rect(160, y, 90, 30), "Start")) manager.StartCamera();
            if (GUI.Button(new Rect(258, y, 90, 30), "Stop")) manager.StopCamera();
            if (preview != null) preview.enabled = GUI.Toggle(new Rect(360, y, 150, 30), preview.enabled, "Show camera image");
            GUI.Label(new Rect(12, y + 35, Screen.width - 24, 28), _diagnostics);
        }
    }
}
