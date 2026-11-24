#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
using System;
using HumanVision;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        [Header("识别与显示弹窗")]
        [SerializeField] private RectTransform advancedUi;
        [SerializeField] private Text advancedStatus;
        [SerializeField] private Button advancedLoad;
        [SerializeField] private Button advancedSave;
        [SerializeField] private Button advancedCancel;
        [SerializeField] private Button advancedApply;

        [Header("显示与帧率")]
        [SerializeField] private Toggle advancedPreview;
        [SerializeField] private Toggle advancedSkeleton;
        [SerializeField] private Button[] advancedFps;
        [SerializeField] private Text[] advancedFpsLabels;

        [Header("识别参数与区域")]
        [SerializeField] private InputField advancedCount;
        [SerializeField] private InputField advancedConfidence;
        [SerializeField] private InputField advancedAge;
        [SerializeField] private InputField advancedHandAge;
        [SerializeField] private InputField advancedRegions;
        [SerializeField] private Toggle advancedUseRegions;
        [SerializeField] private Button advancedDistributeRegions;

        private static readonly int[] AdvancedFrameRates = { 15, 30, 60 };
        private InteractiveSettings advancedDraftBeforeOpen;
        private bool advancedUiBound;

        private bool BindAdvancedUi()
        {
            if (advancedUiBound) return true;
            if (advancedUi == null || advancedStatus == null || advancedLoad == null || advancedSave == null ||
                advancedCancel == null || advancedApply == null || advancedPreview == null ||
                advancedSkeleton == null || advancedCount == null || advancedConfidence == null ||
                advancedAge == null || advancedHandAge == null || advancedRegions == null ||
                advancedUseRegions == null || advancedDistributeRegions == null || !AreAdvancedFrameRateButtonsAssigned())
            {
                Debug.LogError("RuntimePrefabAcceptance：请在 Inspector 检查识别与显示弹窗引用。", this);
                return false;
            }

            advancedLoad.onClick.AddListener(LoadAdvancedSettings);
            advancedSave.onClick.AddListener(SaveAdvancedSettings);
            advancedCancel.onClick.AddListener(CloseAdvancedUi);
            advancedApply.onClick.AddListener(ApplyAdvancedSettings);
            advancedPreview.onValueChanged.AddListener(SetAdvancedPreview);
            advancedUseRegions.onValueChanged.AddListener(SetAdvancedUseRegions);
            advancedDistributeRegions.onClick.AddListener(DistributeAdvancedRegions);
            for (int index = 0; index < AdvancedFrameRates.Length; index++)
            {
                int frameRate = AdvancedFrameRates[index];
                advancedFps[index].onClick.AddListener(() => SetAdvancedFrameRate(frameRate));
            }

            advancedUi.gameObject.SetActive(false);
            advancedUiBound = true;
            return true;
        }

        private bool AreAdvancedFrameRateButtonsAssigned()
        {
            if (advancedFps == null || advancedFps.Length != AdvancedFrameRates.Length ||
                advancedFpsLabels == null || advancedFpsLabels.Length != AdvancedFrameRates.Length) return false;

            for (int index = 0; index < AdvancedFrameRates.Length; index++)
            {
                if (advancedFps[index] == null || advancedFpsLabels[index] == null) return false;
            }
            return true;
        }

        private void ShowAdvancedUi()
        {
            if (!advancedUiBound || interactive == null || operating || advancedUi.gameObject.activeSelf) return;

            advancedDraftBeforeOpen = interactive;
            interactive = JsonUtility.FromJson<InteractiveSettings>(JsonUtility.ToJson(interactive));
            OpenBasicOptions();
            LoadAdvancedFields();
            advancedUi.gameObject.SetActive(true);
        }

        private void LoadAdvancedSettings()
        {
            if (LoadBasicOptions()) LoadAdvancedFields();
        }

        private void SaveAdvancedSettings()
        {
            ReadAdvancedFields();
            SaveBasicOptions();
        }

        private void ApplyAdvancedSettings()
        {
            ReadAdvancedFields();
            ApplyBasicOptions();
            if (basicOptionsOpen) return;

            showSkeleton = advancedSkeleton.isOn;
            advancedDraftBeforeOpen = null;
            advancedUi.gameObject.SetActive(false);
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

        private void SetAdvancedPreview(bool enabled)
        {
            interactive.Preview = enabled;
        }

        private void SetAdvancedUseRegions(bool enabled)
        {
            interactive.Data.Recognition.UseRegions = enabled;
        }

        private void SetAdvancedFrameRate(int frameRate)
        {
            interactive.TargetFps = frameRate;
            UpdateAdvancedFrameRateButtons();
        }

        private void UpdateAdvancedFrameRateButtons()
        {
            for (int index = 0; index < advancedFps.Length; index++)
            {
                bool isSelected = AdvancedFrameRates[index] == interactive.TargetFps;
                SetDialogButtonSelected(advancedFps[index], advancedFpsLabels[index], isSelected);
            }
        }

        private void DistributeAdvancedRegions()
        {
            try
            {
                int count = ReadInt(advancedCount.text, "人数");
                interactive.Data.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(count);
                advancedRegions.text = FormatRegions(interactive.Data.Recognition.Regions);
            }
            catch (ArgumentException error)
            {
                status = diagnostics.Redact(error.Message);
            }
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
            if (advancedUiBound && advancedUi.gameObject.activeSelf)
                advancedStatus.text = diagnostics.Redact(status);
        }
    }
}
#endif
