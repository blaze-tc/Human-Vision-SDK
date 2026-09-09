using UnityEngine;
using UnityEngine.UI;

namespace HumanVision
{
    public sealed class HumanVisionRegionSettingsUI : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        public RawImage preview;
        public bool showSettings = true;
        private readonly Vector3[] _corners = new Vector3[4];
        private int _dragIndex = -1;
        private bool _resize;
        private Vector2 _dragStart;
        private Rect _original;
        private WebCamDevice[] _devices;
        private string _countText = "4";
        private bool _editing;
        private static readonly string[] SourceNames = { "WebCamera", "RTSP" };
        private static readonly string[] RegionNames = { "Region 0", "Region 1", "Region 2", "Region 3", "Region 4", "Region 5", "Region 6", "Region 7" };

        private void Start() { RefreshDevices(); }
        private void RefreshDevices() { _devices = WebCamTexture.devices; }
        private Vector2 _scroll;
        private Rect _panelPixels, _headerPixels, _footerPixels;
        private void OnGUI()
        {
            if (manager == null) return;
            var settings = manager.Settings;
            using (var ui = new HumanVisionMobileGui.Scope()) {
                var header = new Rect(10, 10, Mathf.Min(540, ui.Width - 20), 50);
                _headerPixels = ui.ToPixels(header);
                _footerPixels = ui.ToPixels(new Rect(0, ui.Height - 150, ui.Width, 150));
                if (GUI.Button(header, showSettings ? "CAMERA SETTINGS  /  Collapse" : "CAMERA SETTINGS  /  Expand")) showSettings = !showSettings;
                _panelPixels = new Rect();
                if (showSettings) {
                    var panel = new Rect(10, 68, header.width, Mathf.Max(60, Mathf.Min(510, ui.Height - 228)));
                    _panelPixels = ui.ToPixels(panel);
                    GUILayout.BeginArea(panel, GUI.skin.box);
                    _scroll = GUILayout.BeginScrollView(_scroll);
                    settings.source = (HumanVisionCameraKind)GUILayout.Toolbar((int)settings.source, SourceNames);
                    if (settings.source == HumanVisionCameraKind.WebCamera) {
                        GUILayout.BeginHorizontal();
                        GUILayout.Label(string.IsNullOrEmpty(settings.deviceName) ? "System camera" : settings.deviceName);
                        if (GUILayout.Button("Next camera", GUILayout.Width(145))) {
                            RefreshDevices();
                            if (_devices.Length > 0) {
                                int selected = -1;
                                for (int i = 0; i < _devices.Length; i++) if (_devices[i].name == settings.deviceName) selected = i;
                                settings.deviceName = _devices[(selected + 1) % _devices.Length].name;
                            }
                        }
                        GUILayout.EndHorizontal();
                    } else {
                        settings.rtspUrl = GUILayout.TextField(settings.rtspUrl);
                        settings.rtspTcp = GUILayout.Toggle(settings.rtspTcp, "RTSP over TCP");
                    }
                    GUILayout.BeginHorizontal();
                    GUILayout.Label("People: " + settings.people);
                    _countText = GUILayout.TextField(_countText, 2, GUILayout.Width(60));
                    if (GUILayout.Button("Set", GUILayout.Width(80)) && int.TryParse(_countText, out int count) && count >= 1 && count <= 8)
                        settings.ResizeRegions(count);
                    GUILayout.EndHorizontal();
                    GUILayout.BeginHorizontal();
                    settings.useRegions = GUILayout.Toggle(settings.useRegions, "Use regions");
                    settings.mirror = GUILayout.Toggle(settings.mirror, "Mirror image");
                    GUILayout.EndHorizontal();
                    GUILayout.BeginHorizontal();
                    if (GUILayout.Button(_editing ? "Finish editing" : "Edit regions")) { _editing = !_editing; if (_editing) showSettings = false; }
                    if (GUILayout.Button("Apply")) { _editing = false; manager.StartCamera(); }
                    GUILayout.EndHorizontal();
                    GUILayout.BeginHorizontal();
                    if (GUILayout.Button("Save")) { _editing = false; manager.SaveSettings(); }
                    if (GUILayout.Button("Load")) { manager.LoadSettings(); _countText = manager.Settings.people.ToString(); }
                    GUILayout.EndHorizontal();
                    GUILayout.Label("Drag a box to move it. Drag its corner to resize. Expand settings to save.");
                    GUILayout.Label(manager.Status);
                    GUILayout.EndScrollView();
                    GUILayout.EndArea();
                }
            }
            // GUI.matrix is restored here: boxes and pointer positions share physical screen pixels.
            if (settings.useRegions && settings.regions != null && preview != null && preview.texture != null) DrawRegions(settings);
        }

        private void DrawRegions(HumanVisionCameraSettings settings)
        {
            preview.rectTransform.GetWorldCorners(_corners);
            Vector2 bottomLeft = RectTransformUtility.WorldToScreenPoint(preview.canvas.renderMode == RenderMode.ScreenSpaceOverlay ? null : preview.canvas.worldCamera, _corners[0]);
            Vector2 topRight = RectTransformUtility.WorldToScreenPoint(preview.canvas.renderMode == RenderMode.ScreenSpaceOverlay ? null : preview.canvas.worldCamera, _corners[2]);
            Rect image = new Rect(bottomLeft.x, Screen.height - topRight.y, topRight.x - bottomLeft.x, topRight.y - bottomLeft.y);
            if (image.width < 1 || image.height < 1) return;
            Event e = Event.current;
            for (int i = 0; i < settings.regions.Length; i++) {
                Rect r = settings.regions[i];
                Rect screen = new Rect(image.x + r.x * image.width, image.y + r.y * image.height,
                    r.width * image.width, r.height * image.height);
                Color old = GUI.color;
                GUI.color = Color.HSVToRGB((i * .137f) % 1, .8f, 1);
                GUI.DrawTexture(new Rect(screen.x, screen.y, screen.width, 2), Texture2D.whiteTexture);
                GUI.DrawTexture(new Rect(screen.x, screen.yMax - 2, screen.width, 2), Texture2D.whiteTexture);
                GUI.DrawTexture(new Rect(screen.x, screen.y, 2, screen.height), Texture2D.whiteTexture);
                GUI.DrawTexture(new Rect(screen.xMax - 2, screen.y, 2, screen.height), Texture2D.whiteTexture);
                GUI.Label(new Rect(screen.x + 5, screen.y + 5, 120, 24), RegionNames[i]);
                float handleSize = 28 * HumanVisionMobileGui.Scale;
                Rect handle = new Rect(screen.xMax - handleSize, screen.yMax - handleSize, handleSize, handleSize);
                if (_editing) GUI.DrawTexture(handle, Texture2D.whiteTexture);
                GUI.color = old;
                if (_editing && e.type == EventType.MouseDown && e.button == 0 && screen.Contains(e.mousePosition) &&
                    !_panelPixels.Contains(e.mousePosition) && !_headerPixels.Contains(e.mousePosition) && !_footerPixels.Contains(e.mousePosition)) {
                    _dragIndex = i; _resize = handle.Contains(e.mousePosition); _dragStart = e.mousePosition; _original = r; e.Use();
                }
            }
            if (_editing && _dragIndex >= 0 && _dragIndex < settings.regions.Length && e.type == EventType.MouseDrag) {
                Vector2 delta = e.mousePosition - _dragStart;
                delta = new Vector2(delta.x / image.width, delta.y / image.height);
                Rect next = _original;
                if (_resize) { next.width = Mathf.Clamp(_original.width + delta.x, .01f, 1 - next.x); next.height = Mathf.Clamp(_original.height + delta.y, .01f, 1 - next.y); }
                else { next.x = Mathf.Clamp(_original.x + delta.x, 0, 1 - next.width); next.y = Mathf.Clamp(_original.y + delta.y, 0, 1 - next.height); }
                settings.regions[_dragIndex] = next; e.Use();
            }
            if (e.type == EventType.MouseUp) _dragIndex = -1;
        }
    }
}
