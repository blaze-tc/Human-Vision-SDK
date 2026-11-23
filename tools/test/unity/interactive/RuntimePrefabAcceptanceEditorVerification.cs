#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Reflection;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        [Serializable]
        private sealed class EditorCommandReport
        {
            public string id, action, result = "idle", sdkState, error, uiStatus;
            public bool busy, uiStatusChanged;
        }

        private sealed class EditorCommand
        {
            public string Id, Action, Address, RegionText;
            public HumanVisionAccelerationMode Mode;
            public ModelInputQuality Quality;
            public InputKind Kind;
            public int Fps, Capacity;
            public bool Enabled;
            public float Confidence, AgeMs, HandAgeMs;
        }

        private readonly EditorCommandReport editorCommandReport = new EditorCommandReport();
        private string editorCommandInitialStatus;

        // Editor automation uses the same GUI handlers. Accepted means dispatched,
        // not completed: inspect busy, sdkState and error after the coroutine ends.
        public string EditorVerificationCommand
        {
            get {
                editorCommandReport.busy = operating;
                editorCommandReport.sdkState = view == null ? "Unloaded" : view.Sdk.State.ToString();
                editorCommandReport.uiStatus = diagnostics.Redact(status ?? "");
                editorCommandReport.uiStatusChanged = !string.Equals(editorCommandInitialStatus, status, StringComparison.Ordinal);
                string runtimeError = view == null ? null : view.Sdk.LastError;
                if (editorCommandReport.result == "accepted") editorCommandReport.error = diagnostics.Redact(runtimeError ?? "");
                return JsonUtility.ToJson(editorCommandReport);
            }
            set {
                editorCommandReport.id = editorCommandReport.action = "";
                try {
                    EditorCommand command = ParseEditorVerificationCommand(value);
                    editorCommandReport.id = command.Id;
                    editorCommandReport.action = command.Action;
                    if (!Application.isPlaying) throw new ArgumentException("Verification commands require Play mode.");
                    if (automatedFixture) throw new ArgumentException("Verification commands require interactive mode.");
                    if (operating) throw new ArgumentException("The existing SDK operation is busy; wait before sending another command.");
                    if (interactive == null || startup == null) throw new ArgumentException("Interactive preparation is not complete.");
                    if (basicOptionsOpen) throw new ArgumentException("Close the basic settings draft before verification commands.");
                    editorCommandInitialStatus = status;
                    DispatchEditorVerificationCommand(command);
                    editorCommandReport.result = "accepted";
                    editorCommandReport.error = "";
                } catch (Exception error) {
                    editorCommandReport.result = "rejected";
                    editorCommandReport.error = diagnostics.Redact(error.Message);
                    throw new ArgumentException(editorCommandReport.error, error);
                }
            }
        }

        private void DispatchEditorVerificationCommand(EditorCommand command)
        {
            switch (command.Action) {
                case "compute":
                    if (!HasManualQuality(command.Mode, command.Quality)) throw new ArgumentException(BackendReason(command.Mode));
                    SelectCompute(command.Mode, command.Quality);
                    break;
                case "source":
                case "preview": case "regions": case "display": ApplyEditorBasicOptions(command); break;
                case "save":
                    OpenBasicOptions();
                    try { if (!SaveBasicOptions()) throw new ArgumentException(diagnostics.Redact(status)); }
                    finally { basicOptionsOpen = false; }
                    break;
                case "load":
                    OpenBasicOptions();
                    try {
                        if (!LoadBasicOptions()) throw new ArgumentException(diagnostics.Redact(status));
                        ApplyBasicOptions();
                        if (basicOptionsOpen) throw new ArgumentException(diagnostics.Redact(status));
                    } finally { basicOptionsOpen = false; }
                    break;
                case "capacity": SetCapacity(command.Capacity); break;
                case "skeleton":
                    showSkeleton = command.Enabled;
                    RefreshPresentationMode();
                    diagnostics.Mark(thermalPhase, "skeleton_visibility", NativePreviewBounds, showSkeleton ? "on" : "off");
                    break;
                case "fullscreen": SetFullscreenPreview(command.Enabled); break;
                case "connect": ValidateEditorConnection(); Run(Connect()); break;
                case "stop":
                    if (view == null) throw new ArgumentException("The SDK is already unloaded.");
                    Run(StopRuntime()); break;
                case "restart": ValidateEditorConnection(); Run(Restart()); break;
                case "unload":
                    if (view == null) throw new ArgumentException("The SDK is already unloaded.");
                    Run(Unload()); break;
                case "fps":
                    interactive.TargetFps = command.Fps;
                    SetInteractiveFrameRate(command.Fps);
                    break;
            }
        }

        private void ApplyEditorBasicOptions(EditorCommand command)
        {
            if (!HasManualQuality(startup.AccelerationMode, startup.InputQuality)) throw new ArgumentException(BackendReason(startup.AccelerationMode));
            // Validate the entire candidate before editing the GUI draft. Regions
            // are the existing assignment settings, not a crop of analysis input.
            var candidate = JsonUtility.FromJson<HumanVisionSettingsData>(JsonUtility.ToJson(interactive.Data));
            EditEditorBasicDraft(candidate, command);
            bool preview = command.Action == "preview" ? command.Enabled : interactive.Preview;
            BuildInteractiveOptions(candidate, interactive.Execution, preview).Validate();
            OpenBasicOptions();
            if (command.Action == "source") SelectDraftSource(command.Kind);
            EditEditorBasicDraft(interactive.Data, command);
            interactive.Preview = preview;
            SetDraftText();
            ApplyBasicOptions();
            if (basicOptionsOpen) throw new ArgumentException(diagnostics.Redact(status));
        }

        private static void EditEditorBasicDraft(HumanVisionSettingsData data, EditorCommand command)
        {
            if (command.Action == "source") {
                data.SourceKind = command.Kind;
                SetEditorSourceAddress(data, command);
            } else if (command.Action == "regions") {
                data.Recognition.UseRegions = command.Enabled;
                if (command.RegionText != null) data.Recognition.Regions = ParseInteractiveRegions(command.RegionText);
            } else if (command.Action == "display") {
                data.Recognition.MinimumJointConfidence = command.Confidence;
                data.Recognition.MaximumResultAgeMilliseconds = command.AgeMs;
                data.Recognition.MaximumHandAgeMilliseconds = command.HandAgeMs;
            }
        }

        private void ValidateEditorConnection()
        {
            if (!HasManualQuality(startup.AccelerationMode, startup.InputQuality)) throw new ArgumentException(BackendReason(startup.AccelerationMode));
            var options = startup.Clone();
            switch (options.SourceKind) {
                case InputKind.Rtsp: options.Input.RtspUrl = rtspUrl.Trim(); break;
                case InputKind.Video: options.Input.VideoPath = rtspUrl.Trim(); break;
                case InputKind.WebCamera: options.Input.CameraDevice = rtspUrl.Trim(); break;
            }
            ValidateInteractiveSource(options);
            options.Validate();
        }

        private static void SetEditorSourceAddress(HumanVisionSettingsData data, EditorCommand command)
        {
            switch (command.Kind) {
                case InputKind.Rtsp: data.Mode.RtspUrl = command.Address; break;
                case InputKind.Video: data.Mode.VideoPath = command.Address; break;
                case InputKind.WebCamera: data.Mode.CameraDevice = command.Address; break;
            }
        }

        private static EditorCommand ParseEditorVerificationCommand(string json)
        {
            if (string.IsNullOrWhiteSpace(json) || json.Length > 4096) throw new ArgumentException("Command JSON must contain 1–4096 characters.");
            // Reuse the SDK configuration reader: it rejects duplicate keys,
            // malformed/trailing JSON and distinguishes integer/bool/string.
            var parser = typeof(HumanVisionSdkConfiguration).Assembly.GetType("HumanVision.HumanVisionConfigurationJson")
                ?.GetMethod("Parse", BindingFlags.Static | BindingFlags.NonPublic);
            if (parser == null) throw new ArgumentException("The SDK strict configuration reader is unavailable.");
            Dictionary<string, object> fields;
            try { fields = parser.Invoke(null, new object[] { json }) as Dictionary<string, object>; }
            catch (TargetInvocationException) { throw new ArgumentException("Command JSON is malformed or contains duplicate fields."); }
            if (fields == null) throw new ArgumentException("Command JSON must be an object.");
            var command = new EditorCommand { Action = CommandText(fields, "action"), Id = "" };
            if (fields.ContainsKey("id")) {
                command.Id = CommandText(fields, "id");
                if (command.Id.Length > 64) throw new ArgumentException("Command id must contain at most 64 characters.");
            }
            string[] allowed;
            switch (command.Action) {
                case "compute":
                    allowed = new[] { "mode", "quality" };
                    switch (CommandText(fields, "mode").ToLowerInvariant()) {
                        case "cpu": command.Mode = HumanVisionAccelerationMode.Cpu; break;
                        case "gpu": case "graphics": command.Mode = HumanVisionAccelerationMode.Graphics; break;
                        case "npu": case "neural": command.Mode = HumanVisionAccelerationMode.Neural; break;
                        default: throw new ArgumentException("Compute mode must be cpu, gpu or npu.");
                    }
                    switch (CommandText(fields, "quality").ToLowerInvariant()) {
                        case "low": command.Quality = ModelInputQuality.Low; break;
                        case "medium": command.Quality = ModelInputQuality.Medium; break;
                        case "high": command.Quality = ModelInputQuality.High; break;
                        case "ultra-low": case "ultralow": command.Quality = ModelInputQuality.UltraLow; break;
                        default: throw new ArgumentException("Quality must be low, medium, high or ultra-low.");
                    }
                    break;
                case "source":
                    allowed = new[] { "kind", "address" };
                    command.Address = CommandText(fields, "address").Trim();
                    switch (CommandText(fields, "kind").ToLowerInvariant()) {
                        case "rtsp": command.Kind = InputKind.Rtsp; break;
                        case "video": command.Kind = InputKind.Video; break;
                        case "camera": case "webcam": case "webcamera": command.Kind = InputKind.WebCamera; break;
                        default: throw new ArgumentException("Source kind must be rtsp, video or camera.");
                    }
                    if (command.Kind != InputKind.WebCamera && command.Address.Length == 0) throw new ArgumentException("The input address is required.");
                    break;
                case "fps":
                    allowed = new[] { "value" };
                    if (!fields.TryGetValue("value", out var fps) || !(fps is long number) || number != 15 && number != 30 && number != 60)
                        throw new ArgumentException("FPS value must be integer 15, 30 or 60.");
                    command.Fps = (int)number;
                    break;
                case "capacity":
                    allowed = new[] { "value" };
                    if (!fields.TryGetValue("value", out var count) || !(count is long bodies) || bodies < 1 || bodies > 8)
                        throw new ArgumentException("Capacity value must be an integer from 1 to 8.");
                    command.Capacity = (int)bodies;
                    break;
                case "preview": case "skeleton": case "fullscreen": case "regions":
                    allowed = command.Action == "regions" ? new[] { "enabled", "text" } : new[] { "enabled" };
                    if (!fields.TryGetValue("enabled", out var flag) || !(flag is bool enabled))
                        throw new ArgumentException("Command requires boolean field: enabled");
                    command.Enabled = enabled;
                    if (command.Action == "regions" && fields.ContainsKey("text")) command.RegionText = CommandText(fields, "text");
                    break;
                case "display":
                    allowed = new[] { "confidence", "ageMs", "handAgeMs" };
                    command.Confidence = CommandNumber(fields, "confidence");
                    command.AgeMs = CommandNumber(fields, "ageMs");
                    command.HandAgeMs = CommandNumber(fields, "handAgeMs");
                    if (command.Confidence < 0 || command.Confidence > 1 || command.AgeMs <= 0 || command.HandAgeMs <= 0)
                        throw new ArgumentException("Display confidence must be 0–1 and result ages must be positive.");
                    break;
                case "connect": case "stop": case "restart": case "unload": case "save": case "load":
                    allowed = Array.Empty<string>(); break;
                default: throw new ArgumentException("Unknown verification action.");
            }
            foreach (string key in fields.Keys)
                if (key != "action" && key != "id" && Array.IndexOf(allowed, key) < 0) throw new ArgumentException("Command contains an unexpected field.");
            return command;
        }

        private static string CommandText(Dictionary<string, object> fields, string key) =>
            fields.TryGetValue(key, out var value) && value is string text ? text : throw new ArgumentException("Command requires string field: " + key);

        private static float CommandNumber(Dictionary<string, object> fields, string key)
        {
            if (!fields.TryGetValue(key, out var value) || !(value is long) && !(value is double))
                throw new ArgumentException("Command requires numeric field: " + key);
            double number = Convert.ToDouble(value);
            if (double.IsInfinity(number) || double.IsNaN(number) || number < -float.MaxValue || number > float.MaxValue)
                throw new ArgumentException("Command numeric field exceeds the supported range: " + key);
            return (float)number;
        }
    }
}
#endif
