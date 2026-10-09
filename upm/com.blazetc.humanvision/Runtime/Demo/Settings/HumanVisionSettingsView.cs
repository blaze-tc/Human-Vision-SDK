using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    /// <summary>普通可编辑 UGUI 的引用和草稿交互。不会直接更改 Native 或自动保存。</summary>
    public sealed partial class HumanVisionSettingsView : MonoBehaviour
    {
        [SerializeField] private RawImage preview;
        [SerializeField] private HumanVisionOverlay overlay;
        [SerializeField] private Text status, qualityHint, sourceHint;
        [SerializeField] private InputField[] fields;
        [SerializeField] private Button[] buttons;
        [SerializeField] private Dropdown peopleChoice, qualityChoice, cameraChoice, videoChoice, captureChoice, accelerationChoice;
        [SerializeField] private GameObject cameraPanel, videoPanel, rtspPanel, advancedPanel, regionDetailsPanel;
        [SerializeField] private HumanVisionSettingsRegionHandle[] regionHandles;
        private HumanVisionSettingsController controller;
        private HumanVisionSettingsData draft;
        private ModelInputQualityChoice[] qualities = Array.Empty<ModelInputQualityChoice>();
        private string[] cameras = Array.Empty<string>(), videos = Array.Empty<string>();
        private string[] bundledVideos = Array.Empty<string>();
        // 分辨率与 FPS 分开配置；只在草稿交互时重建选项，不进入识别热路径。
        private static readonly Vector2Int[] capturePresets = {
            new Vector2Int(640, 480), new Vector2Int(1280, 720),
            new Vector2Int(1920, 1080), new Vector2Int(3840, 2160)
        };
        private Vector2Int[] captureSizes = Array.Empty<Vector2Int>();
        private bool wired, editRegions, fullPreview;
        private int selectedRegion;
        private RuntimePlatform? executionPlatform;
        private bool AndroidExecution => (executionPlatform ?? Application.platform) == RuntimePlatform.Android;
        internal void ConfigureExecutionPlatform(RuntimePlatform platform) { executionPlatform = platform; EnsureAccelerationControl(); }
        public RawImage Preview => preview;
        public HumanVisionOverlay Overlay => overlay;
        public HumanVisionSettingsData Draft => draft?.Clone();
        /// <summary>绑定控制器一次；运行时监听器不写入场景资产。</summary>
        public void Bind(HumanVisionSettingsController owner)
        {
            controller = owner; if (wired) return; wired = true;
            EnsureAccelerationControl();
            foreach (var button in buttons) {
                string command = button.name;
                button.onClick.AddListener(() => controller.Execute(command));
            }
            peopleChoice.onValueChanged.AddListener(_ => controller.Edit(ReadDraft));
            qualityChoice.onValueChanged.AddListener(_ => controller.Edit(ReadDraft));
            if (accelerationChoice != null) accelerationChoice.onValueChanged.AddListener(_ => controller.Edit(ReadDraft));
            cameraChoice.onValueChanged.AddListener(i => { if (draft != null && i < cameras.Length) { Put("CameraDevice", cameras[i]); } });
            videoChoice.onValueChanged.AddListener(i => { if (draft != null && i < videos.Length) { Put("VideoPath", videos[i]); } });
            captureChoice.onValueChanged.AddListener(_ => controller.Edit(ReadDraft));
            foreach (var region in regionHandles) region.Changed = (index, rect) => {
                selectedRegion = index; draft.Recognition.Regions[index] = rect; ShowRegionFields();
                // 重叠仅存在于草稿；整组 Apply 会验证，不偷偷挪动其它人的区域。
            };
            RefreshSources();
        }
        /// <summary>所有输入字段先读到副本；解析异常保留原草稿和当前运行配置。</summary>
        public HumanVisionSettingsData ReadDraft()
        {
            var value = draft.Clone(); var mode = value.Mode;
            int count = peopleChoice.value + 1;
            if (count != value.Recognition.MaxBodies) {
                value.Recognition.MaxBodies = count; value.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(count);
            }
            mode.CameraDevice = Field("CameraDevice").text; mode.VideoPath = Field("VideoPath").text; mode.RtspUrl = Field("RtspUrl").text;
            // 自定义路径可留空，使用下拉框中实际选中的随包视频。
            if (value.SourceKind == InputKind.Video && string.IsNullOrWhiteSpace(mode.VideoPath) && videoChoice.value < videos.Length)
                mode.VideoPath = videos[videoChoice.value];
            mode.RtspComputerHost = Field("RtspHost").text;
            var size = captureSizes[captureChoice.value];
            mode.RequestedWidth = size.x; mode.RequestedHeight = size.y; mode.RequestedFramesPerSecond = Integer("FPS");
            mode.LineWidth = Number("LineWidth"); mode.PointDiameter = Number("PointSize");
            value.StatisticsInterval = Number("LogInterval"); value.SkeletonLogInterval = Number("PoseLogInterval");
            value.LogFileMegabytes = Integer("LogFileMB"); value.RetainedLogSessions = Integer("LogSessions");
            if (qualities.Length > qualityChoice.value) value.InputQuality = qualities[qualityChoice.value].Quality;
            if (accelerationChoice != null) {
                if (AndroidExecution) value.SelectAcceleration((HumanVisionAccelerationMode)accelerationChoice.value);
                else { value.UseWindowsCpu = accelerationChoice.value == 1; value.SelectAcceleration(value.UseWindowsCpu ? HumanVisionAccelerationMode.Cpu : HumanVisionAccelerationMode.Graphics); }
            }
            return value;
        }
        /// <summary>显示独立草稿；主动提交/保存才让配置生效。</summary>
        public void ShowDraft(HumanVisionSettingsData value)
        {
            draft = value.Clone(); var mode = draft.Mode;
            peopleChoice.SetValueWithoutNotify(draft.Recognition.MaxBodies - 1);
            if (accelerationChoice != null) accelerationChoice.SetValueWithoutNotify(AndroidExecution ? (int)draft.AccelerationMode : draft.UseWindowsCpu || draft.AccelerationMode == HumanVisionAccelerationMode.Cpu ? 1 : 0);
            cameraPanel.SetActive(draft.SourceKind == InputKind.WebCamera); videoPanel.SetActive(draft.SourceKind == InputKind.Video); rtspPanel.SetActive(draft.SourceKind == InputKind.Rtsp);
            sourceHint.text = "当前草稿输入：" + draft.SourceKind;
            Put("MaxBodies", draft.Recognition.MaxBodies); Put("CameraDevice", mode.CameraDevice);
            Put("VideoPath", mode.VideoPath); Put("RtspUrl", mode.RtspUrl); Put("RtspHost", mode.RtspComputerHost);
            ShowSelectedVideo(mode.VideoPath);
            ShowCaptureResolution(mode); Put("FPS", mode.RequestedFramesPerSecond);
            Put("LineWidth", mode.LineWidth); Put("PointSize", mode.PointDiameter);
            Put("LogInterval", draft.StatisticsInterval); Put("PoseLogInterval", draft.SkeletonLogInterval);
            Put("LogFileMB", draft.LogFileMegabytes); Put("LogSessions", draft.RetainedLogSessions);
            Mark("UseRegions", "按区域绑定角色", draft.Recognition.UseRegions); Mark("Mirror", "镜像", mode.Mirror);
            Mark("AutoStart", "Init 自动启动", draft.AutoStart); Mark("WindowsCpu", "Windows CPU", draft.UseWindowsCpu);
            Mark("DetailedLogs", "详细骨骼日志", draft.DetailedLogs);
            selectedRegion = Mathf.Clamp(selectedRegion, 0, draft.Recognition.Regions.Length - 1);
            ShowRegionFields(); ShowRegions();
        }
        /// <summary>为旧版普通 UGUI Prefab 补齐加速器控件；生成器也可调用并保存资产。</summary>
        public void EnsureAccelerationControl()
        {
            var names = AndroidExecution ? new[] { "NCNN Vulkan", "RK3588 NPU", "CPU" } : new[] { "GPU", "CPU" };
            if (accelerationChoice != null) {
                accelerationChoice.ClearOptions(); accelerationChoice.AddOptions(names.ToList());
                int siblingIndex = accelerationChoice.transform.GetSiblingIndex() - 1;
                var sibling = siblingIndex >= 0 ? accelerationChoice.transform.parent.GetChild(siblingIndex).GetComponent<Text>() : null;
                if (sibling != null && (sibling.text == "加速器" || sibling.text == "计算模式")) sibling.text = "计算模式";
                return;
            }
            var parent = qualityHint.transform.parent;
            int position = qualityHint.transform.GetSiblingIndex(), labelIndex = parent.childCount;
            accelerationChoice = Choice(parent, "AccelerationChoice", "计算模式", names);
            var label = parent.GetChild(labelIndex);
            // Add only these two objects to the existing UGUI. Project diagnostics and all saved references stay intact.
            label.SetSiblingIndex(position); accelerationChoice.transform.SetSiblingIndex(position + 1);
            var font = qualityHint.font;
            if (font != null) {
                label.GetComponent<Text>().font = font;
                foreach (var text in accelerationChoice.GetComponentsInChildren<Text>(true)) text.font = font;
            }
        }
        private void ShowCaptureResolution(DemoModeSettings mode)
        {
            var saved = new Vector2Int(mode.RequestedWidth, mode.RequestedHeight);
            var sizes = capturePresets.ToList(); int index = sizes.IndexOf(saved);
            // 旧配置/API 可能使用其它合法尺寸；显示已保存值，不静默改成 720p。
            // 用户仍通过下拉框选择常用尺寸，不提供宽高输入框。
            if (index < 0) { index = sizes.Count; sizes.Add(saved); }
            captureSizes = sizes.ToArray(); captureChoice.ClearOptions();
            captureChoice.AddOptions(sizes.Select((s, i) => new Dropdown.OptionData(
                (i >= capturePresets.Length ? "已保存：" : "") + s.x + " × " + s.y)).ToList());
            captureChoice.SetValueWithoutNotify(index);
        }
        public void SetQualities(ModelInputQualityChoice[] choices)
            => SetQualities(choices, null);
        /// <summary>同时显示真实平台限制/资源错误；可选等级显示实际模型输入尺寸。</summary>
        public void SetQualities(ModelInputQualityChoice[] choices, string explanation)
        {
            qualities = choices ?? Array.Empty<ModelInputQualityChoice>(); qualityChoice.ClearOptions();
            qualityChoice.AddOptions(qualities.Length == 0 ? new List<string> { "固定模型" } : qualities.Select(v =>
                (v.Quality == ModelInputQuality.Low ? "低" : v.Quality == ModelInputQuality.High ? "高" : "中") + "（" + v.Width + "×" + v.Height + "）").ToList());
            qualityChoice.interactable = qualities.Length > 1;
            int index = draft == null ? -1 : Array.FindIndex(qualities, v => v.Quality == draft.InputQuality);
            qualityChoice.SetValueWithoutNotify(Mathf.Max(0, index));
            qualityHint.text = HumanVisionSettingsController.Redact(explanation ?? (qualities.Length == 0 ? "此平台使用固定模型。" : "选择此平台支持的模型输入等级。"));
        }
        /// <summary>
        /// 注册项目构建时生成的随包视频路径/URL。Android 不能枚举 APK 内的目录，
        /// 因此由项目清单提供真实条目；刷新按钮会保留清单，调用本方法不会启动播放。
        /// </summary>
        public void SetBundledVideos(string[] paths)
        {
            bundledVideos = paths == null ? Array.Empty<string>() : paths.Where(p => !string.IsNullOrWhiteSpace(p)).Distinct().ToArray();
            RefreshSources();
        }
        private void ShowSelectedVideo(string path)
        {
            int index = Array.IndexOf(videos, path);
            // 明确的外部路径显示自定义项，避免单个随包视频无法重新选中来替换旧路径。
            videoChoice.SetValueWithoutNotify(index >= 0 ? index : string.IsNullOrWhiteSpace(path) ? 0 : videos.Length);
        }
        public void RefreshSources()
        {
            cameras = WebCamTexture.devices.Select(v => v.name).ToArray();
            cameraChoice.ClearOptions(); cameraChoice.AddOptions(cameras.Length == 0 ? new List<string> { "没有可用摄像头" } : cameras.ToList());
            cameraChoice.interactable = cameras.Length != 0;
            // 桌面可枚举；Android 使用项目注册的构建清单，不猜测 APK 内文件。
            var local = Directory.Exists(Application.streamingAssetsPath) ? Directory.GetFiles(Application.streamingAssetsPath, "*.mp4", SearchOption.AllDirectories) : Array.Empty<string>();
            videos = bundledVideos.Concat(local.OrderBy(p => p, StringComparer.Ordinal)).Distinct().ToArray();
            var titles = videos.Select(Path.GetFileName).ToList(); titles.Add("使用自定义视频路径");
            videoChoice.ClearOptions(); videoChoice.AddOptions(titles);
            videoChoice.interactable = videos.Length != 0;
            if (draft != null) { cameraChoice.SetValueWithoutNotify(Mathf.Max(0, Array.IndexOf(cameras, draft.Camera.CameraDevice))); ShowSelectedVideo(draft.Video.VideoPath); }
        }
        public void ToggleAdvanced() => advancedPanel.SetActive(!advancedPanel.activeSelf);
        public void ToggleRegionEdit() { editRegions = !editRegions; regionDetailsPanel.SetActive(editRegions); ShowRegions(); }
        public void SelectRegion(int delta) { selectedRegion = (selectedRegion + delta + draft.Recognition.MaxBodies) % draft.Recognition.MaxBodies; ShowRegionFields(); }
        public HumanVisionSettingsData UpdateRegion()
        {
            var value = ReadDraft(); value.Recognition.Regions[selectedRegion] = new Rect(Number("RegionX"), Number("RegionY"), Number("RegionW"), Number("RegionH"));
            // 即使当前关闭区域，编辑区也按开启区域的规则验证，防止把 NaN 草稿画到 UI。
            var check = value.Recognition.Clone(); check.UseRegions = true; check.Validate(); return value;
        }
        private void ShowRegionFields()
        {
            if (draft.Recognition.Regions.Length == 0) return;
            var region = draft.Recognition.Regions[selectedRegion]; Put("RegionX", region.x); Put("RegionY", region.y); Put("RegionW", region.width); Put("RegionH", region.height);
        }
        private void ShowRegions()
        {
            for (int i = 0; i < regionHandles.Length; i++) {
                bool shown = draft.Recognition.UseRegions && i < draft.Recognition.Regions.Length;
                regionHandles[i].gameObject.SetActive(shown); if (shown) regionHandles[i].SetRegion(draft.Recognition.Regions[i], editRegions);
            }
        }
        public void ToggleFullscreen()
        {
            fullPreview = !fullPreview; transform.Find("Settings panel").gameObject.SetActive(!fullPreview);
            var panel = (RectTransform)transform.Find("Preview panel"); panel.anchorMax = new Vector2(fullPreview ? 1 : .72f, .92f);
        }
        public void SetBusy(bool busy)
        {
            foreach (var button in buttons) button.interactable = !busy || button.name == "Stop";
            foreach (var field in fields) field.interactable = !busy;
            peopleChoice.interactable = !busy; qualityChoice.interactable = !busy && qualities.Length > 1;
            if (accelerationChoice != null) accelerationChoice.interactable = !busy;
            var reset = Array.Find(buttons, b => b.name == "ResetQuality"); if (reset != null) reset.interactable = !busy && qualities.Length > 1;
            cameraChoice.interactable = !busy && cameras.Length != 0; videoChoice.interactable = !busy && videos.Length != 0; captureChoice.interactable = !busy;
            foreach (var region in regionHandles) if (region.gameObject.activeSelf) region.SetRegion(region.Region, editRegions && !busy);
        }
        public void SetStatus(string value) => status.text = value;
        private InputField Field(string name) => Array.Find(fields, f => f.name == name) ?? throw new InvalidOperationException("设置场景缺少字段 " + name);
        private float Number(string name) => float.Parse(Field(name).text, CultureInfo.InvariantCulture);
        private int Integer(string name) => int.Parse(Field(name).text, CultureInfo.InvariantCulture);
        private void Put(string name, object value) => Field(name).SetTextWithoutNotify(Convert.ToString(value, CultureInfo.InvariantCulture));
        private void Mark(string name, string label, bool enabled) => Array.Find(buttons, b => b.name == name).GetComponentInChildren<Text>().text = label + (enabled ? " ✓" : " ○");
    }
}
