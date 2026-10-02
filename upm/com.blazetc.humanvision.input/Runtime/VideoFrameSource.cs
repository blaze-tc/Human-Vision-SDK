using System;
using UnityEngine;
using UnityEngine.Video;

namespace HumanVision.Input
{
    /// <summary>Managed-source publication and bounded source-copy lifetime; no recognition dependency.</summary>
    public abstract class UnityTextureFrameSource : MonoBehaviour, IHumanVisionFrameSource
    {
        private SourceRetirement retirement;
        private SourceGeneration timeline;
        private InputRetirementRegistration registration;
        private FrameTextureNormalizer normalizer;
        private ulong token;
        /// <summary>Retiring output owners retained for external consumer copies; excludes active output.</summary>
        public int PendingRetirementCount => retirement.PendingResourceCount - (token != 0 ? 1 : 0);
        private long sequence;
        private int width, height, rotation;
        private FrameColorSpace outputEncoding;
        private bool vertical, mirror;

        public InputSourceState State { get; protected set; } = InputSourceState.Stopped;
        public string LastError { get; protected set; } = string.Empty;
        public Texture CurrentTexture => timeline == null ? null : timeline.CurrentTexture;
        protected bool DisplayMirror;
        protected ulong ActiveGeneration => timeline.Generation;

        protected virtual void Awake()
        {
            retirement = new SourceRetirement();
            timeline = new SourceGeneration(retirement);
            registration = new InputRetirementRegistration(retirement, OnRetirementProgress);
        }

        protected void CheckThread() { retirement.CheckThread(); }

        protected void BeginOutput()
        {
            CheckThread();
            retirement.Poll();
            // Bounded backpressure: never allocate an unregistered output while old copies remain.
            if (retirement.PendingResourceCount >= 15)
                throw new InvalidOperationException("Input texture retirement capacity exhausted; complete outstanding source-copy fences before reopening.");
            RetireOutput();
            timeline.BeginGeneration();
        }

        protected void RetireOutput()
        {
            if (timeline == null) return;
            CheckThread();
            timeline.Close();
            if (normalizer != null)
            {
                // Registered normalizer owns material/output until every external source copy completes.
                // Unity handles its own queued blit resources; no extra application marker is required.
                if (token == 0) normalizer.Dispose();
                normalizer = null;
            }
            token = 0;
            retirement.Poll();
            if (retirement.PendingResourceCount > 0) InputRetirementPump.Watch(registration);
        }

        protected void Publish(Texture input, int degrees, bool verticalMirror, long pts, ulong generation)
        {
            CheckThread();
            if (generation != timeline.Generation || State == InputSourceState.Stopped ||
                State == InputSourceState.Closing || State == InputSourceState.Error) return;
            var observed = InputMonotonicClock.NowUs;
            int w = degrees % 180 == 0 ? input.width : input.height;
            int h = degrees % 180 == 0 ? input.height : input.width;
            if (normalizer != null && (w != width || h != height || degrees != rotation ||
                verticalMirror != vertical || DisplayMirror != mirror || outputEncoding != FrameTextureNormalizer.EncodingFor(input)))
            {
                BeginOutput();
                generation = timeline.Generation;
            }
            if (normalizer == null)
            {
                normalizer = new FrameTextureNormalizer();
                width = w;
                height = h;
                rotation = degrees;
                vertical = verticalMirror;
                mirror = DisplayMirror;
                outputEncoding = FrameTextureNormalizer.EncodingFor(input);
            }
            var texture = normalizer.Prepare(input, degrees);
            if (token == 0)
            {
                token = retirement.Register(generation, texture, normalizer.Dispose);
            }
            var frame = new HumanVisionTextureFrame(timeline.SourceId, generation, ++sequence,
                texture, InputMonotonicClock.NowUs, pts, degrees, DisplayMirror,
                FrameRowOrigin.UnityBottomLeft, outputEncoding, FrameTimestampKind.UnityObserved,
                token, observed, FrameClockDomain.InputMonotonic, 0);
            normalizer.Update(input, degrees, verticalMirror, DisplayMirror);
            frame.PublishedTimestampUs = InputMonotonicClock.NowUs;
            if (timeline.TryPublish(in frame)) State = InputSourceState.Streaming;
        }

        private void OnRetirementProgress()
        {
            if (State == InputSourceState.Closing && PendingRetirementCount == 0) State = InputSourceState.Stopped;
        }

        protected void PollRetirement()
        {
            retirement.Poll();
            OnRetirementProgress();
        }

        public bool TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame)
        {
            frame = default;
            return timeline != null && timeline.TryGetLatestFrame(afterFrameId, out frame);
        }

        public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease)
        {
            lease = default;
            return timeline != null && timeline.TryAcquireSourceCopyLease(in frame, out lease);
        }

        public abstract void Open(HumanVisionSourceSettings settings);
        public abstract void Close();
        protected virtual void OnDisable() { Close(); }
        protected virtual void OnDestroy() { Close(); }
    }

    /// <summary>Actual VideoPlayer decode output, independent of consumer speed and recognition state.</summary>
    public sealed class VideoFrameSource : UnityTextureFrameSource
    {
        private VideoPlayer player;
        private VideoPlayer.FrameReadyEventHandler ready;
        private VideoPlayer.ErrorEventHandler error;
        private bool paused;

        public override void Open(HumanVisionSourceSettings settings)
        {
            CheckThread();
            if (settings == null) throw new ArgumentNullException(nameof(settings));
            Close();
            LastError = string.Empty;
            DisplayMirror = settings.DisplayMirror;
            try
            {
                BeginOutput();
                State = InputSourceState.Opening;
                var owner = new GameObject("HumanVision video decoder") { hideFlags = HideFlags.HideAndDontSave };
                DontDestroyOnLoad(owner);
                player = owner.AddComponent<VideoPlayer>();
                player.playOnAwake = false;
                player.source = VideoSource.Url;
                player.url = settings.Location;
                player.renderMode = VideoRenderMode.APIOnly;
                player.audioOutputMode = VideoAudioOutputMode.None;
                player.isLooping = true;
                player.sendFrameReadyEvents = true;
                Attach();
                player.Play();
            }
            catch (Exception ex) { Fail(ex.Message); }
        }

        private void Attach()
        {
            var generation = ActiveGeneration;
            ready = (sender, index) =>
            {
                if (!ReferenceEquals(player, sender) || player == null || paused ||
                    (State != InputSourceState.Opening && State != InputSourceState.Streaming) ||
                    generation != ActiveGeneration || sender.texture == null) return;
                try
                {
                    Publish(sender.texture, 0, false, sender.time >= 0 ? (long)(sender.time * 1000000) : -1, generation);
                    // A decoded geometry change starts a new generation and replaces callback ownership.
                    if (generation != ActiveGeneration) { Detach(); Attach(); }
                }
                catch (Exception ex) { Fail(ex.Message); }
            };
            error = (sender, message) =>
            {
                if (ReferenceEquals(player, sender) && player != null && !paused &&
                    (State == InputSourceState.Opening || State == InputSourceState.Streaming) &&
                    generation == ActiveGeneration) Fail(message);
            };
            player.frameReady += ready;
            player.errorReceived += error;
        }

        private void Detach()
        {
            if (player == null) return;
            player.frameReady -= ready;
            player.errorReceived -= error;
            ready = null;
            error = null;
        }

        private void Fail(string message)
        {
            LastError = message;
            Close();
            State = InputSourceState.Error;
        }

        public void Pause()
        {
            CheckThread();
            if (player == null || paused) return;
            paused = true;
            Detach();
            player.Pause();
            RetireOutput();
            State = PendingRetirementCount == 0 ? InputSourceState.Stopped : InputSourceState.Closing;
        }

        public void Resume()
        {
            CheckThread();
            if (player == null || !paused) return;
            try
            {
                BeginOutput();
                paused = false;
                State = InputSourceState.Opening;
                Attach();
                player.Play();
            }
            catch (Exception ex) { Fail(ex.Message); }
        }

        public override void Close()
        {
            CheckThread();
            State = InputSourceState.Closing;
            Detach();
            if (player != null)
            {
                var retiring = player;
                player = null;
                retiring.Stop();
                FrameTextureNormalizer.Destroy(retiring.gameObject);
            }
            paused = false;
            RetireOutput();
            State = PendingRetirementCount == 0 ? InputSourceState.Stopped : InputSourceState.Closing;
        }

        private void Update() { PollRetirement(); }
        private void OnApplicationPause(bool value) { if (value) Pause(); else Resume(); }
    }
}
