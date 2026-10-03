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
        public void Build(HumanVisionDemoNavigator navigator, Transform content)
        {
            regions = navigator.Shared.UseRegions;
            InputPreviewCanvas.Label(content, "Shared recognition settings", 35);
            count = InputPreviewCanvas.Field(content, "People (1–8)", navigator.Shared.MaxBodies.ToString());
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
        public void ReadInto(SharedRecognitionSettings settings)
        {
            if (!int.TryParse(count.text, out int people) || people < 1 || people > 8) throw new ArgumentException("Choose 1–8 people.");
            settings.ResizeRegions(people); settings.UseRegions = regions;
        }
    }
}
