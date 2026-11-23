using System;
using UnityEngine;
using HumanVision.Interop;
using UnityEngine.Scripting;

namespace HumanVision
{
    [DefaultExecutionOrder(-100)]
    [DisallowMultipleComponent]
    public sealed class HumanVisionManager : MonoBehaviour
    {
        [Header("SDK configuration")]
        [SerializeField] private HumanVisionConfig config = new HumanVisionConfig();
        [SerializeField] private bool initializeOnStart;

        [Header("Polling")]
        [SerializeField, Min(0.05f)] private float statsRefreshSeconds = 0.25f;

        private IHumanVisionSession _session;
        private float _nextStatsRefreshTime;
        private string _lastLoggedError;

        public event Action<long> ResultUpdated;

        public bool IsInitialized => _session != null;
        public HumanVisionBody[] Bodies => _session?.Bodies;
        public int BodyCount => _session?.BodyCount ?? 0;
        public long ResultSequence => _session?.ResultSequence ?? 0;
        public long SourceFrameId => _session?.SourceFrameId ?? -1;
        public long SourceTimestampUs => _session?.SourceTimestampUs ?? 0;
        public HumanVisionStats Stats => _session?.Stats ?? default;
        public int MaxBodies => _session?.MaxBodies ?? config.MaxBodies;
        public HumanVisionBody[] SampledBodies => (_session as HumanVisionRuntimeSession)?.SampledBodies ?? Bodies;
        public int SampledBodyCount => (_session as HumanVisionRuntimeSession)?.SampledCount ?? BodyCount;
        public bool UsesRuntimeProfile => _session is HumanVisionRuntimeSession;
        public bool UsesAndroidGpuFrames => (_session as HumanVisionRuntimeSession)?.UsesGpuFrames ?? false;
        public bool UsesIndependentInput => (_session as HumanVisionRuntimeSession)?.UsesIndependentInput ?? false;
        public Vector2Int IndependentInputSize => (_session as HumanVisionRuntimeSession)?.IndependentInputSize ?? Vector2Int.zero;
        internal Vector2Int IndependentDecodedSize => (_session as HumanVisionRuntimeSession)?.IndependentDecodedSize ?? Vector2Int.zero;
        internal ulong ResultSourceId => (_session as HumanVisionRuntimeSession)?.ResultSourceId ?? 0;
        internal ulong ResultGeneration => (_session as HumanVisionRuntimeSession)?.ResultGeneration ?? 0;
        internal ulong ResultConfigEpoch => (_session as HumanVisionRuntimeSession)?.ResultConfigEpoch ?? 0;
        internal bool TryGetNativeInputState(out uint state, out ulong frames) {
            state = 0; frames = 0; return _session is HumanVisionRuntimeSession runtime && runtime.TryGetNativeInputState(out state, out frames);
        }
        internal bool PollRuntimeInitialization(out string error) {
            error = "";
            return !(_session is HumanVisionRuntimeSession runtime) || runtime.PollInitialization(out error);
        }
        [Preserve]
        public bool TryGetRuntimeProgress(out HumanVisionRuntimeProgress progress) {
            progress = default;
            return _session is HumanVisionRuntimeSession runtime && runtime.TryGetRuntimeProgress(out progress);
        }
        [Preserve]
        public bool TryGetSourceProgress(out HumanVisionSourceProgress progress) {
            progress = default; return _session is HumanVisionRuntimeSession runtime && runtime.TryGetSourceProgress(out progress);
        }
        internal int AttachLivePreview(ref RuntimeLiveConfigNative config, out RuntimeLiveBindingNative binding) {
            binding = default; return _session is HumanVisionRuntimeSession runtime ? runtime.AttachLivePreview(ref config, out binding) : RuntimeControlBindings.Unsupported;
        }
        internal int CopyNativePreview(ulong after, IntPtr buffer, uint capacity, ref RuntimePreviewNative info) =>
            _session is HumanVisionRuntimeSession runtime ? runtime.CopyNativePreview(after, buffer, capacity, ref info) : RuntimeControlBindings.Unsupported;
        internal int RequestNativeSnapshot(out ulong request) {
            request = 0; return _session is HumanVisionRuntimeSession runtime ? runtime.RequestNativeSnapshot(out request) : RuntimeControlBindings.Unsupported;
        }
        internal int CopyNativeSnapshot(ulong request, IntPtr buffer, uint capacity, ref RuntimePreviewNative info) =>
            _session is HumanVisionRuntimeSession runtime ? runtime.CopyNativeSnapshot(request, buffer, capacity, ref info) : RuntimeControlBindings.Unsupported;
        internal int CancelNativeSnapshot(ulong request) =>
            _session is HumanVisionRuntimeSession runtime ? runtime.CancelNativeSnapshot(request) : RuntimeControlBindings.Unsupported;
        public string ActiveRuntimeProfile => (_session as HumanVisionRuntimeSession)?.ProfileId ?? string.Empty;
        public float HandInferenceFps => (_session as HumanVisionRuntimeSession)?.HandFps ?? 0;
        public string RuntimeDiagnostics => (_session as HumanVisionRuntimeSession)?.Diagnostics ?? "V1 compatibility session";
        internal RuntimeStatsV2Native RuntimeStatsV2 => (_session as HumanVisionRuntimeSession)?.StatsV2 ?? default;
        internal void RecordSourceArrival(bool rateLimited) => (_session as HumanVisionRuntimeSession)?.RecordSourceArrival(rateLimited);
        internal void SetCaptureProvenance(uint provenance) => (_session as HumanVisionRuntimeSession)?.SetCaptureProvenance(provenance);
        public string LastError { get; private set; }
        public bool TrySetRegions(Rect[] regions, long revision)
        {
            if (_session == null || regions == null) return false;
            try { _session.SetRegions(regions, revision); return true; }
            catch (Exception e) { ReportError(e.Message); return false; }
        }
        public bool TryCopyRegionAssignments(int[] indices, out long revision)
        {
            revision = 0;
            if (_session == null || indices == null) return false;
            try { return _session.CopyRegions(ResultSequence, indices, out revision); }
            catch (Exception e) { ReportError(e.Message); return false; }
        }

        private void Start()
        {
            if (initializeOnStart)
            {
                TryInitialize(config);
            }
        }

        private void Update()
        {
            if (_session == null)
            {
                return;
            }

            try
            {
                if (_session.PollLatestResult())
                {
                    ResultUpdated?.Invoke(_session.ResultSequence);
                }

                if (Time.unscaledTime >= _nextStatsRefreshTime)
                {
                    _session.RefreshStats();
                    _nextStatsRefreshTime = Time.unscaledTime + statsRefreshSeconds;
                }
            }
            catch (Exception exception)
            {
                ReportError(exception.Message);
            }
        }

        private void OnDestroy()
        {
            Shutdown();
        }

        public bool TryInitialize(HumanVisionConfig requestedConfig)
        {
            if (HumanVisionAndroidAccelerationSelection.IsNeuralProfile(requestedConfig?.Profile) &&
                requestedConfig.ExecutionPolicy == HumanVisionExecutionPolicy.CompatibleUnityInput) {
                ReportError("Neural model initialization must run asynchronously. Use HumanVisionSdk.Initialize with the Neural acceleration preference.");
                return false;
            }
            Shutdown();
            try
            {
                config = requestedConfig?.Clone() ?? throw new ArgumentNullException(nameof(requestedConfig));
                _session = string.IsNullOrWhiteSpace(config.RuntimeRoot)
                    ? (IHumanVisionSession)new HumanVisionSession(config)
                    : new HumanVisionRuntimeSession(config, HumanVisionAndroidRuntimeSelection.ResolveProfile(config.Profile));
                LastError = string.Empty;
                _lastLoggedError = string.Empty;
                _nextStatsRefreshTime = 0f;
                return true;
            }
            catch (Exception exception)
            {
                ReportError(exception.Message);
                return false;
            }
        }

        // The prepared CPU session has no Unity producer or GPU registration. Adoption is main-thread only.
        internal void AdoptPreparedSession(HumanVisionConfig requestedConfig, HumanVisionRuntimeSession prepared)
        {
            if (_session != null || prepared == null) throw new InvalidOperationException("Only an empty manager can adopt a prepared session.");
            config = requestedConfig.Clone(); _session = prepared;
            LastError = _lastLoggedError = string.Empty; _nextStatsRefreshTime = 0;
        }

        public bool SubmitFrame(
            IntPtr data,
            int width,
            int height,
            int strideBytes,
            HumanVisionPixelFormat pixelFormat,
            long frameId,
            long timestampUs,
            int dataBytes)
        {
            if (_session == null)
            {
                ReportError("HumanVision is not initialized; the frame was not submitted.");
                return false;
            }

            try
            {
                return _session.SubmitFrame(
                    data,
                    width,
                    height,
                    strideBytes,
                    pixelFormat,
                    frameId,
                    timestampUs,
                    dataBytes);
            }
            catch (Exception exception)
            {
                ReportError(exception.Message);
                return false;
            }
        }
        public void BeginAndroidGpuSourceLease(RenderTexture texture)
        {
            try { (_session as HumanVisionRuntimeSession)?.BeginGpuSourceLease(texture); }
            catch (Exception exception) { ReportError(exception.Message); throw; }
        }
        public void EndAndroidGpuSourceLease()
        {
            try { (_session as HumanVisionRuntimeSession)?.EndGpuSourceLease(); }
            catch (Exception exception) { ReportError(exception.Message); throw; }
        }
        public bool TryRetireAndroidGpuSourceCopies(out HumanVisionAndroidSourceRetirement retirement)
        {
            retirement = null;
            try { return !(_session is HumanVisionRuntimeSession runtime) || runtime.TryRetireGpuSourceCopies(out retirement); }
            catch (Exception exception) { ReportError(exception.Message); throw; }
        }
        internal bool SubmitUnifiedGpuFrame(Texture preview,RenderTexture target,in HumanVision.Input.HumanVisionTextureFrame frame,
            long timestampUs,HumanVisionAndroidFrameCopyFence fence) {
            if(!(_session is HumanVisionRuntimeSession runtime))return false;
            return runtime.SubmitUnifiedGpuFrame(preview,target,in frame,timestampUs,fence);
        }
        public bool SubmitAndroidGpuFrame(RenderTexture texture, int rotationDegrees, bool mirrored, long frameId, long timestampUs)
        {
            try { bool accepted = (_session as HumanVisionRuntimeSession)?.SubmitGpuFrame(texture, rotationDegrees, mirrored, frameId, timestampUs) ?? false;
                LastError = string.Empty; return accepted; }
            catch (Exception exception) { ReportError(exception.Message); throw; }
        }

        public bool TrySetMaxBodies(int maxBodies)
        {
            if (_session == null)
            {
                ReportError("HumanVision is not initialized; MaxBodies was not changed.");
                return false;
            }

            try
            {
                _session.ReconfigureMaxBodies(maxBodies);
                config.MaxBodies = maxBodies;
                LastError = string.Empty;
                return true;
            }
            catch (Exception exception)
            {
                ReportError(exception.Message);
                return false;
            }
        }

        public void Shutdown()
        {
            if (_session == null)
            {
                return;
            }

            _session.Dispose();
            _session = null;
        }

        internal HumanVisionRuntimeSession DetachDeferredRuntimeSession()
        {
            if (!(_session is HumanVisionRuntimeSession runtime) || !runtime.UsesDeferredControl) return null;
            _session = null; return runtime;
        }

        // Detach only admitted pure CPU sessions; all Unity/GPU registration stays on the main thread.
        internal HumanVisionRuntimeSession DetachNeuralRuntimeSession()
        {
            var runtime = _session as HumanVisionRuntimeSession;
            if (runtime == null || (!HumanVisionAndroidAccelerationSelection.IsNeuralProfile(runtime.ProfileId) &&
                !HumanVisionAndroidAccelerationSelection.IsCpuProfile(runtime.ProfileId)) || runtime.UsesGpuFrames) return null;
            _session = null;
            return runtime;
        }

        private void ReportError(string message)
        {
            LastError = message ?? "Unknown HumanVision error.";
            if (string.Equals(_lastLoggedError, LastError, StringComparison.Ordinal))
            {
                return;
            }

            _lastLoggedError = LastError;
            Debug.LogError(LastError, this);
        }
    }
}
