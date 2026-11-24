using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using HumanVisionAcceptance.Diagnostics;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace HumanVisionAcceptance
{
    internal sealed class RuntimePrefabAcceptanceDiagnostics : IDisposable
    {
        private const int MaxQueuedMessages = 256;
        private const string GeometryPrefix = "HumanVision RTSP GPU geometry:";
        private static readonly char[] FieldEnd = { ';', '\r', '\n' };
        private static readonly Regex Geometry = new Regex(@"decoded=(\d+)x(\d+)\s+requestedMaximum=(\d+)x(\d+)\s+publishedOutput=(\d+)x(\d+)\s+nativeGeneration=(\d+)", RegexOptions.CultureInvariant);
        private static readonly Regex RtspAddress = new Regex(@"rtsps?://[^\s]+", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly object queueLock = new object();
        private readonly Queue<Message> messages = new Queue<Message>();
        private readonly List<Message> drain = new List<Message>(32);
        private DeviceHardwareSampler hardware;
        private HardwareSample displayedTemperatureSample;
        private AndroidNativeStageCapture nativeCapture;
        private StreamWriter writer;
        private VideoPlayerFrameSource bridge;
        private HumanVisionSdk boundSdk;
        private string activePhase = "";
        private volatile string sensitiveAddress = "";
        private string selectedStream, streamSelectionReason;
        private Vector2Int probedSize;
        private Vector2Int requestedSize;
        private double nextSample, nextFlush, previousSample;
        private double nextDisplayRefresh, displayRefreshHz = -1;
        private string displayRefreshSource = "unavailable";
        private string lifecycleStage = "";
        private int lifecycleAttempt;
        private HumanVisionSdkState? lastSdkState;
        private InputSourceState? lastInputState;
        private int previousUnityFrame, droppedMessages, exportSequence;
        private long previousResult = -1, acceptedResults;
        private double lastAcceptedAt = -1, maxAcceptedGapMs, maxUnityFrameMs;
        private ulong previousGeneration, previousSource;
        private readonly HumanVisionCanonicalJoint[] jointScratch = new HumanVisionCanonicalJoint[32];
        private readonly RtspAnalysisSlotTiming[] analysisSlotTimings = new RtspAnalysisSlotTiming[RtspAnalysisSlotTiming.Capacity];
        private long previousAnalysisSlotSampleUs = -1;
        private readonly ResultIdentity[] validResults = new ResultIdentity[128];
        private readonly List<double> resultAgesMs = new List<double>(32);
        private readonly List<double> presentedAgesMs = new List<double>(32);
        private int pairIdentityMismatchesInWindow, pairInputIdVerifiedInWindow, pairPreviewSizeMismatchesInWindow;
        private ResultIdentity lastAccepted, lastPresented;
        private ulong lastPreviewSource, lastPreviewGeneration;
        private long lastPreviewFrame = -1;
        private int acceptedInWindow, validInWindow, previewInWindow, presentedInWindow, validPresentedInWindow;
        private double lastPresentedAt = -1, maxPresentedGapMs;
        private bool started, disposed, ownsUnityLog;

        public string Status { get; private set; } = "诊断尚未开始";
        public string SessionPath { get; private set; } = "";
        internal string TemperatureLine { get; private set; } = RuntimePrefabTemperatureLine.Unavailable;
        public string ExportPath { get; private set; } = "";
        internal long AcceptedResults => acceptedResults;
        internal long ValidSkeletonResults { get; private set; }
        internal long ValidSkeletonPresented { get; private set; }
        internal double ValidSkeletonFps { get; private set; } = -1;

        private readonly struct ResultIdentity
        {
            internal readonly long Sequence, Frame;

            internal ResultIdentity(long sequence, long frame)
            {
                Sequence = sequence;
                Frame = frame;
            }

            internal bool Matches(long sequence, long frame) => Sequence == sequence && Frame == frame;
        }

        [Serializable]
        private class Entry
        {
            public string kind, utc, phase;
            public double elapsed_s;
        }

        [Serializable]
        private sealed class Marker : Entry
        {
            public string outcome, detail;
            public string lifecycle, sdk_state, input_state, sdk_last_error;
            public int attempt;
            public Vector2Int requested;
        }

        [Serializable]
        private sealed class Message : Entry
        {
            public string line;
        }

        [Serializable]
        private sealed class GeometryEntry : Entry
        {
            public Vector2Int decoded, requested, published;
            public long native_generation;
        }

        [Serializable]
        private sealed class StreamGeometryEntry : Entry
        {
            public string stream, probe_decode, source_id, source_generation, native_generation;
            public long input_frame;
            public Vector2Int probed, decoded, preview;
        }

        [Serializable]
        private sealed class SampleEntry : Entry
        {
            public string sdk_version, state, input_state, sdk_last_error, profile, model_pack, actual_backend, sdk_runtime;
            public string stream, stream_selection_reason, stream_probe_decode = "unavailable", native_generation;
            public Vector2Int stream_probed;
            public Vector2Int requested, decoded, published, model;
            // Geometry of an accepted inference submission. The model field above is the configured contract.
            public Vector2Int analysis_input, analysis_submitted;
            public Vector2Int paired_preview, paired_color;
            public string pair_source_id, pair_source_generation;
            public long pair_preview_frame = -1, pair_result_frame = -1, pair_result_sequence = -1, pair_source_us = -1;
            public int pair_identity_mismatches_window, pair_input_id_verified_window, pair_preview_size_mismatches_window;
            public double input_fps = -1, analysis_input_fps = -1, fresh_fps = -1, unity_fps = -1, frame_age_ms = -1;
            public double window_s = -1;
            public int accepted_results_window, valid_skeleton_results_window, preview_frames_window, presented_results_window, valid_skeleton_presented_window;
            public double accepted_result_fps = -1, valid_skeleton_fps = -1, preview_fps = -1, presented_fps = -1, valid_skeleton_presented_fps = -1;
            public double[] decode_to_result_samples_ms, decode_to_presented_samples_ms;
            public double decode_to_result_p50_ms = -1, decode_to_result_p95_ms = -1;
            public double decode_to_presented_p50_ms = -1, decode_to_presented_p95_ms = -1;
            public double displayed_pair_age_ms = -1, max_presented_gap_ms = -1;
            public bool displayed_pair_expired, displayed_pair_visible;
            public double display_refresh_hz = -1;
            public string display_refresh_source;
            public double max_result_gap_ms = -1, max_unity_frame_ms;
            public float preprocess_ms = -1, infer_ms = -1, postprocess_ms = -1, pipeline_ms = -1;
            public double rknn_input_set_ms = -1, rknn_execute_ms = -1, rknn_output_get_ms = -1, rknn_output_release_ms = -1;
            public bool source_frame_available, result_advanced, readback_available;
            public long input_frame = -1, input_pts_us = -1, input_published_us = -1, input_decoded_us = -1;
            public long analysis_frame = -1, analysis_published_us = -1;
            public long analysis_slot_sample_us = -1;
            public RtspAnalysisSlotTiming[] analysis_slot_timings;
            public long matched_input_frame = -1, matched_analysis_published_us = -1;
            public long matched_analysis_accepted_us = -1, matched_preview_published_us = -1;
            public long result_frame = -1, result_sequence = -1, result_source_us = -1;
            public long submitted = -1, processed = -1, dropped = -1, accepted_results;
            public string source_id, source_generation, input_clock_id, input_clock_domain;
            public string analysis_source_id, analysis_source_generation;
            public int bodies, unity_frame, target_unity_fps, configured_vsync_count, display_width, display_height;
            public VideoPlayerFrameSource.CpuReadbackStatistics cpu_readback;
            public HardwareSample hardware;
        }

        internal void SetInputAddress(string address) => sensitiveAddress = address ?? "";

        internal void RecordStreamSelection(string phase, string stream, Vector2Int probed, string reason, Vector2Int bounds)
        {
            selectedStream = stream;
            streamSelectionReason = reason;
            probedSize = probed;
            string detail = $"stream={stream} probed={probed.x}x{probed.y} reason={reason}";
            Mark(phase, "stream_selected", bounds, detail);
            Debug.Log("HumanVision RTSP stream selected: " + detail);
        }

        internal static string ProbeDecodeStatus(Vector2Int probed, Vector2Int decoded)
        {
            if (probed.x <= 0 || probed.y <= 0 || decoded.x <= 0 || decoded.y <= 0) return "unavailable";
            return probed == decoded ? "matched" : "mismatch";
        }

        internal void RecordDisplayedStream(string phase, in HumanVisionTextureFrame frame, Vector2Int decoded, ulong nativeGeneration)
        {
            var preview = new Vector2Int(frame.Width, frame.Height);
            string comparison = ProbeDecodeStatus(probedSize, decoded);
            string decoder = decoded.x > 0 ? $"{decoded.x}x{decoded.y}" : "unreported";
            string detail = $"stream={selectedStream} decoded={decoder} preview={preview.x}x{preview.y} probed={probedSize.x}x{probedSize.y} probe_decode={comparison}";
            Mark(phase, "stream_displayed", requestedSize, detail);
            Write(Stamp(new StreamGeometryEntry {
                stream = selectedStream, probed = probedSize, decoded = decoded, preview = preview, probe_decode = comparison,
                source_id = frame.SourceId.ToString(CultureInfo.InvariantCulture),
                source_generation = frame.Generation.ToString(CultureInfo.InvariantCulture),
                native_generation = nativeGeneration > 0 ? nativeGeneration.ToString(CultureInfo.InvariantCulture) : "",
                input_frame = frame.FrameId
            }, "stream_geometry", phase));
            Flush();
            Debug.Log("HumanVision RTSP stream displayed: " + detail);
        }

        internal void Mark(string phase, string outcome, Vector2Int requested, string detail = null,
            HumanVisionSdk sdk = null, int attempt = 0, string lifecycle = null)
        {
            if (!EnsureStarted()) return;
            DrainMessages();
            var value = CreateMarker(outcome, requested, detail, sdk, attempt, lifecycle);
            lock (queueLock) activePhase = phase ?? "";
            requestedSize = requested;
            if (outcome == "request" || outcome == "initialize_begin") {
                ClearObservedFrames();
            }
            if (outcome == "request") {
                selectedStream = streamSelectionReason = null;
                probedSize = default;
            }
            Write(Stamp(value, "mark", phase));
            Flush();
        }

        internal void TrackLifecycle(string stage, int attempt)
        {
            lifecycleStage = stage;
            lifecycleAttempt = attempt;
            lastSdkState = null;
            lastInputState = null;
        }

        private Marker CreateMarker(string outcome, Vector2Int requested, string detail, HumanVisionSdk sdk,
            int attempt, string lifecycle) => new Marker {
            outcome = outcome, detail = Redact(detail), requested = requested,
            attempt = attempt, lifecycle = lifecycle,
            sdk_state = sdk?.State.ToString(), input_state = sdk?.InputState.ToString(), sdk_last_error = Redact(sdk?.LastError)
        };

        private void ObserveLifecycle(HumanVisionSdk sdk, string phase)
        {
            if (sdk == null || string.IsNullOrEmpty(lifecycleStage) ||
                (lastSdkState == sdk.State && lastInputState == sdk.InputState)) return;
            lastSdkState = sdk.State;
            lastInputState = sdk.InputState;
            var marker = CreateMarker("state_changed", requestedSize, null, sdk, lifecycleAttempt, lifecycleStage);
            Write(Stamp(marker, "lifecycle", phase));
        }

        internal void Sample(HumanVisionSdk sdk, string phase)
        {
            BindSdk(sdk);
            if (!EnsureStarted()) return;
            ObserveLifecycle(sdk, phase);
            maxUnityFrameMs = Math.Max(maxUnityFrameMs, Time.unscaledDeltaTime * 1000d);
            double now = clock.Elapsed.TotalSeconds;
            if (now < nextSample) return;
            nextSample = now + 1;
            DrainMessages();
            RefreshDisplayInfo(now);
            int unityFrame = Time.frameCount;
            var sample = Stamp(new SampleEntry {
                sdk_version = HumanVisionSdk.Version,
                unity_frame = unityFrame,
                target_unity_fps = Application.targetFrameRate,
                configured_vsync_count = QualitySettings.vSyncCount,
                display_refresh_hz = displayRefreshHz,
                display_refresh_source = displayRefreshSource,
                display_width = Display.main.systemWidth,
                display_height = Display.main.systemHeight,
                unity_fps = previousSample > 0 ? (unityFrame - previousUnityFrame) / (now - previousSample) : -1,
                max_unity_frame_ms = maxUnityFrameMs,
                accepted_results = acceptedResults,
                max_result_gap_ms = lastAcceptedAt >= 0 ? Math.Max(maxAcceptedGapMs, (now - lastAcceptedAt) * 1000) : -1,
                requested = requestedSize,
                stream = selectedStream,
                stream_selection_reason = streamSelectionReason,
                stream_probed = probedSize,
                hardware = hardware?.Read()
            }, "sample", phase);
            if (!ReferenceEquals(sample.hardware, displayedTemperatureSample)) {
                displayedTemperatureSample = sample.hardware;
                TemperatureLine = RuntimePrefabTemperatureLine.Format(displayedTemperatureSample);
            }
            SampleWindows(sample, previousSample > 0 ? now - previousSample : -1, now);
            maxUnityFrameMs = maxAcceptedGapMs = 0;
            previousUnityFrame = unityFrame;
            previousSample = now;
            if (sdk != null) CaptureSdk(sdk, sample);
            Write(sample);
            if (now >= nextFlush) Flush();
        }

        private void RefreshDisplayInfo(double now)
        {
            if (now < nextDisplayRefresh) return;
            nextDisplayRefresh = now + 5;
#if UNITY_ANDROID && !UNITY_EDITOR
            try {
                using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
                using (var manager = activity.Call<AndroidJavaObject>("getWindowManager"))
                using (var display = manager.Call<AndroidJavaObject>("getDefaultDisplay")) {
                    float hz = display.Call<float>("getRefreshRate");
                    if (hz > 0 && !float.IsInfinity(hz)) {
                        displayRefreshHz = hz;
                        displayRefreshSource = "android_display";
                        return;
                    }
                }
            } catch (Exception) { }
#endif
            double fallback = Screen.currentResolution.refreshRate;
            displayRefreshHz = fallback > 0 ? fallback : -1;
            displayRefreshSource = fallback > 0 ? "unity_current_resolution" : "unavailable";
        }

        internal void BindSdk(HumanVisionSdk sdk)
        {
            if (ReferenceEquals(boundSdk, sdk)) return;
            if (!ReferenceEquals(boundSdk, null)) boundSdk.ResultUpdated -= OnResult;
            boundSdk = sdk;
            bridge = null;
            ClearObservedFrames();
            if (sdk != null) sdk.ResultUpdated += OnResult;
        }

        private void ClearObservedFrames()
        {
            lastAccepted = lastPresented = default;
            lastAcceptedAt = lastPresentedAt = -1;
            maxAcceptedGapMs = maxPresentedGapMs = 0;
            previousResult = -1;
            previousAnalysisSlotSampleUs = -1;
            lastPreviewSource = lastPreviewGeneration = 0;
            lastPreviewFrame = -1;
            Array.Clear(validResults, 0, validResults.Length);
        }

        private void OnResult(long sequence)
        {
            var sdk = boundSdk;
            if (sdk == null || !sdk.IsRunning || !sdk.HasFreshResult || sequence <= 0 ||
                sequence != sdk.ResultSequence || sdk.SourceFrameId < 0 || lastAccepted.Matches(sequence, sdk.SourceFrameId)) return;
            long resultFrame = sdk.SourceFrameId;
            lastAccepted = new ResultIdentity(sequence, resultFrame);
            double now = clock.Elapsed.TotalSeconds;
            if (lastAcceptedAt >= 0) maxAcceptedGapMs = Math.Max(maxAcceptedGapMs, (now - lastAcceptedAt) * 1000);
            lastAcceptedAt = now;
            acceptedResults++;
            acceptedInWindow++;

            if (!HasUsableSkeleton(sdk, sequence, resultFrame)) return;
            ValidSkeletonResults++;
            validInWindow++;
            validResults[(int)(sequence % validResults.Length)] = lastAccepted;
            if (sdk.TryGetResultDecodeAgeMs(resultFrame, out _, out double ageMs)) resultAgesMs.Add(ageMs);
        }

        private bool HasUsableSkeleton(HumanVisionSdk sdk, long sequence, long resultFrame)
        {
            int capacity = sdk.GetMaxBodies();
            for (int i = 0; i < capacity; ++i) {
                if (!sdk.CopySkeletonByIndex(i, jointScratch, out var metadata) || metadata.StableTrackId <= 0 ||
                    metadata.ResultSequence != sequence || metadata.SourceFrameId != resultFrame) continue;
                // The SDK copy filters confidence, finite coordinates and observation age.
                int observedJoints = 0;
                foreach (var joint in jointScratch)
                    if (joint.Position.Valid && !joint.Position.IsDerived && joint.PredictionMilliseconds == 0 && ++observedJoints >= 2)
                        return true;
            }
            return false;
        }

        internal void ObservePreview(in HumanVisionTextureFrame frame)
        {
            if (frame.SourceId == lastPreviewSource && frame.Generation == lastPreviewGeneration &&
                frame.FrameId == lastPreviewFrame) return;
            lastPreviewSource = frame.SourceId;
            lastPreviewGeneration = frame.Generation;
            lastPreviewFrame = frame.FrameId;
            previewInWindow++;
        }

        internal bool ObservePresented(in HumanVisionPresentedFrame frame)
        {
            if (frame.ResultSequence <= 0 || frame.SourceFrameId < 0 || frame.InputFrameId < 0 ||
                lastPresented.Matches(frame.ResultSequence, frame.SourceFrameId)) return false;
            lastPresented = new ResultIdentity(frame.ResultSequence, frame.SourceFrameId);
            double now = clock.Elapsed.TotalSeconds;
            if (lastPresentedAt >= 0) maxPresentedGapMs = Math.Max(maxPresentedGapMs, (now - lastPresentedAt) * 1000);
            lastPresentedAt = now;
            presentedInWindow++;

            if (boundSdk != null && boundSdk.TryGetInputFrame(-1, out var live)) {
                if (frame.SourceId != live.SourceId || frame.SourceGeneration != live.Generation ||
                    frame.SourceFrameId != boundSdk.SourceFrameId || frame.ResultSequence != boundSdk.ResultSequence ||
                    frame.SourceTimestampUs != boundSdk.SourceTimestampUs) pairIdentityMismatchesInWindow++;
                if (frame.ColorTexture == null || frame.ColorTexture.width != live.Width || frame.ColorTexture.height != live.Height ||
                    frame.OverlayTexture == null || frame.OverlayTexture.width != live.Width || frame.OverlayTexture.height != live.Height)
                    pairPreviewSizeMismatchesInWindow++;
            }
            long inputFrameId = -1;
            double ageMs = -1;
            bool hasDecodeAge = boundSdk != null && boundSdk.TryGetResultDecodeAgeMs(
                frame.SourceFrameId, out inputFrameId, out ageMs);
            if (hasDecodeAge) {
                pairInputIdVerifiedInWindow++;
                if (inputFrameId != frame.InputFrameId) pairIdentityMismatchesInWindow++;
            }

            if (!validResults[(int)(frame.ResultSequence % validResults.Length)].Matches(frame.ResultSequence, frame.SourceFrameId))
                return false;
            ValidSkeletonPresented++;
            validPresentedInWindow++;
            if (hasDecodeAge && inputFrameId == frame.InputFrameId) presentedAgesMs.Add(ageMs);
            return true;
        }

        private void SampleWindows(SampleEntry sample, double windowSeconds, double now)
        {
            sample.window_s = windowSeconds;
            sample.accepted_results_window = acceptedInWindow;
            sample.valid_skeleton_results_window = validInWindow;
            sample.preview_frames_window = previewInWindow;
            sample.presented_results_window = presentedInWindow;
            sample.valid_skeleton_presented_window = validPresentedInWindow;
            sample.pair_identity_mismatches_window = pairIdentityMismatchesInWindow;
            sample.pair_input_id_verified_window = pairInputIdVerifiedInWindow;
            sample.pair_preview_size_mismatches_window = pairPreviewSizeMismatchesInWindow;
            pairIdentityMismatchesInWindow = pairInputIdVerifiedInWindow = pairPreviewSizeMismatchesInWindow = 0;
            if (windowSeconds > 0) {
                sample.accepted_result_fps = acceptedInWindow / windowSeconds;
                sample.valid_skeleton_fps = ValidSkeletonFps = validInWindow / windowSeconds;
                sample.preview_fps = previewInWindow / windowSeconds;
                sample.presented_fps = presentedInWindow / windowSeconds;
                sample.valid_skeleton_presented_fps = validPresentedInWindow / windowSeconds;
            }
            sample.decode_to_result_samples_ms = TakeAges(resultAgesMs, out sample.decode_to_result_p50_ms, out sample.decode_to_result_p95_ms);
            sample.decode_to_presented_samples_ms = TakeAges(presentedAgesMs, out sample.decode_to_presented_p50_ms, out sample.decode_to_presented_p95_ms);
            sample.displayed_pair_age_ms = lastPresentedAt >= 0 ? (now - lastPresentedAt) * 1000 : -1;
            sample.max_presented_gap_ms = lastPresentedAt >= 0 ? Math.Max(maxPresentedGapMs, sample.displayed_pair_age_ms) : -1;
            maxPresentedGapMs = 0;
            acceptedInWindow = validInWindow = previewInWindow = presentedInWindow = validPresentedInWindow = 0;
        }

        private static double[] TakeAges(List<double> values, out double p50, out double p95)
        {
            p50 = p95 = -1;
            if (values.Count == 0) return Array.Empty<double>();
            values.Sort();
            p50 = (values[(values.Count - 1) / 2] + values[values.Count / 2]) / 2;
            p95 = values[(int)Math.Ceiling(values.Count * .95) - 1];
            var sample = values.ToArray();
            values.Clear();
            return sample;
        }

        private void CaptureSdk(HumanVisionSdk sdk, SampleEntry result)
        {
            result.state = sdk.State.ToString();
            result.input_state = sdk.InputState.ToString();
            result.sdk_last_error = Redact(sdk.LastError);
            result.input_fps = sdk.InputPublicationFps;
            result.analysis_input_fps = sdk.AnalysisInputPublicationFps;
            result.fresh_fps = sdk.FreshResultFps;
            result.profile = sdk.RuntimeProfile;
            result.model_pack = sdk.ActiveModelPack;
            result.model = sdk.AnalysisInputSize;
            result.analysis_submitted = sdk.SubmittedAnalysisInputSize;
            result.result_frame = sdk.SourceFrameId;
            result.result_sequence = sdk.ResultSequence;
            result.result_source_us = sdk.SourceTimestampUs;
            result.bodies = sdk.GetUsersCount();
            result.frame_age_ms = sdk.HasFreshResult ? sdk.ResultAgeMilliseconds : -1;
            CaptureAnalysisSlotTimings(sdk, result);
            hardware?.SetNeuralActive(sdk.IsInitialized && sdk.ActiveAccelerationMode == HumanVisionAccelerationMode.Neural);
            var view = HumanVisionRuntimeView.Instance;
            if (view != null && view.Sdk == sdk) {
                result.displayed_pair_expired = view.DisplayedFrameExpired;
                result.displayed_pair_visible = view.TryGetDisplayedFrame(out _);
            }

            if (sdk.TryGetAnalysisInputFrame(-1, out var analysis)) {
                result.analysis_input = new Vector2Int(analysis.Width, analysis.Height);
                result.analysis_frame = analysis.FrameId;
                result.analysis_published_us = analysis.PublishedTimestampUs;
                result.analysis_source_id = analysis.SourceId.ToString(CultureInfo.InvariantCulture);
                result.analysis_source_generation = analysis.Generation.ToString(CultureInfo.InvariantCulture);
            }
            if (sdk.TryGetMatchedAnalysisInputTiming(out long matchedFrame, out long analysisPublished,
                    out long analysisAccepted, out long previewPublished)) {
                result.matched_input_frame = matchedFrame;
                result.matched_analysis_published_us = analysisPublished;
                result.matched_analysis_accepted_us = analysisAccepted;
                result.matched_preview_published_us = previewPublished;
            }

            if (sdk.TryGetInputFrame(-1, out var frame)) {
                result.source_frame_available = true;
                result.input_frame = frame.FrameId;
                result.input_pts_us = frame.PresentationTimestampUs;
                result.input_published_us = frame.PublishedTimestampUs;
                result.input_decoded_us = frame.SourceTimestampUs;
                result.source_generation = frame.Generation.ToString(CultureInfo.InvariantCulture);
                result.source_id = frame.SourceId.ToString(CultureInfo.InvariantCulture);
                result.input_clock_id = frame.SourceClockId.ToString(CultureInfo.InvariantCulture);
                result.input_clock_domain = frame.SourceClockDomain.ToString();
                result.published = new Vector2Int(frame.Width, frame.Height);
                if (sdk.TryGetInputDecodedGeometry(in frame, out var decoded, out ulong nativeGeneration)) {
                    result.decoded = decoded;
                    result.native_generation = nativeGeneration.ToString(CultureInfo.InvariantCulture);
                    result.stream_probe_decode = ProbeDecodeStatus(probedSize, decoded);
                }
                if (view != null && view.Sdk == sdk && view.TryGetFrame(out var paired) && paired.OverlayTexture != null) {
                    result.paired_preview = new Vector2Int(paired.OverlayTexture.width, paired.OverlayTexture.height);
                    if (paired.ColorTexture != null) result.paired_color = new Vector2Int(paired.ColorTexture.width, paired.ColorTexture.height);
                    result.pair_source_id = paired.SourceId.ToString(CultureInfo.InvariantCulture);
                    result.pair_source_generation = paired.SourceGeneration.ToString(CultureInfo.InvariantCulture);
                    result.pair_preview_frame = paired.InputFrameId;
                    result.pair_result_frame = paired.SourceFrameId;
                    result.pair_result_sequence = paired.ResultSequence;
                    result.pair_source_us = paired.SourceTimestampUs;
                }
                if (previousGeneration != frame.Generation || previousSource != frame.SourceId) {
                    previousGeneration = frame.Generation;
                    previousSource = frame.SourceId;
                    previousResult = -1;
                    bridge = null;
                }
                if (bridge == null) bridge = FindBridge(frame);
                if (bridge != null) {
                    result.readback_available = sdk.ActiveAccelerationMode != HumanVisionAccelerationMode.Graphics;
                    if (result.readback_available) result.cpu_readback = bridge.CpuReadbackStats;
                }
            }

            if (!sdk.IsInitialized) return;
            result.result_advanced = sdk.HasFreshResult && result.result_sequence > 0 && result.result_sequence != previousResult;
            if (result.result_advanced) previousResult = result.result_sequence;
            var stats = sdk.Stats;
            result.submitted = stats.SubmittedFrames;
            result.processed = stats.ProcessedFrames;
            result.dropped = stats.DroppedFrames;
            result.preprocess_ms = stats.DetectionMs;
            result.infer_ms = stats.PoseMs;
            result.postprocess_ms = stats.TrackingMs;
            result.pipeline_ms = stats.TotalMs;
            try {
                string runtime = sdk.RuntimeDiagnostics;
                result.sdk_runtime = Redact(runtime);
                result.actual_backend = RuntimePrefabAcceptance.NormalizeBackend(ReadField(runtime, "Actual backend="), result.profile);
                if (result.actual_backend == "backend.rknn") {
                    result.rknn_input_set_ms = ReadNumber(runtime, "input_set_ms=");
                    result.rknn_execute_ms = ReadNumber(runtime, "execute_ms=");
                    result.rknn_output_get_ms = ReadNumber(runtime, "output_get_ms=");
                    result.rknn_output_release_ms = ReadNumber(runtime, "output_release_ms=");
                }
            } catch (Exception error) { result.sdk_runtime = "unavailable: " + error.GetType().Name; }
        }

        private void CaptureAnalysisSlotTimings(HumanVisionSdk sdk, SampleEntry sample)
        {
            long sampledUs = InputMonotonicClock.NowUs;
            int count = sdk.CopyAnalysisSlotTimings(analysisSlotTimings);
            int written = 0;
            for (int i = 0; i < count; ++i) {
                var timing = analysisSlotTimings[i];
                if (timing.SlotOutstanding || timing.LastUpdatedUs >= previousAnalysisSlotSampleUs)
                    analysisSlotTimings[written++] = timing;
            }
            previousAnalysisSlotSampleUs = sampledUs;
            sample.analysis_slot_sample_us = sampledUs;
            sample.analysis_slot_timings = Array.Empty<RtspAnalysisSlotTiming>();
            if (written == 0) return;
            sample.analysis_slot_timings = new RtspAnalysisSlotTiming[written];
            Array.Copy(analysisSlotTimings, sample.analysis_slot_timings, written);
        }

        private static VideoPlayerFrameSource FindBridge(HumanVisionTextureFrame frame)
        {
            // Match the source identity as well as its borrowed texture. The
            // acceptance assembly uses the SDK's public surface on IL2CPP.
            foreach (var adapter in UnityEngine.Object.FindObjectsOfType<HumanVisionInputAdapter>())
                if (adapter.SourceId == frame.SourceId && adapter.SourceGeneration == frame.Generation && adapter.PreviewTexture == frame.Texture)
                    return adapter.GetComponent<VideoPlayerFrameSource>();
            return null;
        }

        private static string ReadField(string text, string key)
        {
            int start = text.IndexOf(key, StringComparison.Ordinal);
            if (start < 0) return "";
            start += key.Length;
            int end = text.IndexOfAny(FieldEnd, start);
            return (end < 0 ? text.Substring(start) : text.Substring(start, end - start)).Trim();
        }

        private static double ReadNumber(string text, string key) =>
            double.TryParse(ReadField(text, key), NumberStyles.Float, CultureInfo.InvariantCulture, out double result) ? result : -1;

        private bool EnsureStarted()
        {
            if (disposed) return false;
            if (started) return writer != null;
            started = true;
            try {
                string folder = Path.Combine(Application.persistentDataPath, "RuntimePrefabDiagnostics");
                Directory.CreateDirectory(folder);
                string name = "runtime-prefab-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture) + ".jsonl";
                SessionPath = Path.Combine(folder, name);
                writer = new StreamWriter(new FileStream(SessionPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read), new UTF8Encoding(false));
                Status = "正在记录诊断";
                Write(Stamp(new Marker {
                    outcome = "started",
                    detail = "device=" + SystemInfo.deviceModel + " graphics=" + SystemInfo.graphicsDeviceType + " sdk=" + HumanVisionSdk.Version
                        + " logcat_local_utc_offset_min=" + TimeZoneInfo.Local.GetUtcOffset(DateTime.Now).TotalMinutes.ToString(CultureInfo.InvariantCulture)
                }, "session", ""));
                Application.logMessageReceivedThreaded += CaptureGeometry;
                ownsUnityLog = true;
#if UNITY_ANDROID && !UNITY_EDITOR
                hardware = new DeviceHardwareSampler(SystemInfo.processorCount);
                hardware.Start();
                nativeCapture = new AndroidNativeStageCapture((line, _) => CaptureNative(line));
                nativeCapture.Start();
#endif
                Debug.Log("HV_RT_DIAGNOSTICS session=" + SessionPath);
                Flush();
            } catch (Exception error) {
                Status = "诊断启动失败 · " + error.GetType().Name;
                Debug.LogWarning("HV_RT_DIAGNOSTICS " + Status);
            }
            return writer != null;
        }

        private void CaptureGeometry(string line, string stackTrace, LogType type)
        {
            if (line.StartsWith(GeometryPrefix, StringComparison.Ordinal)) QueueLine("geometry_log", line);
            else if (line.StartsWith("HumanVision RTSP split feed:", StringComparison.Ordinal))
                QueueLine("split_feed", line);
        }

        private void CaptureNative(string line)
        {
            // Whitelist cadence records and startup metadata. Native logcat is
            // restricted to the current app PID. Arbitrary application logs and
            // camera addresses are not included in the exported session.
            if (line.IndexOf("input_decode_cadence ", StringComparison.Ordinal) >= 0 ||
                line.IndexOf("input_gpu_cadence ", StringComparison.Ordinal) >= 0 ||
                line.IndexOf("input_split_", StringComparison.Ordinal) >= 0 ||
                line.IndexOf("input_analysis_", StringComparison.Ordinal) >= 0 ||
                line.IndexOf("actual_codec_format=", StringComparison.Ordinal) >= 0 ||
                line.IndexOf("input_surface_release ", StringComparison.Ordinal) >= 0 ||
                line.IndexOf(" E HVInputGate:", StringComparison.Ordinal) >= 0 ||
                line.StartsWith("native.capture.", StringComparison.Ordinal)) QueueLine("native", Redact(line));
        }

        private void QueueLine(string kind, string line)
        {
            lock (queueLock) {
                if (messages.Count == MaxQueuedMessages) {
                    messages.Dequeue();
                    droppedMessages++;
                }
                messages.Enqueue(Stamp(new Message { line = line }, kind, activePhase));
            }
        }

        private void DrainMessages()
        {
            int missed;
            lock (queueLock) {
                while (messages.Count != 0) drain.Add(messages.Dequeue());
                missed = droppedMessages;
                droppedMessages = 0;
            }
            if (missed > 0)
                Write(Stamp(new Marker { outcome = "dropped", detail = missed.ToString(CultureInfo.InvariantCulture) }, "collector", activePhase));
            foreach (Message message in drain) {
                if (message.kind == "geometry_log") {
                    var match = Geometry.Match(message.line);
                    if (match.Success) {
                        var geometry = new GeometryEntry {
                            kind = "geometry", utc = message.utc, phase = message.phase, elapsed_s = message.elapsed_s,
                            decoded = ReadSize(match, 1), requested = ReadSize(match, 3), published = ReadSize(match, 5),
                            native_generation = long.Parse(match.Groups[7].Value, CultureInfo.InvariantCulture)
                        };
                        Write(geometry);
                        continue;
                    }
                }
                Write(message);
            }
            drain.Clear();
        }

        private static Vector2Int ReadSize(Match match, int index) => new Vector2Int(
            int.Parse(match.Groups[index].Value, CultureInfo.InvariantCulture),
            int.Parse(match.Groups[index + 1].Value, CultureInfo.InvariantCulture));

        internal string Redact(string text)
        {
            if (string.IsNullOrEmpty(text)) return "";
            string address = sensitiveAddress;
            if (!string.IsNullOrEmpty(address)) text = text.Replace(address, "[RTSP address]");
            return HumanVisionSettingsController.Redact(RtspAddress.Replace(text, "[RTSP address]"));
        }

        private T Stamp<T>(T record, string kind, string phase) where T : Entry
        {
            record.kind = kind;
            record.utc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture);
            record.elapsed_s = clock.Elapsed.TotalSeconds;
            record.phase = phase ?? "";
            return record;
        }

        private void Write<T>(T record) where T : Entry
        {
            try { writer?.WriteLine(JsonUtility.ToJson(record)); }
            catch (IOException) { Status = "诊断日志写入失败"; }
        }

        private bool Flush()
        {
            nextFlush = clock.Elapsed.TotalSeconds + 5;
            try { writer?.Flush(); return true; }
            catch (IOException) { Status = "诊断日志保存失败"; return false; }
        }

        internal bool Export()
        {
            ExportPath = "";
            if (!EnsureStarted()) return false;
            DrainMessages();
            if (!Flush()) return false;
#if UNITY_ANDROID && !UNITY_EDITOR
            if (TryExportToDownloads(SessionPath, out string path)) {
                ExportPath = path;
                Status = "已导出 · Download/HumanVisionLogs";
                Debug.Log("HV_RT_DIAGNOSTICS export=" + ExportPath);
                return true;
            }
#endif
            if (!TryExportToAppDirectory()) return false;
#if UNITY_ANDROID && !UNITY_EDITOR
            Status = "公共导出不可用 · 日志快照已保存在应用目录";
#else
            Status = "日志快照已保存在应用目录";
#endif
            Debug.Log("HV_RT_DIAGNOSTICS export=" + ExportPath);
            return true;
        }

        private bool TryExportToAppDirectory()
        {
            string name = Path.GetFileNameWithoutExtension(SessionPath) + "-export-" +
                (++exportSequence).ToString(CultureInfo.InvariantCulture) + ".jsonl";
            string path = Path.Combine(Path.GetDirectoryName(SessionPath), name);
            bool created = false;
            try {
                using (var source = new FileStream(SessionPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (var destination = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                    created = true;
                    source.CopyTo(destination);
                }
                ExportPath = path;
                return true;
            } catch (Exception error) when (error is IOException || error is UnauthorizedAccessException) {
                if (created) {
                    try { File.Delete(path); }
                    catch (Exception cleanup) when (cleanup is IOException || cleanup is UnauthorizedAccessException) { }
                }
                Status = "诊断快照导出失败 · " + error.GetType().Name;
                Debug.LogWarning("HV_RT_DIAGNOSTICS " + Status);
                return false;
            }
        }

#if UNITY_ANDROID && !UNITY_EDITOR
        private static bool TryExportToDownloads(string source, out string path)
        {
            path = "";
            AndroidJavaObject resolver = null, uri = null;
            bool completed = false;
            try {
                using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                    if (version.GetStatic<int>("SDK_INT") < 29) return false;
                using (var unity = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                using (var activity = unity.GetStatic<AndroidJavaObject>("currentActivity"))
                    resolver = activity.Call<AndroidJavaObject>("getContentResolver");
                string name = Path.GetFileNameWithoutExtension(source) + "-" + DateTime.UtcNow.ToString("HHmmssfff", CultureInfo.InvariantCulture) + ".jsonl";
                using (var downloads = new AndroidJavaClass("android.provider.MediaStore$Downloads"))
                using (var collection = downloads.GetStatic<AndroidJavaObject>("EXTERNAL_CONTENT_URI"))
                using (var values = new AndroidJavaObject("android.content.ContentValues"))
                using (var pending = new AndroidJavaObject("java.lang.Integer", 1)) {
                    values.Call("put", "_display_name", name);
                    values.Call("put", "mime_type", "application/octet-stream");
                    values.Call("put", "relative_path", "Download/HumanVisionLogs/");
                    values.Call("put", "is_pending", pending);
                    uri = resolver.Call<AndroidJavaObject>("insert", collection, values);
                }
                if (uri == null) return false;
                using (var output = resolver.Call<AndroidJavaObject>("openOutputStream", uri))
                using (var paths = new AndroidJavaClass("java.nio.file.Paths"))
                using (var input = paths.CallStatic<AndroidJavaObject>("get", source, Array.Empty<string>()))
                using (var files = new AndroidJavaClass("java.nio.file.Files")) {
                    if (output == null) return false;
                    files.CallStatic<long>("copy", input, output);
                    output.Call("close");
                }
                using (var values = new AndroidJavaObject("android.content.ContentValues"))
                using (var ready = new AndroidJavaObject("java.lang.Integer", 0)) {
                    values.Call("put", "is_pending", ready);
                    if (resolver.Call<int>("update", uri, values, null, null) <= 0) return false;
                }
                path = "Download/HumanVisionLogs/" + name;
                Debug.Log("HV_RT_DIAGNOSTICS export_uri=" + uri.Call<string>("toString"));
                completed = true;
                return true;
            } catch (Exception error) {
                Debug.LogWarning("HV_RT_DIAGNOSTICS MediaStore export failed: " + error.GetType().Name);
                return false;
            } finally {
                if (!completed && resolver != null && uri != null) {
                    try { resolver.Call<int>("delete", uri, null, null); } catch (Exception) { }
                }
                uri?.Dispose();
                resolver?.Dispose();
            }
        }
#endif

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            BindSdk(null);
            if (ownsUnityLog) Application.logMessageReceivedThreaded -= CaptureGeometry;
            nativeCapture?.Dispose();
            hardware?.Dispose();
            if (writer == null) return;
            DrainMessages();
            Write(Stamp(new Marker { outcome = "disposed" }, "session", activePhase));
            Flush();
            writer.Dispose();
            writer = null;
        }
    }
}
