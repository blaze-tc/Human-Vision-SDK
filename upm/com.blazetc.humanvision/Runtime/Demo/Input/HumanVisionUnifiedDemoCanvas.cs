using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    public sealed class HumanVisionUnifiedDemoCanvas : MonoBehaviour
    {
        private HumanVisionDemoNavigator navigator;
        private Text recognitionStatus, compactStatus;
        private HumanVisionInputAdapter adapter;
        private float nextStatus;
        public static void Build(HumanVisionDemoNavigator navigator, out RawImage preview, out HumanVisionOverlay overlay)
        {
            var safe = InputPreviewCanvas.Root(navigator.transform, out preview);
            var bones = new GameObject("Existing HumanVision Skeleton Renderer", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionOverlay));
            bones.transform.SetParent(preview.transform, false); InputPreviewCanvas.Stretch((RectTransform)bones.transform); overlay = bones.GetComponent<HumanVisionOverlay>();
            var navigation = InputPreviewCanvas.Row(InputPreviewCanvas.Panel(safe, "Navigation", new Vector2(0, .88f), Vector2.one));
            foreach (InputKind kind in System.Enum.GetValues(typeof(InputKind))) {
                var captured = kind; InputPreviewCanvas.Button(navigation, kind.ToString(), () => navigator.SwitchTo(captured));
            }
            var panel = InputPreviewCanvas.Panel(safe, "Settings", new Vector2(0, .28f), new Vector2(.65f, .88f));
            var content = InputPreviewCanvas.Scroll(panel);
            InputPreviewCanvas.Label(content, "Human Vision / " + navigator.Kind, 45);
            var strip = InputPreviewCanvas.Panel(safe, "Always visible compact status", Vector2.zero, new Vector2(1, .08f));
            strip.GetComponent<Image>().raycastTarget = false;
            var status = InputPreviewCanvas.Row(strip);
            navigator.Input.Status = InputPreviewCanvas.Label(status, "Waiting for source", 44);
            navigator.Input.CompactStatus = true; navigator.Input.Status.fontSize = 16;
            navigator.Input.Status.resizeTextForBestFit = false; navigator.Input.Status.raycastTarget = false;
            navigator.ModePanel = panel.gameObject.AddComponent<HumanVisionModeSettingsPanel>(); navigator.ModePanel.Build(navigator, content);
            var shared = navigator.SharedSettingsPrefab != null ? Object.Instantiate(navigator.SharedSettingsPrefab, content) : new GameObject("Shared settings", typeof(RectTransform), typeof(HumanVisionSharedSettingsPanel));
            shared.transform.SetParent(content, false);
            var sharedContent = InputPreviewCanvas.Column(shared.transform);
            shared.AddComponent<LayoutElement>().preferredHeight = 570;
            navigator.SharedPanel = shared.GetComponent<HumanVisionSharedSettingsPanel>(); navigator.SharedPanel.Build(navigator, sharedContent);
            var view = navigator.gameObject.AddComponent<HumanVisionUnifiedDemoCanvas>(); view.navigator = navigator;
            view.adapter = navigator.GetComponent<HumanVisionInputAdapter>();
            view.compactStatus = InputPreviewCanvas.Label(status, "Recognition initializing", 44);
            view.compactStatus.fontSize = 16; view.compactStatus.resizeTextForBestFit = false;
            view.recognitionStatus = InputPreviewCanvas.Label(content, "Recognition initializing", 230);
            view.recognitionStatus.fontSize = 16; view.recognitionStatus.resizeTextForBestFit = false;
            InputPreviewCanvas.Button(navigation, "Settings: show / hide", () => panel.gameObject.SetActive(!panel.gameObject.activeSelf));
        }
        private void Update()
        {
            if (Time.unscaledTime < nextStatus) return; nextStatus = Time.unscaledTime + .25f;
            var manager = navigator.Manager; var contract = navigator.Contract;
            if (adapter == null) adapter = navigator.GetComponent<HumanVisionInputAdapter>();
            string inputError = !string.IsNullOrEmpty(navigator.Input.Source?.LastError) ? navigator.Input.Source.LastError : navigator.Input.LastError;
            string recognitionError = !string.IsNullOrEmpty(adapter?.LastError) ? adapter.LastError : navigator.Bridge.LastError;
            compactStatus.text = !string.IsNullOrEmpty(recognitionError) ? "Recognition error | Open Settings" : manager.IsInitialized
                ? "Ready | " + manager.BodyCount + "/" + manager.MaxBodies + " bodies" : "Recognition unavailable | Open Settings";
            recognitionStatus.text = navigator.Status + "\nSource: " + inputError + "\nRecognition: " + recognitionError + "\n" + (manager.IsInitialized ?
                "Profile: " + manager.ActiveRuntimeProfile + "; raw / sampled / capacity: " + manager.BodyCount + "/" + manager.SampledBodyCount + "/" + manager.MaxBodies +
                "\nRaw fresh complete FPS: " + manager.Stats.InferenceFps.ToString("F2") +
                "\nPreview refresh FPS: " + navigator.Bridge.VideoFrameRate.ToString("F2") +
                "\nOutput sampling/render FPS: unavailable" +
                "\nNative result frame: " + manager.SourceFrameId + "; local age: " + navigator.Bridge.ResultAgeMilliseconds.ToString("F0") + " ms" : "Recognition unavailable; preview remains independent.") +
                (contract == null ? "" : "\nAnalysis detector " + (contract.DetectorWidth == 0 ? "integrated" : contract.DetectorWidth + "x" + contract.DetectorHeight) + "; body " + contract.PoseWidth + "x" + contract.PoseHeight + "; cadence " + contract.DetectionCadence) +
                "\n30 fresh complete observations/s remains unaccepted.";
            recognitionStatus.GetComponent<LayoutElement>().preferredHeight = Mathf.Max(230, recognitionStatus.preferredHeight);
        }
    }
}
