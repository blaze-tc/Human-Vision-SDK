using System;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVisionAcceptance
{
    internal static class RuntimeAcceptanceUi
    {
        internal static readonly Color Background = new Color(.039f, .061f, .093f);
        internal static readonly Color Surface = new Color(.088f, .123f, .173f);
        internal static readonly Color Field = new Color(.035f, .057f, .087f);
        internal static readonly Color Border = new Color(.20f, .27f, .35f);
        internal static readonly Color Accent = new Color(.25f, .91f, 1f);
        internal static readonly Color Ink = new Color(.94f, .97f, 1f);
        internal static readonly Color Muted = new Color(.69f, .77f, .85f);
        internal static Font Font { get; set; }

        internal static RectTransform Rect(Transform parent, string name)
        {
            var node = new GameObject(name, typeof(RectTransform));
            node.transform.SetParent(parent, false);
            return (RectTransform)node.transform;
        }

        internal static RectTransform Panel(Transform parent, string name, Color color)
        {
            var rect = Rect(parent, name);
            rect.gameObject.AddComponent<Image>().color = color;
            return rect;
        }

        internal static void Fill(RectTransform rect, int padding = 0)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = new Vector2(padding, padding);
            rect.offsetMax = new Vector2(-padding, -padding);
        }

        internal static void Height(Component child, float value)
        {
            var layout = child.GetComponent<LayoutElement>() ?? child.gameObject.AddComponent<LayoutElement>();
            layout.minHeight = value;
            layout.preferredHeight = value;
        }

        internal static RectTransform Stack(Transform parent, string name, float gap = 8)
        {
            var rect = Rect(parent, name);
            var layout = rect.gameObject.AddComponent<VerticalLayoutGroup>();
            layout.spacing = gap;
            layout.childControlHeight = true;
            layout.childControlWidth = true;
            layout.childForceExpandHeight = false;
            layout.childForceExpandWidth = true;
            return rect;
        }

        internal static RectTransform Row(Transform parent, string name, float gap = 8)
        {
            var rect = Rect(parent, name);
            var layout = rect.gameObject.AddComponent<HorizontalLayoutGroup>();
            layout.spacing = gap;
            layout.childControlHeight = true;
            layout.childControlWidth = true;
            layout.childForceExpandHeight = true;
            layout.childForceExpandWidth = true;
            return rect;
        }

        internal static Text Label(Transform parent, string value, int size = 18)
        {
            var rect = Rect(parent, string.IsNullOrEmpty(value) ? "Label" : value);
            var text = rect.gameObject.AddComponent<Text>();
            text.font = Font != null ? Font : Resources.GetBuiltinResource<Font>("Arial.ttf");
            text.fontSize = size;
            text.text = value;
            text.color = Ink;
            text.supportRichText = false;
            text.raycastTarget = false;
            text.alignment = TextAnchor.MiddleLeft;
            return text;
        }

        internal static Button Button(Transform parent, string value, Action action, float height = 38)
        {
            var rect = Panel(parent, value, Border);
            var button = rect.gameObject.AddComponent<Button>();
            button.targetGraphic = rect.GetComponent<Image>();
            var colors = button.colors;
            colors.normalColor = Color.white;
            colors.highlightedColor = new Color(1f, 1f, 1f, .80f);
            colors.pressedColor = new Color(.65f, .72f, .84f);
            colors.disabledColor = new Color(.56f, .59f, .64f, .62f);
            colors.fadeDuration = .08f;
            button.colors = colors;
            var text = Label(rect, value);
            text.alignment = TextAnchor.MiddleCenter;
            Fill(text.rectTransform, 4);
            Height(button, height);
            if (action != null) button.onClick.AddListener(() => action());
            return button;
        }

        internal static void Select(Button button, bool selected)
        {
            button.targetGraphic.color = selected ? Accent : Border;
            button.GetComponentInChildren<Text>(true).color = selected ? Background : Ink;
        }

        internal static void Caption(Button button, string value)
        {
            var label = button.GetComponentInChildren<Text>(true);
            if (label.text != value) label.text = value;
        }

        internal static InputField TextField(Transform parent, string initial, bool multiline = false, bool secret = false)
        {
            var rect = Panel(parent, "Text Input", Field);
            var input = rect.gameObject.AddComponent<InputField>();
            input.targetGraphic = rect.GetComponent<Image>();
            var text = Label(rect, "", 18);
            text.alignment = multiline ? TextAnchor.UpperLeft : TextAnchor.MiddleLeft;
            text.supportRichText = false;
            Fill(text.rectTransform, 8);
            input.textComponent = text;
            input.lineType = multiline ? InputField.LineType.MultiLineNewline : InputField.LineType.SingleLine;
            if (secret) input.contentType = InputField.ContentType.Password;
            input.text = initial ?? "";
            Height(input, multiline ? 108 : 36);
            return input;
        }

        internal static Toggle Toggle(Transform parent, string label, bool initial, Action<bool> changed)
        {
            var row = Row(parent, label);
            var toggle = row.gameObject.AddComponent<Toggle>();
            var frame = Panel(row, "Check Box", Border);
            var layout = frame.gameObject.AddComponent<LayoutElement>();
            layout.minWidth = layout.preferredWidth = 24;
            layout.flexibleWidth = 0;
            var check = Panel(frame, "Check Mark", Accent);
            Fill(check, 5);
            toggle.targetGraphic = frame.GetComponent<Image>();
            toggle.graphic = check.GetComponent<Image>();
            Label(row, label).raycastTarget = true;
            toggle.SetIsOnWithoutNotify(initial);
            Height(toggle, 30);
            if (changed != null) toggle.onValueChanged.AddListener(value => changed(value));
            return toggle;
        }

        internal static RectTransform Scroll(Transform parent, string name, float gap = 8)
        {
            var rect = Rect(parent, name);
            var scroll = rect.gameObject.AddComponent<ScrollRect>();
            scroll.horizontal = false;
            scroll.vertical = true;
            scroll.movementType = ScrollRect.MovementType.Clamped;
            scroll.scrollSensitivity = 36;
            var viewport = Panel(rect, "Viewport", Color.clear);
            Fill(viewport);
            viewport.gameObject.AddComponent<RectMask2D>();
            var content = Stack(viewport, "Content", gap);
            content.anchorMin = new Vector2(0, 1);
            content.anchorMax = Vector2.one;
            content.pivot = new Vector2(.5f, 1);
            content.offsetMin = Vector2.zero;
            content.offsetMax = Vector2.zero;
            content.gameObject.AddComponent<ContentSizeFitter>().verticalFit = ContentSizeFitter.FitMode.PreferredSize;
            scroll.viewport = viewport;
            scroll.content = content;
            return content;
        }
    }
}
