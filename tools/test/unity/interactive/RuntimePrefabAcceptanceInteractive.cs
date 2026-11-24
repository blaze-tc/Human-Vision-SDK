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
        private string widthText, heightText, inputFpsText, capacityText, confidenceText, ageText, handAgeText, regionsText;
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
            SetDraftText(); basicOptionsOpen = true;
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
                string path = InteractiveSettingsPath;
                WriteInteractiveSettings(path, interactive);
                status = "基础设置已保存：" + path;
                return true;
            } catch (Exception error) { status = "保存失败：" + error.Message; return false; }
        }

        private void SaveConnectedInput()
        {
            if (interactive == null || automatedFixture)
                return;

            try
            {
                WriteInteractiveSettings(InteractiveSettingsPath, interactive);
                status = "输入已连接，已保存本次输入。";
            }
            catch (IOException error)
            {
                status = "输入已连接，保存输入失败：" + diagnostics.Redact(error.Message);
            }
            catch (UnauthorizedAccessException error)
            {
                status = "输入已连接，保存输入失败：" + diagnostics.Redact(error.Message);
            }
        }

        private static void WriteInteractiveSettings(string path, InteractiveSettings settings)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + ".tmp";
            try
            {
                File.WriteAllText(temporary, JsonUtility.ToJson(settings, true));
                if (File.Exists(path))
                {
                    try { File.Replace(temporary, path, path + ".bak"); }
                    catch (PlatformNotSupportedException)
                    {
                        File.Copy(path, path + ".bak", true);
                        File.Copy(temporary, path, true);
                    }
                }
                else
                    File.Move(temporary, path);
            }
            finally
            {
                if (File.Exists(temporary))
                    File.Delete(temporary);
            }
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
