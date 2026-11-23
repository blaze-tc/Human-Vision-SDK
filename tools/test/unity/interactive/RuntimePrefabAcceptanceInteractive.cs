#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
using System;
using System.Collections;
using System.Globalization;
using System.IO;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        [Serializable]
        private sealed class InteractiveSettings
        {
            public int Version = 1;
            public HumanVisionSettingsData Data;
            public HumanVisionExecutionPolicy Execution = HumanVisionExecutionPolicy.CompatibleUnityInput;
            public bool Preview = true;
            public int TargetFps = 30;
        }

        private InteractiveSettings interactive;
        private bool basicOptionsOpen;
        private GUIStyle basicWindowStyle;
        private string widthText, heightText, inputFpsText, capacityText, confidenceText, ageText, handAgeText, regionsText;
        private string[] cameraNames = Array.Empty<string>();
        private readonly string[] backendReasons = new string[3];
        private double previewWindowAt, previewFps = -1;
        private int previewFrames;
        private ulong countedPreviewSource, countedPreviewGeneration;
        private long countedPreviewFrame = -1;

        public bool InteractiveMode => !automatedFixture;
        public bool BasicOptionsVisible => basicOptionsOpen;
        public double ActualPreviewFps => previewFps;
        public string BackendAvailability => $"CPU: {BackendReason(HumanVisionAccelerationMode.Cpu)} | GPU: {BackendReason(HumanVisionAccelerationMode.Graphics)} | NPU: {BackendReason(HumanVisionAccelerationMode.Neural)}";
        private static string InteractiveSettingsPath => Path.Combine(Application.persistentDataPath, "RuntimePrefabAcceptance", "interactive-settings.json");

        private void PrepareInteractiveState()
        {
            if (automatedFixture) return;
            RuntimeIndependentAcceptanceProbe.ConfigureOptions(startup);
            interactive = new InteractiveSettings {
                Data = HumanVisionSettingsData.FromOptions(startup), Execution = startup.ExecutionPolicy,
                Preview = startup.PreviewEnabled, TargetFps = Application.targetFrameRate == 15 || Application.targetFrameRate == 60 ? Application.targetFrameRate : 30
            };
            interactive.Data.Recognition = recognition.Clone();
            interactive.Data.Mode.RtspUrl = rtspUrl;
#if UNITY_EDITOR
            if (!manualFixture && !string.IsNullOrWhiteSpace(editorVideoPath)) {
                interactive.Data.SourceKind = InputKind.Video;
                interactive.Data.Video.VideoPath = editorVideoPath.Trim();
            }
#endif
            if (!manualFixture && File.Exists(InteractiveSettingsPath)) {
                try { interactive = ReadInteractiveSettings(InteractiveSettingsPath); }
                catch (Exception error) { status = "保存的基础设置不可用：" + error.Message; }
            }
            startup = BuildInteractiveOptions(interactive.Data, interactive.Execution, interactive.Preview);
            recognition = startup.Recognition.Clone(); maxBodies = recognition.MaxBodies;
            rtspUrl = InputAddress(startup);
            SetInteractiveFrameRate(interactive.TargetFps);
            try {
                var devices = WebCamTexture.devices;
                cameraNames = new string[devices.Length];
                for (int i = 0; i < devices.Length; i++) cameraNames[i] = devices[i].name;
            } catch (Exception error) { status = "无法枚举相机：" + error.Message; }
        }

        // One SDK owner validates the real provider before a mode becomes selectable.
        private IEnumerator ProbeManualBackend(HumanVisionAccelerationMode mode, HumanVisionSdkOptions requested)
        {
            int index = (int)mode;
            installedModes[index] = false;
            if (mode == HumanVisionAccelerationMode.Neural && !IsAndroid) {
                backendReasons[index] = "Windows 不支持 NPU";
                yield break;
            }
            if (DeferGraphicsProbeUntilInput(IsAndroid, mode)) {
                // Resource validation was completed by PrepareManualChoices. A
                // closed compatibility session cannot admit a native Vulkan source.
                installedModes[index] = true;
                backendReasons[index] = "模型已安装，待实际输入连接验证";
                yield break;
            }
            var probe = requested.Clone();
            probe.OpenInputOnInitialize = false;
            probe.ExecutionPolicy = HumanVisionExecutionPolicy.CompatibleUnityInput;
            probe.PreviewEnabled = false;
            probe.InputQuality = ModelInputQuality.Low;
            status = "正在检测 " + mode + " 后端（不打开输入）…";
            yield return view.Sdk.Initialize(probe);
            bool ready = view.Sdk.IsInitialized && view.Sdk.ActiveAccelerationMode == mode && string.IsNullOrEmpty(view.Sdk.LastError);
            backendReasons[index] = ready ? "可用" : diagnostics.Redact(string.IsNullOrEmpty(view.Sdk.LastError) ? "初始化未完成" : view.Sdk.LastError);
            yield return view.Sdk.Shutdown();
            bool retired = view.Sdk.State == HumanVisionSdkState.Stopped && !view.Sdk.IsInitialized && view.Sdk.InputState == InputSourceState.Stopped;
            if (!retired) {
                backendReasons[index] = "上一个后端尚未完成释放";
                yield break;
            }
            installedModes[index] = ready;
        }

        private string BackendReason(HumanVisionAccelerationMode mode) =>
            backendReasons[(int)mode] ?? (installedModes[(int)mode] ? "可用" : "尚未检测或模型未安装");

        internal static bool DeferGraphicsProbeUntilInput(bool android, HumanVisionAccelerationMode mode) =>
            android && mode == HumanVisionAccelerationMode.Graphics;

        private void ConfigureInteractiveOptions(HumanVisionSdkOptions options)
        {
            if (automatedFixture || interactive == null) return;
            options.ExecutionPolicy = options.SourceKind == InputKind.Rtsp ? interactive.Execution : HumanVisionExecutionPolicy.CompatibleUnityInput;
            options.PreviewEnabled = interactive.Preview;
            SetInteractiveFrameRate(interactive.TargetFps);
            ValidateInteractiveSource(options);
        }

        internal static HumanVisionSdkOptions BuildInteractiveOptions(HumanVisionSettingsData draft, HumanVisionExecutionPolicy execution, bool preview)
        {
            if (draft == null) throw new ArgumentNullException(nameof(draft));
            draft.Validate();
            var options = draft.ToOptions();
            options.UseWindowsCpu = options.AccelerationMode == HumanVisionAccelerationMode.Cpu;
            options.ExecutionPolicy = options.SourceKind == InputKind.Rtsp ? execution : HumanVisionExecutionPolicy.CompatibleUnityInput;
            options.PreviewEnabled = preview;
            options.UseModelSizedAnalysisInput = true;
            options.OpenInputOnInitialize = true;
            ValidateInteractiveSource(options);
            // Address validation belongs to Apply/Connect; an empty initial draft is editable.
            return options;
        }

        private static void ValidateInteractiveSource(HumanVisionSdkOptions options)
        {
            bool windows = Application.platform == RuntimePlatform.WindowsEditor || Application.platform == RuntimePlatform.WindowsPlayer;
            if (windows && options.AccelerationMode == HumanVisionAccelerationMode.Neural)
                throw new ArgumentException("Windows 不支持 NPU；请选择 CPU 或 GPU。");
            if (windows && options.SourceKind == InputKind.Rtsp && options.ExecutionPolicy != HumanVisionExecutionPolicy.CompatibleUnityInput && options.Input.Mirror)
                throw new ArgumentException("Windows 独立 RTSP 暂不支持镜像；请关闭镜像或选择兼容模式。");
        }

        private IEnumerator ConnectInteractive(HumanVisionSdkConfiguration requestedRecognition)
        {
            var options = startup.Clone();
            options.Recognition = (requestedRecognition ?? recognition).Clone();
            switch (options.SourceKind) {
                case InputKind.WebCamera: options.Input.CameraDevice = rtspUrl.Trim(); break;
                case InputKind.Video: options.Input.VideoPath = rtspUrl.Trim(); break;
                case InputKind.Rtsp: options.Input.RtspUrl = rtspUrl.Trim(); break;
            }
            options.OpenInputOnInitialize = true;
            thermalPhase = "interactive_" + options.SourceKind.ToString().ToLowerInvariant();
            streamSelection = default; streamLabel = null; displayedDecodedSize = default;
            yield return Initialize(options);
        }

        private void RememberInteractiveConfiguration()
        {
            if (automatedFixture || interactive == null) return;
            var data = interactive.Data;
            data.SourceKind = startup.SourceKind; data.Recognition = recognition.Clone();
            data.AccelerationMode = startup.AccelerationMode; data.UseWindowsCpu = startup.UseWindowsCpu;
            data.InputQuality = startup.InputQuality; data.RuntimeRoot = startup.RuntimeRoot;
            var mode = JsonUtility.FromJson<DemoModeSettings>(JsonUtility.ToJson(startup.Input));
            switch (startup.SourceKind) {
                case InputKind.WebCamera: data.Camera = mode; break;
                case InputKind.Video: data.Video = mode; break;
                case InputKind.Rtsp: data.Rtsp = mode; break;
            }
            if (startup.SourceKind == InputKind.Rtsp) interactive.Execution = startup.ExecutionPolicy;
            interactive.Preview = startup.PreviewEnabled;
            if (view != null) {
                var available = view.Sdk.GetInputQualityCapabilities(startup);
                if (string.IsNullOrEmpty(available.Error)) installedQualities[(int)startup.AccelerationMode] = available.Choices;
            }
            rtspUrl = InputAddress(startup);
        }

        private void TrackVisiblePreview()
        {
            double now = Time.realtimeSinceStartupAsDouble;
            if (!hasInput) {
                previewWindowAt = now; previewFrames = 0;
                previewFps = view != null && view.Sdk.IsRunning && !startup.PreviewEnabled ? 0 : -1;
                countedPreviewSource = countedPreviewGeneration = 0; countedPreviewFrame = -1;
                return;
            }
            if (input.SourceId != countedPreviewSource || input.Generation != countedPreviewGeneration) {
                previewWindowAt = now; previewFrames = 0; previewFps = -1; countedPreviewFrame = -1;
                countedPreviewSource = input.SourceId; countedPreviewGeneration = input.Generation;
            }
            if (input.FrameId != countedPreviewFrame) { countedPreviewFrame = input.FrameId; ++previewFrames; }
            if (now - previewWindowAt >= 1) {
                previewFps = previewFrames / (now - previewWindowAt); previewFrames = 0; previewWindowAt = now;
            }
        }

        private static void SetInteractiveFrameRate(int fps)
        {
            if (fps != 15 && fps != 30 && fps != 60) fps = 30;
            Application.targetFrameRate = fps;
        }

        partial void DrawSidebarChoices(float width, ref float y, ref bool handled)
        {
            handled = true;
            DrawSidebarText("分析设备", width, ref y, true);
            float third = (width - 16) / 3;
            DrawManualMode(new Rect(0, y, third, 36), HumanVisionAccelerationMode.Cpu, "CPU");
            DrawManualMode(new Rect(third + 8, y, third, 36), HumanVisionAccelerationMode.Graphics, "GPU");
            DrawManualMode(new Rect((third + 8) * 2, y, third, 36), HumanVisionAccelerationMode.Neural, "NPU");
            GUI.enabled = true; y += 46;
            DrawSidebarText(BackendReason(startup.AccelerationMode), width, ref y);
            DrawSidebarText("模型等级", width, ref y, true);
            DrawManualQuality(new Rect(0, y, third, 36), startup.AccelerationMode, ModelInputQuality.Low, "低");
            DrawManualQuality(new Rect(third + 8, y, third, 36), startup.AccelerationMode, ModelInputQuality.Medium, "中");
            DrawManualQuality(new Rect((third + 8) * 2, y, third, 36), startup.AccelerationMode, ModelInputQuality.High, "高");
            GUI.enabled = true; y += 46;
            if (HasManualQuality(startup.AccelerationMode, ModelInputQuality.UltraLow)) {
                DrawManualQuality(new Rect(0, y, width, 36), startup.AccelerationMode, ModelInputQuality.UltraLow, "超低");
                GUI.enabled = true; y += 44;
            }
            GUI.enabled = !operating && interactive != null && !automatedFixture;
            if (GUI.Button(new Rect(0, y, width, 36), "基础配置 / 输入源 / 保存读取", button)) OpenBasicOptions();
            GUI.enabled = true; y += 48;
            DrawSidebarText($"输入源：{startup.SourceKind}", width, ref y, true);
            GUI.enabled = !operating;
            var address = new Rect(0, y, width - 78, 36);
            rtspUrl = startup.SourceKind == InputKind.Rtsp && !showAddress
                ? GUI.PasswordField(address, rtspUrl, '*', field) : GUI.TextField(address, rtspUrl, field);
            if (GUI.Button(new Rect(width - 70, y, 70, 36), showAddress ? "隐藏" : "显示", button)) showAddress = !showAddress;
            y += 44;
            GUI.enabled = !operating && (automatedFixture || HasManualQuality(startup.AccelerationMode, startup.InputQuality));
            if (GUI.Button(new Rect(0, y, width, 36), "连接 / 应用输入", selected)) Run(Connect());
            GUI.enabled = true; y += 44;
            if (automatedFixture) {
                GUI.enabled = !operating;
                if (GUI.Button(new Rect(0, y, width, 36), useRequestedStream ? "固定所填码流" : "自动选流", button)) useRequestedStream = !useRequestedStream;
                GUI.enabled = true; y += 44;
            }
            if (campaignRunning) {
                GUI.enabled = !campaignCancelled;
                if (GUI.Button(new Rect(0, y, width, 36), "取消一键测试", button)) { campaignCancelled = true; status = "正在结束当前测试步骤…"; }
            } else {
                bool canMeasure = startup.SourceKind == InputKind.Rtsp && (automatedFixture ||
                    HasManualQuality(startup.AccelerationMode, ModelInputQuality.Low) && startup.InputQuality == ModelInputQuality.Low);
                GUI.enabled = !operating && canMeasure;
                if (GUI.Button(new Rect(0, y, width, 36), canMeasure ? "一键测试" : "低档可一键测试", button)) Run(RunThermalCampaign());
            }
            GUI.enabled = true; y += 44;
            if (GUI.Button(new Rect(0, y, width, 36), "导出 JSONL / 复制路径", button)) {
                diagnostics.Export(); if (!string.IsNullOrEmpty(diagnostics.ExportPath)) GUIUtility.systemCopyBuffer = diagnostics.ExportPath; status = diagnostics.Status;
            }
            y += 50;
        }
        partial void DrawSidebarModal() { DrawBasicOptions(); }
        partial void SidebarPhotoReason(ref string reason) { reason = PhotoUnavailableReason(); }
        partial void ObserveSidebarPreview() { var displayed = default(HumanVisionPresentedFrame); RecordDrawnPreview(live, in displayed); }

        private void DrawInteractiveHeader()
        {
            if (automatedFixture) return;
            if (GUI.Button(new Rect(990, 24, 178, 34), "基础配置 / 输入源", button)) OpenBasicOptions();
        }

        private void DrawInteractiveAddress()
        {
            string label = startup.SourceKind == InputKind.Video ? "视频路径" : startup.SourceKind == InputKind.WebCamera ? "相机设备" : "RTSP 地址";
            GUI.Label(new Rect(28, 104, 118, 34), label, body);
            GUI.enabled = !operating;
            var rect = new Rect(146, 104, 974, 34);
            if (startup.SourceKind == InputKind.Rtsp && !showAddress) rtspUrl = GUI.PasswordField(rect, rtspUrl, '*', field);
            else rtspUrl = GUI.TextField(rect, rtspUrl, field);
            if (GUI.Button(new Rect(1132, 104, 74, 34), showAddress ? "隐藏" : "显示", button)) showAddress = !showAddress;
            GUI.enabled = !operating && HasManualQuality(startup.AccelerationMode, startup.InputQuality);
            if (GUI.Button(new Rect(1224, 104, 188, 34), "连接 / 应用输入", selected)) Run(Connect());
            GUI.enabled = true;
        }

        private void OpenBasicOptions()
        {
            if (interactive == null || operating) { status = "请等待后端检测或当前操作完成"; return; }
            string editedAddress = rtspUrl;
            RememberInteractiveConfiguration();
            rtspUrl = editedAddress;
            switch (startup.SourceKind) {
                case InputKind.Rtsp: interactive.Data.Rtsp.RtspUrl = rtspUrl; break;
                case InputKind.Video: interactive.Data.Video.VideoPath = rtspUrl; break;
                case InputKind.WebCamera: interactive.Data.Camera.CameraDevice = rtspUrl; break;
            }
            SetDraftText(); basicOptionsOpen = true; capacityMenuOpen = false;
        }

        private void SetDraftText()
        {
            var mode = interactive.Data.Mode; var config = interactive.Data.Recognition;
            widthText = mode.RequestedWidth.ToString(); heightText = mode.RequestedHeight.ToString(); inputFpsText = mode.RequestedFramesPerSecond.ToString();
            capacityText = config.MaxBodies.ToString(); confidenceText = config.MinimumJointConfidence.ToString(CultureInfo.InvariantCulture);
            ageText = config.MaximumResultAgeMilliseconds.ToString(CultureInfo.InvariantCulture); handAgeText = config.MaximumHandAgeMilliseconds.ToString(CultureInfo.InvariantCulture);
            var lines = new string[config.Regions.Length];
            for (int i = 0; i < lines.Length; i++) { var r = config.Regions[i]; lines[i] = FormattableString.Invariant($"{r.x:0.######}, {r.y:0.######}, {r.width:0.######}, {r.height:0.######}"); }
            regionsText = string.Join("\n", lines);
        }

        private void ReadDraftText()
        {
            var mode = interactive.Data.Mode; var config = interactive.Data.Recognition;
            mode.RequestedWidth = ReadInt(widthText, "采集宽度"); mode.RequestedHeight = ReadInt(heightText, "采集高度");
            mode.RequestedFramesPerSecond = ReadInt(inputFpsText, "请求帧率"); config.MaxBodies = ReadInt(capacityText, "人数");
            config.MinimumJointConfidence = ReadFloat(confidenceText, "关节置信度");
            config.MaximumResultAgeMilliseconds = ReadFloat(ageText, "身体结果时效"); config.MaximumHandAgeMilliseconds = ReadFloat(handAgeText, "手关节时效");
            config.Regions = ParseInteractiveRegions(regionsText);
        }

        private static int ReadInt(string value, string label) => int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var number)
            ? number : throw new ArgumentException(label + "必须是整数");
        private static float ReadFloat(string value, string label) => float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
            ? number : throw new ArgumentException(label + "必须是数字");

        internal static Rect[] ParseInteractiveRegions(string text)
        {
            string[] lines = (text ?? "").Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
            var result = new Rect[lines.Length];
            for (int i = 0; i < lines.Length; i++) {
                string[] values = lines[i].Split(',');
                if (values.Length != 4) throw new ArgumentException("区域每行需为 x,y,width,height");
                result[i] = new Rect(ReadFloat(values[0], "区域x"), ReadFloat(values[1], "区域y"), ReadFloat(values[2], "区域宽"), ReadFloat(values[3], "区域高"));
            }
            return result;
        }

        private void SelectDraftSource(InputKind kind)
        {
            try { ReadDraftText(); } catch (Exception error) { status = error.Message; return; }
            interactive.Data.SourceKind = kind; SetDraftText();
        }

        private void ApplyBasicOptions()
        {
            try {
                ReadDraftText();
                var options = BuildInteractiveOptions(interactive.Data, interactive.Execution, interactive.Preview);
                options.Validate();
                if (!HasManualQuality(options.AccelerationMode, options.InputQuality)) throw new ArgumentException(BackendReason(options.AccelerationMode));
                basicOptionsOpen = false;
                SetInteractiveFrameRate(interactive.TargetFps);
                Run(Initialize(options));
            } catch (Exception error) { status = error.Message; }
        }

        private bool SaveBasicOptions()
        {
            try {
                ReadDraftText(); BuildInteractiveOptions(interactive.Data, interactive.Execution, interactive.Preview).Validate();
                string path = InteractiveSettingsPath; Directory.CreateDirectory(Path.GetDirectoryName(path));
                string temporary = path + ".tmp"; File.WriteAllText(temporary, JsonUtility.ToJson(interactive, true));
                if (!File.Exists(path)) File.Move(temporary, path);
                else {
                    try { File.Replace(temporary, path, path + ".bak"); }
                    catch (PlatformNotSupportedException) {
                        File.Copy(path, path + ".bak", true);
                        File.Copy(temporary, path, true); File.Delete(temporary);
                    }
                }
                status = "基础设置已保存：" + path;
                return true;
            } catch (Exception error) { status = "保存失败：" + error.Message; return false; }
        }

        private bool LoadBasicOptions()
        {
            try {
                interactive = ReadInteractiveSettings(InteractiveSettingsPath);
                SetDraftText(); status = "已读取草稿，点击应用后生效"; return true;
            } catch (Exception error) { status = error.Message; return false; }
        }

        private static InteractiveSettings ReadInteractiveSettings(string path)
        {
            var saved = JsonUtility.FromJson<InteractiveSettings>(File.ReadAllText(path));
            if (saved == null || saved.Version != 1 || saved.Data == null || !Enum.IsDefined(typeof(HumanVisionExecutionPolicy), saved.Execution))
                throw new ArgumentException("基础配置版本或执行策略无效");
            saved.Data.Validate();
            if (saved.TargetFps != 15 && saved.TargetFps != 30 && saved.TargetFps != 60) throw new ArgumentException("Unity限帧只支持15/30/60");
            BuildInteractiveOptions(saved.Data, saved.Execution, saved.Preview).Validate();
            return saved;
        }

        private void DrawBasicOptions()
        {
            if (!basicOptionsOpen || interactive == null) return;
            if (basicWindowStyle == null) basicWindowStyle = new GUIStyle(GUI.skin.window) { font = uiFont, fontSize = 18 };
            GUI.ModalWindow(41773, new Rect(115, 104, 1210, 650), DrawBasicOptionsWindow, "基础配置 · 点击应用后生效", basicWindowStyle);
        }

        private void DrawBasicOptionsWindow(int id)
        {
            var data = interactive.Data; var mode = data.Mode;
            GUI.Label(new Rect(24, 28, 300, 30), "输入源", title);
            DrawSourceChoice(24, InputKind.Rtsp, "RTSP"); DrawSourceChoice(175, InputKind.Video, "本地视频"); DrawSourceChoice(326, InputKind.WebCamera, "相机");
            string addressLabel = data.SourceKind == InputKind.Rtsp ? "完整 RTSP 地址" : data.SourceKind == InputKind.Video ? "视频路径 / URL" : "相机设备名（空=默认）";
            GUI.Label(new Rect(24, 104, 520, 26), addressLabel, small);
            if (data.SourceKind == InputKind.Rtsp) mode.RtspUrl = GUI.TextField(new Rect(24, 134, 540, 30), mode.RtspUrl, field);
            if (data.SourceKind == InputKind.Video) mode.VideoPath = GUI.TextField(new Rect(24, 134, 540, 30), mode.VideoPath, field);
            if (data.SourceKind == InputKind.WebCamera) {
                mode.CameraDevice = GUI.TextField(new Rect(24, 134, 405, 30), mode.CameraDevice, field);
                GUI.enabled = cameraNames.Length > 0;
                if (GUI.Button(new Rect(437, 134, 127, 30), "下一相机", button)) {
                    int next = Array.IndexOf(cameraNames, mode.CameraDevice) + 1; mode.CameraDevice = cameraNames[next % cameraNames.Length];
                }
                GUI.enabled = true;
            }
            GUI.Label(new Rect(24, 177, 120, 28), "请求宽 / 高 / FPS", small);
            widthText = GUI.TextField(new Rect(168, 177, 110, 28), widthText, field);
            heightText = GUI.TextField(new Rect(290, 177, 110, 28), heightText, field);
            inputFpsText = GUI.TextField(new Rect(412, 177, 110, 28), inputFpsText, field);
            GUI.Label(new Rect(24, 217, 540, 26), data.SourceKind == InputKind.Rtsp ? "原图模式使用当前 RTSP 地址，不自动切换码流。" : "请求尺寸不等于模型尺寸；视频使用源尺寸。", small);
            bool independentWindows = !IsAndroid && data.SourceKind == InputKind.Rtsp && interactive.Execution != HumanVisionExecutionPolicy.CompatibleUnityInput;
            GUI.enabled = !independentWindows;
            mode.Mirror = GUI.Toggle(new Rect(24, 254, 150, 28), mode.Mirror, "水平镜像");
            GUI.enabled = true;
            data.RtspTcp = GUI.Toggle(new Rect(180, 254, 130, 28), data.RtspTcp, "RTSP TCP");
            GUI.enabled = data.SourceKind != InputKind.Rtsp;
            data.AutoSelectRtspStream = GUI.Toggle(new Rect(320, 254, 238, 28), data.AutoSelectRtspStream, "自动选择同通道码流");
            GUI.enabled = true;
            if (independentWindows) GUI.Label(new Rect(24, 285, 540, 25), "Windows独立输入暂不支持镜像；兼容模式可验证镜像。", small);
            GUI.Label(new Rect(24, 320, 100, 26), "执行方式", small);
            DrawExecutionChoice(new Rect(134, 316, 128, 32), HumanVisionExecutionPolicy.CompatibleUnityInput, "兼容输入");
            DrawExecutionChoice(new Rect(272, 316, 138, 32), HumanVisionExecutionPolicy.IndependentPreferred, "优先独立");
            DrawExecutionChoice(new Rect(420, 316, 144, 32), HumanVisionExecutionPolicy.IndependentRequired, "强制独立");
            if (data.SourceKind != InputKind.Rtsp) GUI.Label(new Rect(24, 354, 540, 27), "视频 / 相机当前使用Unity兼容驱动，识别仍为异步。", small);
            interactive.Preview = GUI.Toggle(new Rect(24, 392, 160, 30), interactive.Preview, "显示预览");
            showSkeleton = GUI.Toggle(new Rect(200, 392, 160, 30), showSkeleton, "显示骨骼");
            GUI.Label(new Rect(24, 438, 120, 30), "Unity限帧", small);
            for (int i = 0; i < 3; i++) { int fps = i == 0 ? 15 : i == 1 ? 30 : 60;
                if (GUI.Button(new Rect(160 + i * 130, 434, 120, 32), fps + " FPS", interactive.TargetFps == fps ? selected : button)) {
                    interactive.TargetFps = fps; SetInteractiveFrameRate(fps);
                }
            }
            GUI.Label(new Rect(610, 28, 570, 30), "识别与区域", title);
            GUI.Label(new Rect(610, 70, 130, 28), "人数上限1–8", small);
            capacityText = GUI.TextField(new Rect(790, 70, 90, 28), capacityText, field);
            data.Recognition.UseRegions = GUI.Toggle(new Rect(910, 70, 240, 28), data.Recognition.UseRegions, "启用区域分配");
            if (GUI.Button(new Rect(610, 110, 190, 32), "按人数横向均分", button)) {
                try { data.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(ReadInt(capacityText, "人数")); regionsText = FormatRegions(data.Recognition.Regions); }
                catch (Exception error) { status = error.Message; }
            }
            GUI.Label(new Rect(610, 153, 570, 27), "每行 x,y,width,height；左上原点、0–1、不可重叠", small);
            regionsText = GUI.TextArea(new Rect(610, 183, 570, 192), regionsText, field);
            GUI.Label(new Rect(610, 392, 170, 28), "关节置信度0–1", small);
            confidenceText = GUI.TextField(new Rect(815, 392, 110, 28), confidenceText, field);
            GUI.Label(new Rect(610, 434, 180, 28), "身体 / 手时效(ms)", small);
            ageText = GUI.TextField(new Rect(815, 434, 110, 28), ageText, field);
            handAgeText = GUI.TextField(new Rect(940, 434, 110, 28), handAgeText, field);
            GUI.Label(new Rect(24, 485, 1160, 28), BackendAvailability, small);
            GUI.Label(new Rect(24, 516, 1160, 28), "精确拍照：" + PhotoUnavailableReason(), small);
            GUI.Label(new Rect(24, 547, 1160, 30), status ?? "", small);
            if (GUI.Button(new Rect(24, 594, 165, 36), "读取保存设置", button)) LoadBasicOptions();
            if (GUI.Button(new Rect(200, 594, 165, 36), "保存草稿", button)) SaveBasicOptions();
            if (GUI.Button(new Rect(790, 594, 185, 36), "取消 / 关闭", button)) basicOptionsOpen = false;
            GUI.enabled = !operating;
            if (GUI.Button(new Rect(990, 594, 190, 36), "应用并连接", selected)) ApplyBasicOptions();
            GUI.enabled = true;
        }

        private void DrawSourceChoice(float x, InputKind kind, string label)
        {
            if (GUI.Button(new Rect(x, 64, 140, 32), label, interactive.Data.SourceKind == kind ? selected : button)) SelectDraftSource(kind);
        }
        private void DrawExecutionChoice(Rect rect, HumanVisionExecutionPolicy policy, string label)
        {
            GUI.enabled = interactive.Data.SourceKind == InputKind.Rtsp;
            var effective = interactive.Data.SourceKind == InputKind.Rtsp ? interactive.Execution : HumanVisionExecutionPolicy.CompatibleUnityInput;
            if (GUI.Button(rect, label, effective == policy ? selected : button)) {
                if (!IsAndroid && policy != HumanVisionExecutionPolicy.CompatibleUnityInput && interactive.Data.Mode.Mirror)
                    status = "请先关闭镜像，再选择Windows独立输入";
                else interactive.Execution = policy;
            }
            GUI.enabled = true;
        }
        private static string FormatRegions(Rect[] regions)
        {
            var lines = new string[regions.Length];
            for (int i = 0; i < lines.Length; i++) { var r = regions[i]; lines[i] = FormattableString.Invariant($"{r.x:0.######}, {r.y:0.######}, {r.width:0.######}, {r.height:0.######}"); }
            return string.Join("\n", lines);
        }

        private string PhotoUnavailableReason()
        {
            if (photoCaptureActive) return "正在保存照片";
            if (!hasInput || view == null) return "等待有效预览";
            if (!IsAndroid && view.Sdk.TryGetRuntimeProgress(out var progress) && progress.ExecutionMode == "native")
                return "Windows独立输入暂无原分辨率精确照片";
            if (!view.SupportsMatchedFrames || !SystemInfo.supportsAsyncGPUReadback) return "当前图形设备不支持精确同帧回读";
            return "可用";
        }
    }
}
#endif
