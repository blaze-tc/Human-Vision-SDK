#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
using HumanVision;
using UnityEngine;
using UnityEngine.UI;
using UI = HumanVisionAcceptance.RuntimeAcceptanceUi;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private RectTransform advancedUi;
        private InteractiveSettings advancedDraftBeforeOpen;
        private InputField advancedCount, advancedConfidence, advancedAge, advancedHandAge, advancedRegions;
        private Toggle advancedPreview, advancedUseRegions, advancedSkeleton;
        private Text advancedStatus;
        private readonly Button[] advancedFps = new Button[3];
        private static readonly int[] AdvancedFrameRates = { 15, 30, 60 };

        private void BuildAdvancedUi(RectTransform host)
        {
            advancedUi = UI.Panel(host, "Recognition Settings Dialog", new Color(0, 0, 0, .72f));
            UI.Fill(advancedUi);
            var card = UI.Panel(advancedUi, "Recognition Settings", UI.Surface);
            Place(card, new Vector2(.22f, .09f), new Vector2(.78f, .91f), Vector2.zero, Vector2.zero);
            var title = UI.Label(card, "识别与显示设置", 25);
            Place(title.rectTransform, new Vector2(0, 1), Vector2.one, new Vector2(20, -63), new Vector2(-20, -12));
            var content = UI.Scroll(card, "Recognition Settings Scroll", 10);
            Place((RectTransform)content.parent.parent, Vector2.zero, Vector2.one, new Vector2(20, 89), new Vector2(-20, -69));
            var display = UI.Row(content, "Display");
            UI.Height(display, 32);
            advancedPreview = UI.Toggle(display, "显示预览", true, value => interactive.Preview = value);
            advancedSkeleton = UI.Toggle(display, "显示骨骼", true, null);
            advancedUseRegions = UI.Toggle(display, "启用识别区域", true, value => interactive.Data.Recognition.UseRegions = value);
            UI.Label(content, "Unity 帧率上限", 18);
            var fps = UI.Row(content, "Unity Frame Rate");
            UI.Height(fps, 36);
            for (int i = 0; i < AdvancedFrameRates.Length; i++) {
                int limit = AdvancedFrameRates[i];
                advancedFps[i] = UI.Button(fps, limit + " FPS", () => {
                    interactive.TargetFps = limit;
                    UpdateAdvancedFrameRateButtons();
                });
            }
            var filters = UI.Row(content, "Recognition Limits");
            advancedCount = AdvancedInput(filters, "人数上限（1–8）");
            advancedConfidence = AdvancedInput(filters, "关节置信度");
            advancedAge = AdvancedInput(filters, "身体时效（ms）");
            advancedHandAge = AdvancedInput(filters, "手时效（ms）");
            var regionLabel = UI.Row(content, "Region Editor");
            UI.Height(regionLabel, 35);
            UI.Label(regionLabel, "识别区域（左上原点，0–1）", 17);
            UI.Button(regionLabel, "按人数横向均分", () => {
                try {
                    interactive.Data.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(ReadInt(advancedCount.text, "人数"));
                    advancedRegions.text = FormatRegions(interactive.Data.Recognition.Regions);
                } catch (System.Exception error) { status = diagnostics.Redact(error.Message); }
            }, 34);
            advancedRegions = UI.TextField(content, "", multiline: true);
            UI.Label(content, "每行输入 x,y,width,height，区域不可重叠。", 15).color = UI.Muted;
            advancedStatus = UI.Label(card, "", 15);
            advancedStatus.color = UI.Muted;
            Place(advancedStatus.rectTransform, Vector2.zero, new Vector2(1, 0), new Vector2(20, 53), new Vector2(-20, 86));
            var actions = UI.Row(card, "Recognition Settings Actions");
            Place(actions, Vector2.zero, new Vector2(1, 0), new Vector2(20, 13), new Vector2(-20, 51));
            UI.Button(actions, "读取", () => { if (LoadBasicOptions()) LoadAdvancedFields(); });
            UI.Button(actions, "保存草稿", () => { ReadAdvancedFields(); SaveBasicOptions(); });
            UI.Button(actions, "取消", CloseAdvancedUi);
            var apply = UI.Button(actions, "应用", () => {
                ReadAdvancedFields();
                ApplyBasicOptions();
                if (!basicOptionsOpen) {
                    showSkeleton = advancedSkeleton.isOn;
                    advancedDraftBeforeOpen = null;
                    advancedUi.gameObject.SetActive(false);
                }
            });
            UI.Select(apply, true);
            advancedUi.gameObject.SetActive(false);
        }

        private static InputField AdvancedInput(Transform parent, string label)
        {
            var column = UI.Stack(parent, label, 4);
            UI.Label(column, label, 16);
            return UI.TextField(column, "");
        }

        private void ShowAdvancedUi()
        {
            if (interactive == null || operating || advancedUi.gameObject.activeSelf) return;
            advancedDraftBeforeOpen = interactive;
            interactive = JsonUtility.FromJson<InteractiveSettings>(JsonUtility.ToJson(interactive));
            OpenBasicOptions();
            LoadAdvancedFields();
            advancedUi.gameObject.SetActive(true);
        }

        private void LoadAdvancedFields()
        {
            advancedCount.text = capacityText;
            advancedConfidence.text = confidenceText;
            advancedAge.text = ageText;
            advancedHandAge.text = handAgeText;
            advancedRegions.text = regionsText;
            advancedPreview.SetIsOnWithoutNotify(interactive.Preview);
            advancedUseRegions.SetIsOnWithoutNotify(interactive.Data.Recognition.UseRegions);
            advancedSkeleton.SetIsOnWithoutNotify(showSkeleton);
            UpdateAdvancedFrameRateButtons();
        }

        private void ReadAdvancedFields()
        {
            capacityText = advancedCount.text;
            confidenceText = advancedConfidence.text;
            ageText = advancedAge.text;
            handAgeText = advancedHandAge.text;
            regionsText = advancedRegions.text;
        }

        private void UpdateAdvancedFrameRateButtons()
        {
            for (int i = 0; i < advancedFps.Length; i++)
                UI.Select(advancedFps[i], AdvancedFrameRates[i] == interactive.TargetFps);
        }

        private void CloseAdvancedUi()
        {
            if (advancedDraftBeforeOpen != null) interactive = advancedDraftBeforeOpen;
            advancedDraftBeforeOpen = null;
            basicOptionsOpen = false;
            advancedUi.gameObject.SetActive(false);
        }

        private void RefreshAdvancedUi()
        {
            if (advancedUi.gameObject.activeSelf)
                advancedStatus.text = diagnostics.Redact(status);
        }
    }
}
#endif
