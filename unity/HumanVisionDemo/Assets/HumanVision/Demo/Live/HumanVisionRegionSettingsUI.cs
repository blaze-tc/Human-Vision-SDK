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
        private string _summary = "", _peopleLabel = "";
        private float _nextTextUpdate;

        private void Start() { RefreshDevices(); }
        private void RefreshDevices() { _devices = WebCamTexture.devices; }
        private void OnGUI()
        {
            if (manager == null) return;
            if (GUI.Button(new Rect(12, 12, 140, 30), showSettings ? "Hide settings" : "Camera settings")) showSettings = !showSettings;
            if (Time.unscaledTime >= _nextTextUpdate) {
                _nextTextUpdate = Time.unscaledTime + .25f;
                _summary = manager.InputStatus + " | Bodies: " + manager.GetUsersCount();
                _peopleLabel = "n=" + manager.Settings.people;
            }
            GUI.Label(new Rect(160, 12, Screen.width - 172, 28), _summary);
            if (!showSettings) return;
            var settings = manager.Settings;
            GUI.Box(new Rect(12, 48, 480, 252), "Camera and recognition regions");
            int source = GUI.Toolbar(new Rect(24, 75, 280, 26), (int)settings.source, SourceNames);
            settings.source = (HumanVisionCameraKind)source;
            if (settings.source == HumanVisionCameraKind.WebCamera) {
                GUI.Label(new Rect(24, 108, 350, 22), string.IsNullOrEmpty(settings.deviceName) ? "Default system camera" : settings.deviceName);
                if (GUI.Button(new Rect(380, 105, 96, 26), "Next device")) {
                    RefreshDevices();
                    if (_devices.Length > 0) {
                        int selected = -1;
                        for (int i = 0; i < _devices.Length; i++) if (_devices[i].name == settings.deviceName) selected = i;
                        settings.deviceName = _devices[(selected + 1) % _devices.Length].name;
                    }
                }
            } else {
                settings.rtspUrl = GUI.TextField(new Rect(24, 108, 340, 24), settings.rtspUrl);
                settings.rtspTcp = GUI.Toggle(new Rect(380, 108, 90, 24), settings.rtspTcp, "TCP");
            }
            GUI.Label(new Rect(24, 139, 52, 25), "People");
            _countText = GUI.TextField(new Rect(78, 137, 35, 25), _countText, 2);
            if (GUI.Button(new Rect(120, 137, 65, 25), "Set") && int.TryParse(_countText, out int count) && count >= 1 && count <= 8)
                settings.ResizeRegions(count);
            GUI.Label(new Rect(190, 139, 55, 25), _peopleLabel);
            settings.useRegions = GUI.Toggle(new Rect(247, 138, 120, 25), settings.useRegions, "Use regions");
            settings.mirror = GUI.Toggle(new Rect(380, 138, 85, 25), settings.mirror, "Mirror");
            if (GUI.Button(new Rect(24, 170, 80, 27), "Start")) { _editing = false; manager.StartCamera(); }
            if (GUI.Button(new Rect(110, 170, 80, 27), "Stop")) manager.StopCamera();
            if (GUI.Button(new Rect(196, 170, 90, 27), "Edit regions")) _editing = !_editing;
            if (GUI.Button(new Rect(292, 170, 85, 27), "Apply")) { _editing = false; manager.ApplySettings(); }
            if (GUI.Button(new Rect(384, 170, 92, 27), "Save")) { _editing = false; manager.SaveSettings(); }
            if (GUI.Button(new Rect(24, 204, 90, 25), "Load saved")) { manager.LoadSettings(); _countText = settings.people.ToString(); }
            GUI.Label(new Rect(122, 204, 354, 30), "Drag a box to move; bottom-right corner to resize.");
            GUI.Label(new Rect(24, 238, 450, 54), manager.Status);
            if (settings.useRegions && settings.regions != null && preview != null && preview.texture != null) DrawRegions(settings);
        }

        private void DrawRegions(HumanVisionCameraSettings settings)
        {
            preview.rectTransform.GetWorldCorners(_corners);
            Vector2 bottomLeft = RectTransformUtility.WorldToScreenPoint(null, _corners[0]);
            Vector2 topRight = RectTransformUtility.WorldToScreenPoint(null, _corners[2]);
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
                Rect handle = new Rect(screen.xMax - 18, screen.yMax - 18, 18, 18);
                if (_editing) GUI.DrawTexture(handle, Texture2D.whiteTexture);
                GUI.color = old;
                if (_editing && e.type == EventType.MouseDown && e.button == 0 && screen.Contains(e.mousePosition) &&
                    !new Rect(12, 48, 480, 252).Contains(e.mousePosition)) {
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
