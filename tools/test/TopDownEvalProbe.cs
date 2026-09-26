using System;
using System.IO;
using HumanVision.Demo;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace HumanVision
{
    // Evaluation-only component copied into the ignored Android build project.
    [DefaultExecutionOrder(-200)]
    public sealed class TopDownEvalProbe : MonoBehaviour
    {
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void RegisterScenes()
        {
            SceneManager.sceneLoaded += (scene, mode) => Debug.Log("HV_TOPDOWN_SCENE name=" + scene.name + " mode=" + mode);
        }
        public const int Capacity = 4; // Replaced by build script per APK.
        public const int Interval = 4; // Replaced by build script per APK.
        private HumanVisionManager _pipeline;
        private HumanVisionCameraManager _camera;
        private VideoPlayerFrameSource _bridge;
        private float _next;
        private float _nextStats;
        private long _lastSampledFrame = -1;
        private readonly int[] _regions = new int[8];

        private void Awake()
        {
            if (!File.Exists(HumanVisionCameraSettings.FilePath))
            {
                var settings = new HumanVisionCameraSettings();
                settings.ResizeRegions(Capacity);
                settings.Save();
            }
            else if (HumanVisionCameraSettings.Load().people != Capacity)
                throw new InvalidOperationException("Evaluation package capacity differs from saved settings");
            _pipeline = GetComponent<HumanVisionManager>();
            _camera = GetComponent<HumanVisionCameraManager>();
            _bridge = GetComponent<VideoPlayerFrameSource>();
            Debug.Log("HV_TOPDOWN_BOOT interval=" + Interval + " capacity=" + Capacity +
                " saved_people=" + HumanVisionCameraSettings.Load().people);
        }

        private void LateUpdate()
        {
            if (Time.unscaledTime >= _next)
            {
                _next = Time.unscaledTime + 1f;
                Debug.Log("HV_TOPDOWN_HEARTBEAT frame_count=" + Time.frameCount +
                    " initialized=" + (_pipeline != null && _pipeline.IsInitialized) +
                    " camera=" + (_camera == null ? "missing" : _camera.Status) +
                    " input=" + (_camera == null ? "missing" : _camera.InputStatus));
            }
            if (_pipeline == null || !_pipeline.IsInitialized) return;
            if (_pipeline.SourceFrameId != _lastSampledFrame)
            {
                _lastSampledFrame = _pipeline.SourceFrameId;
                _pipeline.TryCopyRegionAssignments(_regions, out long revision);
                Debug.Log("HV_TOPDOWN_SAMPLE frame=" + _pipeline.SourceFrameId +
                    " bodies=" + _pipeline.BodyCount + " drawn=" + _camera.GetUsersCount() +
                    " revision=" + revision + " presentation=" + _bridge.PresentationFrameId +
                    " sequence=" + _pipeline.ResultSequence +
                    " observed_age_ms=" + _bridge.ResultAgeMilliseconds);
            }
            if (Time.unscaledTime < _nextStats) return;
            _nextStats = Time.unscaledTime + 1f;
            var s = _pipeline.RuntimeStatsV2;
            Debug.Log("HV_TOPDOWN_STATS interval=" + Interval + " capacity=" + Capacity +
                " configured_people=" + _camera.Settings.people + " max_bodies=" + _pipeline.MaxBodies +
                " region_count=" + _camera.GetRegionCount() +
                " cpu_full_frame_readbacks=" + _bridge.FullFrameReadbackRequests +
                " max_overlay_lag_frames=" + _bridge.MaxOverlayLagFrames +
                " max_live_age_ms=" + _bridge.maxLiveResultAgeMilliseconds +
                " fresh=" + s.FreshObservationFrames + " fresh_fps=" + s.FreshObservationFps +
                " source_seen=" + s.SourceFramesSeen + " rate_drops=" + s.SourceRateLimitedDrops +
                " capture_requested=" + s.GpuCaptureRequested + " submitted=" + s.GpuCaptureSubmitted +
                " bridge_no_slot=" + s.GpuBridgeNoFreeSlotDrops + " bridge_superseded=" + s.GpuBridgeSupersededReadyDrops +
                " copy_errors=" + s.GpuCopyErrors + " import_errors=" + s.GpuImportErrors +
                " pose_drops=" + s.PoseJobDrops + " detector_attempted=" + s.DetectorAttempted +
                " detector_completed=" + s.DetectorCompleted + " detector_late=" + s.DetectorLate +
                " detector_discarded=" + s.DetectorDiscarded + " missed_deadlines=" + s.MissedDetectorDeadlines +
                " pose_validation_failures=" + s.PoseValidationFailures +
                " detector_interval=" + s.DetectorIntervalFrames + " detector_age_ms=" + s.DetectorAgeMs +
                " detector_completion_ms=" + s.DetectorCompletionLagMs +
                " age_p50_ms=" + s.AgeP50Ms + " age_p95_ms=" + s.AgeP95Ms +
                " sensor_p50_ms=" + s.SensorCaptureAgeP50Ms + " sensor_p95_ms=" + s.SensorCaptureAgeP95Ms +
                " provenance=" + s.CaptureProvenance + " copy_path=" + s.CopyPath +
                " source_frame=" + _pipeline.SourceFrameId + " presentation_frame=" + _bridge.PresentationFrameId +
                " bodies=" + _pipeline.BodyCount + " drawn=" + _camera.GetUsersCount() +
                " backend=" + _pipeline.RuntimeDiagnostics.Replace(' ', '_'));
            Debug.Log("HV_TOPDOWN_ERRORS manager=" + (_pipeline.LastError ?? "") +
                " bridge=" + (_bridge.LastError ?? "") + " camera=" + (_camera.Status ?? ""));
            foreach (var line in _pipeline.RuntimeDiagnostics.Split('\n'))
                Debug.Log("HV_TOPDOWN_NATIVE " + line.Trim());
        }
    }
}
