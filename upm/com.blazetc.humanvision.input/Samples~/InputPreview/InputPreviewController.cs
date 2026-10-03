using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

public sealed class InputPreviewController : MonoBehaviour
{
    private InputPreviewControls controls;
    private InputKind kind = InputKind.Video;
    private InputField location, width, height, fps;
    private bool mirror;
    private void Start()
    {
        var safe = InputPreviewCanvas.Root(transform, out var preview);
        controls = gameObject.AddComponent<InputPreviewControls>(); controls.Preview = preview;
        var navigation = InputPreviewCanvas.Row(InputPreviewCanvas.Panel(safe, "Source navigation", new Vector2(0, .88f), Vector2.one));
        foreach (InputKind value in System.Enum.GetValues(typeof(InputKind))) {
            var captured = value;
            InputPreviewCanvas.Button(navigation, value.ToString(), () => { kind = captured; controls.Close(); });
        }
        var panel = InputPreviewCanvas.Panel(safe, "Input settings", new Vector2(0, .22f), new Vector2(.65f, .88f));
        var content = InputPreviewCanvas.Scroll(panel);
        InputPreviewCanvas.Label(content, "Independent preview — no models or SDK", 70);
        location = InputPreviewCanvas.Field(content, "Video path / RTSP URL / camera device", "");
        width = InputPreviewCanvas.Field(content, "Requested width", "1280");
        height = InputPreviewCanvas.Field(content, "Requested height", "720");
        fps = InputPreviewCanvas.Field(content, "Requested FPS", "30");
        InputPreviewCanvas.Button(content, "Mirror: toggle", () => mirror = !mirror);
        InputPreviewCanvas.Button(content, "Start / reconnect", Open);
        InputPreviewCanvas.Button(content, "Stop", controls.Close);
        controls.Status = InputPreviewCanvas.Label(InputPreviewCanvas.Column(InputPreviewCanvas.Panel(safe, "Source status", Vector2.zero, new Vector2(1, .22f))), "Choose a source and press Start", 125);
        InputPreviewCanvas.Button(navigation, "Settings: show / hide", () => panel.gameObject.SetActive(!panel.gameObject.activeSelf));
    }
    private void Open()
    {
        if (!int.TryParse(width.text, out int w) || !int.TryParse(height.text, out int h) || !int.TryParse(fps.text, out int f) || w < 64 || h < 64 || f < 1) {
            controls.Status.text = "Enter valid capture dimensions and FPS."; return;
        }
        HumanVisionSourceSettings settings = kind == InputKind.Rtsp ? new RtspSourceSettings() : new HumanVisionSourceSettings();
        settings.Kind = kind; settings.Location = location.text; settings.DeviceName = location.text;
        settings.RequestedWidth = w; settings.RequestedHeight = h; settings.RequestedFramesPerSecond = f; settings.DisplayMirror = mirror;
        controls.Open(settings);
    }
}
