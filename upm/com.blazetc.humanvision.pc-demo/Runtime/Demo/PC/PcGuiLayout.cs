using UnityEngine;

namespace HumanVision.Demo.PC
{
    // Policies operate in physical screen pixels; GUI.matrix transforms both
    // reference-space drawing and Unity's IMGUI event coordinates.
    internal static class PcGuiLayout
    {
        private static bool Finite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
        public static float Scale(float width, float height, float dpi, float userScale)
        {
            float resolution = Mathf.Max(1, Mathf.Min(width / 1280f, height / 720f));
            float density = Finite(dpi) && dpi >= 72 && dpi <= 384 ? dpi / 96f : 1;
            float requested = Finite(userScale) && userScale > 0 ? Mathf.Clamp(userScale, .75f, 2) : 1;
            float readable = Mathf.Clamp(Mathf.Max(resolution, density), 1, 3) * requested;
            // Retain room for a complete control in small high-DPI windows.
            float fit = Mathf.Max(.1f, Mathf.Min(width / 320f, height / 160f));
            return Mathf.Min(readable, fit);
        }

        public static Rect PanelPixels(float width, float height, Rect safeArea, float dpi, float userScale, bool expanded)
        {
            float scale = Scale(width, height, dpi, userScale);
            float margin = Mathf.Min(8 * scale, Mathf.Min(safeArea.width, safeArea.height) / 8);
            return new Rect(safeArea.x + margin, height - safeArea.yMax + margin,
                Mathf.Min(520 * scale, Mathf.Max(1, safeArea.width - margin * 2)),
                Mathf.Min((expanded ? 950 : 64) * scale, Mathf.Max(1, safeArea.height - margin * 2)));
        }

        //16 units of box padding +22 scrollbar +6 gutter; independent of
        //the natural width of a file path, diagnostic line or button caption.
        public static float ScrollContentWidth(float panelWidthPixels, float scale) => Mathf.Max(1, panelWidthPixels / scale - 44);

        public static float ScrollViewportHeight(float panelHeightPixels, float scale) => Mathf.Max(1, panelHeightPixels / scale - 64);
    }
}
