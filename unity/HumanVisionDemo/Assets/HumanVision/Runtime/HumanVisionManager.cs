using System;
using UnityEngine;

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
        public float HandInferenceFps => (_session as HumanVisionRuntimeSession)?.HandFps ?? 0;
        public string RuntimeDiagnostics => (_session as HumanVisionRuntimeSession)?.Diagnostics ?? "V1 compatibility session";
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
            Shutdown();
            try
            {
                config = requestedConfig?.Clone() ?? throw new ArgumentNullException(nameof(requestedConfig));
                _session = string.IsNullOrWhiteSpace(config.RuntimeRoot)
                    ? (IHumanVisionSession)new HumanVisionSession(config)
                    : new HumanVisionRuntimeSession(config);
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
