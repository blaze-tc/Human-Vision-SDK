using System;
using System.Collections;
using System.IO;
using System.Text.RegularExpressions;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.SceneManagement;

namespace HumanVision.Demo
{
    /// <summary>设置 Demo 业务绑定。所有人数/输入/区域通过总控 API；普通场景可替换 View 而复用此控制器。</summary>
    [DisallowMultipleComponent]
    public sealed class HumanVisionSettingsController : MonoBehaviour
    {
        [SerializeField] private HumanVisionSdk sdk;
        [SerializeField] private HumanVisionSettingsView view;
        [SerializeField, Tooltip("可选中文字体；构建时建议提供有授权的中文 Font 资产。")] private Font uiFont;
        [SerializeField, Tooltip("可选返回场景；留空时只触发 ReturnRequested。")] private string returnScene = "";
        [SerializeField] private UnityEvent returnRequested = new UnityEvent();
        [SerializeField] private string runtimeRoot = "";
        [SerializeField, Tooltip("无保存配置时是否自动启动；输入等初始值读取 SDK Inspector。")] private bool autoStartWhenNoSavedSettings;
        private HumanVisionSettingsData draft = new HumanVisionSettingsData(), active, saved;
        private HumanVisionSettingsLogger logger;
        private bool applying, applySucceeded;
        private string message = "草稿尚未应用";
        private double nextStatus;
        private double applyStarted;
        private bool configurationLoaded;
        public HumanVisionSettingsData Draft => draft.Clone();
        public HumanVisionSettingsData Active => active?.Clone();
        public HumanVisionSettingsData Saved => saved?.Clone();
        public bool Busy => applying || (sdk != null && sdk.Busy);
        public UnityEvent ReturnRequested => returnRequested;
        public HumanVisionSdk Sdk => sdk;
        public HumanVisionSettingsView View => view;
        /// <summary>生成器设置引用；场景中也可以在 Inspector 手动绑定。</summary>
        public void Configure(HumanVisionSdk controller, HumanVisionSettingsView settingsView, Font font = null)
        { sdk = controller; view = settingsView; uiFont = font; sdk.InitializeOnStart = false; }
        private void Awake() { if (sdk != null) sdk.InitializeOnStart = false; }
        private IEnumerator Start()
        {
            if (sdk == null || view == null) { enabled = false; Debug.LogError("设置 Demo 需要绑定 SDK 和 View。", this); yield break; }
            if (uiFont == null && Application.platform != RuntimePlatform.Android) {
                string[] available = Font.GetOSInstalledFontNames();
                foreach (string name in new[] { "Microsoft YaHei", "Microsoft YaHei UI", "SimHei", "Noto Sans CJK SC" })
                    if (Array.IndexOf(available, name) >= 0) { uiFont = Font.CreateDynamicFontFromOSFont(name, 22); break; }
            }
            if (uiFont != null) foreach (var text in view.GetComponentsInChildren<UnityEngine.UI.Text>(true)) text.font = uiFont;
            // 首次 Apply 之前就创建日志，资源缺失、配置读取及输入打开失败也有证据。
            logger = new HumanVisionSettingsLogger(); Guard(() => logger.BeginStartup());
            logger.Record("controller.start", "settings=" + HumanVisionSdkSettingsStore.DefaultPath + " runtimeOverride=" + runtimeRoot);
            view.Bind(this); Reload(HumanVisionSdkSettingsStore.DefaultPath);
            Guard(() => logger.ApplySettings(draft, configurationLoaded));
            view.ShowDraft(draft); RefreshQualities();
            sdk.Initialized += ConnectRenderer; sdk.ErrorOccurred += OnError; sdk.Stopped += DisconnectRenderer;
            sdk.UserEntered += OnUserEntered; sdk.UserExited += OnUserExited; sdk.RegionOccupancyChanged += OnRegionChanged;
            applying = true; view.SetBusy(true);
            yield return sdk.PrepareInputQualities(runtimeRoot);
            applying = false; RefreshQualities(); view.SetBusy(false);
            logger.Record("quality.available", sdk.GetInputQualityCapabilities(draft.ToOptions(runtimeRoot)).Message);
            if (draft.AutoStart) yield return Apply(false);
        }
        private void OnDestroy()
        {
            if (sdk != null) { sdk.Initialized -= ConnectRenderer; sdk.ErrorOccurred -= OnError; sdk.Stopped -= DisconnectRenderer; }
            if (sdk != null) { sdk.UserEntered -= OnUserEntered; sdk.UserExited -= OnUserExited; sdk.RegionOccupancyChanged -= OnRegionChanged; }
            logger?.Record("controller.destroy", "state=" + (sdk != null ? sdk.State.ToString() : "destroyed"));
            logger?.Dispose();
        }
        private void OnDisable()
        {
            if (!applying) return;
            StopAllCoroutines(); applying = false;
            if (sdk != null && sdk.isActiveAndEnabled) sdk.StartCoroutine(sdk.StopSdk());
        }
        /// <summary>解析/验证成功才替换草稿。外部也可借此实现自己的设置控件。</summary>
        public void Edit(Func<HumanVisionSettingsData> read)
        { Guard(() => { var next = read(); next.Validate(); draft = next.Clone(); view.ShowDraft(draft); }); }
        /// <summary>普通 UGUI 按钮的命令入口；应用中只能停止。</summary>
        public void Execute(string command)
        {
            logger?.Record("ui.command", command + " busy=" + Busy);
            if (Busy && command != "Stop") { logger?.Record("ui.rejected", "Waiting for active operation: " + command); return; }
            Guard(() => {
                switch (command) {
                    case "Apply": StartCoroutine(Apply(false)); return;
                    case "ApplySave": StartCoroutine(Apply(true)); return;
                    case "Stop": StopAllCoroutines(); applying = false; StartCoroutine(sdk.StopSdk()); message = "已请求安全停止"; return;
                    case "Return": StartCoroutine(Return()); return;
                    case "Save": var value = view.ReadDraft(); value.Validate(); HumanVisionSdkSettingsStore.Save(HumanVisionSdkSettingsStore.DefaultPath, value); saved = value.Clone(); draft = value; logger?.Record("configuration.saved", JsonUtility.ToJson(value)); message = "仅保存草稿，未改变运行设置"; break;
                    case "Reload": Reload(HumanVisionSdkSettingsStore.DefaultPath); break;
                    case "RestoreBackup": Reload(HumanVisionSdkSettingsStore.DefaultPath + ".bak"); message = "备份已读取为草稿；尚未覆盖配置或应用"; break;
                    case "Advanced": view.ToggleAdvanced(); return;
                    case "EditRegions": view.ToggleRegionEdit(); return;
                    case "FullscreenPreview": view.ToggleFullscreen(); return;
                    case "PreviousRegion": view.SelectRegion(-1); return;
                    case "NextRegion": view.SelectRegion(1); return;
                    case "UpdateRegion": draft = view.UpdateRegion(); break;
                    case "SelectCamera": case "RefreshVideos": view.RefreshSources(); return;
                    case "CopyLogPath": GUIUtility.systemCopyBuffer = logger.Directory; message = "已复制日志路径"; return;
                    case "OpenLogs": Application.OpenURL(new Uri(logger.Directory).AbsoluteUri); message = "日志路径：" + logger.Directory; return;
                    case "ExportLogs": message = "日志已导出：" + logger.Export(); return;
                    default:
                        var next = view.ReadDraft();
                        switch (command) {
                            case "Camera": next.SourceKind = InputKind.WebCamera; break;
                            case "Video": next.SourceKind = InputKind.Video; break;
                            case "RTSP": next.SourceKind = InputKind.Rtsp; break;
                            case "Mirror": next.Mode.Mirror = !next.Mode.Mirror; break;
                            case "UseRegions": next.Recognition.UseRegions = !next.Recognition.UseRegions; break;
                            case "ResetRegions": next.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(next.Recognition.MaxBodies); break;
                            case "AutoStart": next.AutoStart = !next.AutoStart; break;
                            case "WindowsCpu": next.UseWindowsCpu = !next.UseWindowsCpu; break;
                            case "DetailedLogs": next.DetailedLogs = !next.DetailedLogs; break;
                            case "ResetQuality": next.InputQuality = ModelInputQuality.Medium; break;
                            case "BuildRtsp": case "BuildRtspVideo": next.Mode.RtspUrl = HumanVisionRtspComputerHost.BuildUrl(next.Mode.RtspComputerHost, command == "BuildRtsp"); break;
                            default: return;
                        }
                        next.Validate(); draft = next; break;
                }
                view.ShowDraft(draft); RefreshQualities();
            });
        }
        /// <summary>预检成功才启动。保存只在本次输入实际 Running 后执行；失败保留之前 Active/Saved。</summary>
        public IEnumerator Apply(bool save)
        {
            if (Busy) yield break;
            applySucceeded = false;
            HumanVisionSettingsData candidate = null; HumanVisionSdkOptions options = null;
            Guard(() => { candidate = view.ReadDraft(); candidate.Validate(); var requested = candidate.ToOptions(runtimeRoot); requested.Validate(); options = requested; });
            if (options == null) yield break;
            Guard(() => logger?.Configure(candidate));
            applyStarted = Time.realtimeSinceStartupAsDouble;
            logger?.Record("apply.begin", "save=" + save + " runtimeOverride=" + runtimeRoot + " requested=" + JsonUtility.ToJson(candidate));
            applying = true; message = "应用中，等待实际输入启动"; view.SetBusy(true);
            yield return sdk.Initialize(options);
            if (sdk.IsRunning && string.IsNullOrEmpty(sdk.LastError)) {
                applySucceeded = true;
                active = candidate.Clone(); draft = candidate.Clone();
                view.Overlay.ConfigureStyle(candidate.Mode.LineWidth, candidate.Mode.PointDiameter);
                Guard(() => { if (save) { HumanVisionSdkSettingsStore.Save(HumanVisionSdkSettingsStore.DefaultPath, candidate); saved = candidate.Clone(); logger?.Record("configuration.saved", "path=" + HumanVisionSdkSettingsStore.DefaultPath); }
                    message = save ? "实际输入已启动，配置已保存" : "实际输入已启动；未保存"; });
                logger?.Record("apply.succeeded", "elapsedMs=" + ((Time.realtimeSinceStartupAsDouble-applyStarted)*1000).ToString("F1") + " input=" + sdk.InputState + " source=" + candidate.SourceKind);
            } else { message = "应用失败：" + Redact(sdk.LastError); logger?.Record("apply.failed", "elapsedMs=" + ((Time.realtimeSinceStartupAsDouble-applyStarted)*1000).ToString("F1") + " state=" + sdk.State + " error=" + sdk.LastError); }
            applying = false; view.ShowDraft(draft); RefreshQualities(); view.SetBusy(false);
        }
        private IEnumerator Return()
        {
            yield return Apply(false);
            // 返回必须是当前草稿成功应用后的运行状态，预检失败不能沿用旧会话返回。
            if (!applySucceeded || !sdk.IsRunning || !string.IsNullOrEmpty(sdk.LastError)) yield break;
            if (!string.IsNullOrEmpty(returnScene)) {
                if (!Application.CanStreamedLevelBeLoaded(returnScene)) { message = "返回场景不在 Build Settings：" + returnScene; yield break; }
                yield return sdk.StopSdk(); SceneManager.LoadScene(returnScene);
            } else returnRequested.Invoke();
        }
        private void Reload(string path)
        {
            configurationLoaded = false;
            try {
                bool exists = File.Exists(path);
                var value = exists ? HumanVisionSdkSettingsStore.Load(path) : HumanVisionSettingsData.FromOptions(sdk.Configuration, autoStartWhenNoSavedSettings);
                draft = value.Clone();
                configurationLoaded = true;
                if (path == HumanVisionSdkSettingsStore.DefaultPath) saved = exists ? value.Clone() : null;
                message = "已读取配置为草稿，尚未应用";
                logger?.Record("configuration.loaded", "path=" + path + " exists=" + exists + " draft=" + JsonUtility.ToJson(draft));
            } catch (Exception e) {
                message = "读取失败，保留原文件和草稿：" + Redact(e.Message);
                logger?.Record("configuration.load.failed", "path=" + path + " error=" + e.Message, e.StackTrace);
            }
        }
        private void ConnectRenderer()
        {
            view.Overlay.Configure(sdk.RuntimeManager, sdk.FrameBridge); sdk.ScreenTarget = view.Preview.rectTransform;
            logger?.Record("runtime.initialized", "elapsedMs=" + ((Time.realtimeSinceStartupAsDouble-applyStarted)*1000).ToString("F1") + " root=" + sdk.RuntimeRootPath +
                " profile=" + sdk.RuntimeProfile + " modelPack=" + sdk.ActiveModelPack + " analysis=" + sdk.AnalysisInputSize + " diagnostics=" + sdk.RuntimeDiagnostics);
            RefreshQualities();
        }
        private void RefreshQualities()
        {
            var capability = sdk.GetInputQualityCapabilities(draft.ToOptions(runtimeRoot));
            view.SetQualities(capability.Choices, capability.Message);
            if (!string.IsNullOrEmpty(capability.Error)) logger?.Record("quality.error", capability.Error);
        }
        private void DisconnectRenderer() { logger?.Record("stop.complete", "Runtime and input safely released"); if (view != null) { view.Overlay.Configure(null, null); view.Preview.texture = null; } }
        private void OnError(string error) { message = "SDK：" + Redact(error); logger?.Record("sdk.error", "state=" + sdk.State + " input=" + sdk.InputState + " error=" + error); }
        private void OnUserEntered(int index, long id) => logger?.Record("user.entered", "slot=" + index + " id=" + id);
        private void OnUserExited(int index, long id) => logger?.Record("user.exited", "slot=" + index + " id=" + id);
        private void OnRegionChanged(int index, HumanVisionRegionOccupancy state) => logger?.Record("region.changed", "slot=" + index + " state=" + state);
        private void Update()
        {
            if (sdk == null || view == null) return;
            var texture = sdk.GetColorImageTex(); view.Preview.texture = texture; view.Preview.color = texture == null ? Color.black : Color.white;
            if (texture != null) view.Preview.GetComponent<UnityEngine.UI.AspectRatioFitter>().aspectRatio = (float)texture.width / texture.height;
            view.SetBusy(Busy);
            if (Time.realtimeSinceStartupAsDouble < nextStatus) return; nextStatus = Time.realtimeSinceStartupAsDouble + .2;
            var stats = sdk.Stats;
            string occupancy = "";
            for (int i = 0; i < sdk.GetMaxBodies(); i++) occupancy += " " + i + ":" + (sdk.TryGetRegionOccupancy(i, out bool occupied) ? occupied ? "有人" : "无人" : "未知");
            view.SetStatus(Redact(message) + "\nSDK " + sdk.State + " / Input " + sdk.InputState + "　实际人数 " + sdk.GetUsersCount() + "/" + sdk.GetMaxBodies() +
                "　输入 " + stats.InputFps.ToString("F1") + " / 推理 " + stats.InferenceFps.ToString("F1") + " FPS　结果 " + sdk.ResultSequence + " / 帧 " + sdk.SourceFrameId + "\n槽位观测：" + occupancy);
            try { logger?.Tick(sdk); } catch (Exception e) { message = "日志写入失败：" + Redact(e.Message); }
            if (logger != null && !string.IsNullOrEmpty(logger.LastWriteError)) message = "日志写入失败：" + logger.LastWriteError;
        }
        private void Guard(Action work) { try { work(); } catch (Exception e) { message = Redact(e.Message); logger?.Record("configuration.rejected", e.GetType().Name + ": " + e.Message, e.StackTrace); } }
        /// <summary>UI/日志错误文本去除 RTSP 凭据；原始输入地址仅存在于编辑字段和用户配置文件。</summary>
        public static string Redact(string text)
        {
            string value = Regex.Replace(text ?? "", @"(?i)(rtsps?://)[^/\s@]+@", "$1***@");
            return Regex.Replace(value, @"(?i)\b(password|passwd|pwd|(?:access[_-]|refresh[_-]|auth[_-])?token|api[_-]?key|secret)=([^&\s""'<>]+)", "$1=***");
        }
    }
}
