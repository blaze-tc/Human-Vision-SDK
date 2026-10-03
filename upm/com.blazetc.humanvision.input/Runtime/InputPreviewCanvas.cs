using System;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanVision.Input
{
    /// <summary>Shared scalable input UI primitives. Contains no recognition dependency.</summary>
    public static class InputPreviewCanvas
    {
        private static Font Font => Resources.GetBuiltinResource<Font>("Arial.ttf");
        public static RectTransform Root(Transform parent, out RawImage preview)
        {
            if (UnityEngine.Object.FindObjectOfType<Camera>() == null) {
                var camera = new GameObject("Preview background", typeof(Camera)).GetComponent<Camera>();
                camera.transform.SetParent(parent, false); camera.clearFlags = CameraClearFlags.SolidColor; camera.backgroundColor = new Color(.025f, .035f, .05f);
            }
            var go = new GameObject("Input Canvas", typeof(RectTransform), typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            go.transform.SetParent(parent, false);
            go.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = go.GetComponent<CanvasScaler>(); scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1280, 720); scaler.matchWidthOrHeight = .5f;
            var image = new GameObject("Independent Preview", typeof(RectTransform), typeof(RawImage), typeof(AspectRatioFitter));
            image.transform.SetParent(go.transform, false); Stretch((RectTransform)image.transform);
            preview = image.GetComponent<RawImage>(); preview.raycastTarget = false;
            image.GetComponent<AspectRatioFitter>().aspectMode = AspectRatioFitter.AspectMode.FitInParent;
            image.GetComponent<AspectRatioFitter>().aspectRatio = 16f / 9;
            var safe = new GameObject("Safe Area", typeof(RectTransform), typeof(InputSafeArea));
            safe.transform.SetParent(go.transform, false); Stretch((RectTransform)safe.transform);
            if (UnityEngine.Object.FindObjectOfType<EventSystem>() == null)
                new GameObject("EventSystem", typeof(EventSystem), typeof(StandaloneInputModule)).transform.SetParent(parent, false);
            return (RectTransform)safe.transform;
        }
        public static RectTransform Panel(Transform parent, string name, Vector2 min, Vector2 max)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image)); go.transform.SetParent(parent, false);
            var rect = (RectTransform)go.transform; rect.anchorMin = min; rect.anchorMax = max;
            rect.offsetMin = new Vector2(8, 8); rect.offsetMax = new Vector2(-8, -8);
            go.GetComponent<Image>().color = new Color(.035f, .06f, .10f, .94f); return rect;
        }
        public static RectTransform Column(Transform parent)
        {
            var go = new GameObject("Controls", typeof(RectTransform), typeof(VerticalLayoutGroup), typeof(ContentSizeFitter));
            go.transform.SetParent(parent, false);
            var layout = go.GetComponent<VerticalLayoutGroup>(); layout.padding = new RectOffset(12, 12, 12, 12);
            layout.spacing = 8; layout.childControlHeight = true; layout.childForceExpandHeight = false; layout.childForceExpandWidth = true;
            go.GetComponent<ContentSizeFitter>().verticalFit = ContentSizeFitter.FitMode.PreferredSize;
            var rect = (RectTransform)go.transform; rect.anchorMin = new Vector2(0, 1); rect.anchorMax = Vector2.one;
            rect.pivot = new Vector2(.5f, 1); rect.offsetMin = rect.offsetMax = Vector2.zero; return rect;
        }
        public static RectTransform Row(Transform parent)
        {
            var go = new GameObject("Navigation controls", typeof(RectTransform), typeof(HorizontalLayoutGroup));
            go.transform.SetParent(parent, false); Stretch((RectTransform)go.transform);
            var layout = go.GetComponent<HorizontalLayoutGroup>(); layout.spacing = 8; layout.padding = new RectOffset(8, 8, 8, 8);
            layout.childControlHeight = layout.childControlWidth = true; layout.childForceExpandWidth = true;
            return (RectTransform)go.transform;
        }
        public static RectTransform Scroll(Transform parent)
        {
            var go = new GameObject("Settings Scroll", typeof(RectTransform), typeof(ScrollRect)); go.transform.SetParent(parent, false); Stretch((RectTransform)go.transform);
            var viewport = new GameObject("Viewport", typeof(RectTransform), typeof(Image), typeof(RectMask2D));
            viewport.transform.SetParent(go.transform, false); Stretch((RectTransform)viewport.transform); viewport.GetComponent<Image>().color = Color.clear;
            var content = Column(viewport.transform); var scroll = go.GetComponent<ScrollRect>();
            scroll.viewport = (RectTransform)viewport.transform; scroll.content = content; scroll.horizontal = false; scroll.movementType = ScrollRect.MovementType.Clamped;
            return content;
        }
        private static GameObject Item(Transform parent, string name, float height, params Type[] components)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(LayoutElement)); go.transform.SetParent(parent, false);
            go.GetComponent<LayoutElement>().preferredHeight = height;
            foreach (var type in components) go.AddComponent(type); return go;
        }
        public static Text Label(Transform parent, string text, float height = 60)
        {
            var label = Item(parent, "Label", height, typeof(Text)).GetComponent<Text>(); label.font = Font;
            label.fontSize = 22; label.color = Color.white; label.text = text; label.supportRichText = false; return label;
        }
        public static Button Button(Transform parent, string text, Action action)
        {
            var go = Item(parent, text, 48, typeof(Image), typeof(Button)); go.GetComponent<Image>().color = new Color(.12f, .25f, .37f);
            var label = Label(go.transform, text); Stretch(label.rectTransform); label.alignment = TextAnchor.MiddleCenter; label.raycastTarget = false;
            var button = go.GetComponent<Button>(); button.onClick.AddListener(() => action()); return button;
        }
        public static InputField Field(Transform parent, string label, string value)
        {
            Label(parent, label, 30);
            var go = Item(parent, label, 48, typeof(Image), typeof(InputField)); go.GetComponent<Image>().color = new Color(.12f, .17f, .22f);
            var text = Label(go.transform, ""); Stretch(text.rectTransform); text.rectTransform.offsetMin = new Vector2(10, 5); text.rectTransform.offsetMax = new Vector2(-10, -5);
            text.alignment = TextAnchor.MiddleLeft; text.raycastTarget = false;
            var field = go.GetComponent<InputField>(); field.textComponent = text; field.text = value ?? ""; return field;
        }
        public static void Stretch(RectTransform rect) { rect.anchorMin = Vector2.zero; rect.anchorMax = Vector2.one; rect.offsetMin = rect.offsetMax = Vector2.zero; }
    }
}
