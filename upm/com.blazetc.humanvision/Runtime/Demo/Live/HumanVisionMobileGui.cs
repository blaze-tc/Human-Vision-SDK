using System;
using UnityEngine;

namespace HumanVision
{
    // Shared IMGUI units: the short screen edge is 480 units on a phone.
    // Region manipulation remains in screen pixels, outside this scope.
    public static class HumanVisionMobileGui
    {
        private static GUISkin _skin;
        public static float Scale => Application.isMobilePlatform
            ? Mathf.Clamp(Mathf.Min(Screen.width, Screen.height) / 480f, .65f, 4f)
            : Mathf.Max(1f, Mathf.Min(Screen.width / 1280f, Screen.height / 720f));
        public static Rect SafePixels {
            get { var r = Screen.safeArea; return new Rect(r.x, Screen.height - r.yMax, r.width, r.height); }
        }
        private static Texture2D Fill(Color color)
        {
            var texture = new Texture2D(1, 1) { hideFlags = HideFlags.HideAndDontSave };
            texture.SetPixel(0, 0, color); texture.Apply(); return texture;
        }
        private static void Prepare()
        {
            if (_skin != null) return;
            _skin = UnityEngine.Object.Instantiate(GUI.skin);
            _skin.hideFlags = HideFlags.HideAndDontSave;
            var panel = Fill(new Color(.045f, .07f, .11f, .94f));
            var button = Fill(new Color(.12f, .22f, .29f, .97f));
            var active = Fill(new Color(.06f, .48f, .47f, 1));
            foreach (var style in new[] { _skin.button, _skin.toggle, _skin.textField, _skin.label, _skin.box }) {
                style.fontSize = 19; style.normal.textColor = new Color(.92f, .96f, 1);
                style.wordWrap = true; style.padding = new RectOffset(12, 12, 8, 8);
            }
            _skin.button.fixedHeight = _skin.textField.fixedHeight = _skin.toggle.fixedHeight = 50;
            _skin.button.normal.background = button;
            _skin.button.hover.background = _skin.button.active.background = active;
            _skin.button.onNormal.background = _skin.button.onHover.background = active;
            _skin.button.hover.textColor = _skin.button.active.textColor = Color.white;
            _skin.toggle.normal.background = _skin.toggle.hover.background = button;
            _skin.toggle.onNormal.background = _skin.toggle.onHover.background = active;
            _skin.toggle.normal.textColor = _skin.toggle.onNormal.textColor = Color.white;
            _skin.toggle.border = new RectOffset();
            _skin.toggle.padding = new RectOffset(12, 12, 8, 8);
            _skin.textField.normal.background = panel;
            _skin.box.normal.background = panel;
            _skin.box.alignment = TextAnchor.UpperLeft;
            _skin.label.fixedHeight = 0;
            _skin.verticalScrollbar.fixedWidth = 22;
            _skin.verticalScrollbarThumb.fixedWidth = 22;
        }
        public sealed class Scope : IDisposable
        {
            private readonly Matrix4x4 _matrix = GUI.matrix;
            private readonly GUISkin _previous = GUI.skin;
            private readonly Rect _safe = SafePixels;
            private readonly float _scale = Scale;
            public float Width => _safe.width / _scale;
            public float Height => _safe.height / _scale;
            public Scope()
            {
                Prepare(); GUI.skin = _skin;
                GUI.matrix = Matrix4x4.TRS(new Vector3(_safe.x, _safe.y, 0), Quaternion.identity, Vector3.one * _scale);
            }
            public Rect ToPixels(Rect rect) => new Rect(_safe.x + rect.x * _scale, _safe.y + rect.y * _scale, rect.width * _scale, rect.height * _scale);
            public void Dispose() { GUI.matrix = _matrix; GUI.skin = _previous; }
        }
    }
}
