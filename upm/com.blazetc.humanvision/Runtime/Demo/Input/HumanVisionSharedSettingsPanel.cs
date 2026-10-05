using System;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    public sealed class HumanVisionSharedSettingsPanel : MonoBehaviour
    {
        private InputField count;
        private bool regions;
        private bool windowsCpu;
        private ModelInputQuality quality;
        private Transform qualityContent;
        private Text qualityStatus;
        private readonly System.Collections.Generic.List<Button> qualityButtons = new System.Collections.Generic.List<Button>();
        private readonly System.Collections.Generic.List<string> qualityLabels = new System.Collections.Generic.List<string>();
        public void Build(HumanVisionDemoNavigator navigator, Transform content)
        {
            var layout = content.GetComponent<VerticalLayoutGroup>(); if (layout != null) layout.childControlWidth = true;
            regions = navigator.Shared.UseRegions;
            windowsCpu = navigator.Shared.UseWindowsCpu;
            quality = navigator.Shared.InputQuality;
            InputPreviewCanvas.Label(content, "Shared recognition settings", 35);
            count = InputPreviewCanvas.Field(content, "People (1–8)", navigator.Shared.MaxBodies.ToString());
            qualityContent = InputPreviewCanvas.Column(content);
            qualityContent.GetComponent<VerticalLayoutGroup>().childControlWidth = true;
            qualityContent.gameObject.AddComponent<LayoutElement>();
            qualityStatus = InputPreviewCanvas.Label(qualityContent, "Model Input Quality: preparing availability. Saved draft: " + quality, 70);
            var reset = InputPreviewCanvas.Button(content, "Reset quality draft to Medium (default)", () => {
                quality = ModelInputQuality.Medium; RefreshSelected();
                if (qualityButtons.Count == 0) qualityStatus.text = "Quality draft reset to Medium (default). Save or Apply explicitly; the actual profile input contract remains fixed.";
            });
            var resetText = reset.GetComponentInChildren<Text>(); resetText.resizeTextForBestFit = true; resetText.resizeTextMinSize = 14; resetText.resizeTextMaxSize = 22;
            if (Application.platform != RuntimePlatform.Android) {
                var backend = InputPreviewCanvas.Button(content, windowsCpu ? "PC backend: CPU" : "PC backend: DirectML GPU", () => {});
                backend.onClick.AddListener(() => {
                    windowsCpu = !windowsCpu;
                    backend.GetComponentInChildren<Text>().text = windowsCpu ? "PC backend: CPU" : "PC backend: DirectML GPU";
                });
            }
            var toggle = InputPreviewCanvas.Button(content, "Regions: " + regions, () => {});
            toggle.onClick.AddListener(() => { regions = !regions; toggle.GetComponentInChildren<Text>().text = "Regions: " + regions; });
            InputPreviewCanvas.Button(content, "Edit / finish numbered regions", () => {
                navigator.ApplyShared(); navigator.RegionEditor.SetEditing(!navigator.RegionEditor.Editing);
            });
            InputPreviewCanvas.Label(content, "Drag regions to move; drag bottom-right corners to resize. Regions use existing post-inference assignment.", 100);
            InputPreviewCanvas.Button(content, "Apply shared settings", navigator.ApplyShared);
            InputPreviewCanvas.Button(content, "Save all settings", navigator.Save);
            InputPreviewCanvas.Button(content, "Retry recognition initialization", navigator.RetryRecognition);
        }
        public void RefreshQualityChoices(ModelInputQualityChoice[] choices)
        {
            foreach (var button in qualityButtons) Destroy(button.gameObject);
            qualityButtons.Clear();
            qualityLabels.Clear();
            qualityStatus.text = choices == null || choices.Length == 0
                ? "Model Input Quality unavailable for this profile. Actual contract is fixed; saved Android draft: " + quality
                : "Model Input Quality draft: " + quality + ". Apply to activate; capture size is independent.";
            if (choices != null) foreach (var choice in choices) {
                var selected = choice.Quality;
                string label = selected + " " + choice.Width + "x" + choice.Height;
                string tradeoff = selected == ModelInputQuality.High ? "more input detail / more work" : selected == ModelInputQuality.Low ? "less input detail / less work" : "balanced input detail / work";
                var button = InputPreviewCanvas.Button(qualityContent, label, () => {});
                button.GetComponent<LayoutElement>().preferredHeight = 64;
                var text = button.GetComponentInChildren<Text>(); text.resizeTextForBestFit = true; text.resizeTextMinSize = 14; text.resizeTextMaxSize = 22;
                button.onClick.AddListener(() => { quality = selected; RefreshSelected(); });
                qualityLabels.Add(label + " — " + tradeoff);
                qualityButtons.Add(button);
            }
            RefreshSelected();
        }
        private void RefreshSelected()
        {
            for (int i = 0; i < qualityButtons.Count; ++i) {
                var button = qualityButtons[i];
                var label = qualityLabels[i];
                bool selected = button.name.StartsWith(quality + " ", StringComparison.Ordinal);
                button.GetComponentInChildren<Text>().text = (selected ? "Selected: " : "") + label;
                button.GetComponent<Image>().color = selected ? new Color(.18f, .44f, .45f) : new Color(.12f, .25f, .37f);
            }
            if (qualityButtons.Count != 0) qualityStatus.text = "Model Input Quality draft: " + quality + ". Apply to activate; capture size is independent.";
        }
        public void ReadInto(SharedRecognitionSettings settings)
        {
            if (!int.TryParse(count.text, out int people) || people < 1 || people > 8) throw new ArgumentException("Choose 1–8 people.");
            settings.ResizeRegions(people); settings.UseRegions = regions; settings.UseWindowsCpu = windowsCpu; settings.InputQuality = quality;
        }
    }
}
