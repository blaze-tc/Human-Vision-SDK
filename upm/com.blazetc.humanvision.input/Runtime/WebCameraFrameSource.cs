using System;
using System.Collections;
using UnityEngine;

namespace HumanVision.Input
{
    /// <summary>Asynchronous permission; actual output contract comes from WebCamTexture.</summary>
    public sealed class WebCameraFrameSource : UnityTextureFrameSource
    {
        private WebCamTexture cameraTexture;
        private Coroutine opening;
        private HumanVisionSourceSettings settings;
        private bool paused;
        private ulong callbackGeneration;

        public override void Open(HumanVisionSourceSettings value)
        {
            CheckThread();
            if (value == null) throw new ArgumentNullException(nameof(value));
            Close();
            settings = new HumanVisionSourceSettings
            {
                Kind = InputKind.WebCamera, DeviceName = value.DeviceName,
                RequestedWidth = value.RequestedWidth, RequestedHeight = value.RequestedHeight,
                RequestedFramesPerSecond = value.RequestedFramesPerSecond, DisplayMirror = value.DisplayMirror
            };
            DisplayMirror = settings.DisplayMirror;
            LastError = string.Empty;
            try
            {
                BeginOutput();
                callbackGeneration = ActiveGeneration;
                State = InputSourceState.Opening;
                opening = StartCoroutine(OpenCamera(callbackGeneration));
            }
            catch (Exception ex) { Fail(ex.Message); }
        }

        private IEnumerator OpenCamera(ulong generation)
        {
            if (!Application.HasUserAuthorization(UserAuthorization.WebCam))
                yield return Application.RequestUserAuthorization(UserAuthorization.WebCam);
            if (generation != ActiveGeneration || State != InputSourceState.Opening) yield break;
            if (!Application.HasUserAuthorization(UserAuthorization.WebCam))
            {
                Fail("Camera permission was denied. Enable camera access and reopen the source.");
                yield break;
            }
            try
            {
                cameraTexture = new WebCamTexture(settings.DeviceName, settings.RequestedWidth,
                    settings.RequestedHeight, settings.RequestedFramesPerSecond);
                cameraTexture.Play();
            }
            catch (Exception ex) { Fail(ex.Message); }
            opening = null;
        }

        private void Update()
        {
            PollRetirement();
            // Unity exposes a placeholder texture while opening; it is never an actual frame.
            if (paused || cameraTexture == null || !cameraTexture.didUpdateThisFrame ||
                cameraTexture.width <= 16 || cameraTexture.height <= 16) return;
            try
            {
                Publish(cameraTexture, cameraTexture.videoRotationAngle,
                    cameraTexture.videoVerticallyMirrored, -1, callbackGeneration);
                callbackGeneration = ActiveGeneration;
            }
            catch (Exception ex) { Fail(ex.Message); }
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
            if (paused || State == InputSourceState.Stopped || State == InputSourceState.Error) return;
            paused = true;
            if (opening != null) { StopCoroutine(opening); opening = null; }
            if (cameraTexture != null) cameraTexture.Pause();
            RetireOutput();
            State = PendingRetirementCount == 0 ? InputSourceState.Stopped : InputSourceState.Closing;
        }

        public void Resume()
        {
            CheckThread();
            if (!paused) return;
            if (cameraTexture == null) { paused = false; Open(settings); return; }
            try
            {
                BeginOutput();
                callbackGeneration = ActiveGeneration;
                paused = false;
                State = InputSourceState.Opening;
                cameraTexture.Play();
            }
            catch (Exception ex) { Fail(ex.Message); }
        }

        public override void Close()
        {
            CheckThread();
            State = InputSourceState.Closing;
            if (opening != null) { StopCoroutine(opening); opening = null; }
            if (cameraTexture != null)
            {
                var retiring = cameraTexture;
                cameraTexture = null;
                retiring.Stop();
                FrameTextureNormalizer.Destroy(retiring);
            }
            paused = false;
            RetireOutput();
            State = PendingRetirementCount == 0 ? InputSourceState.Stopped : InputSourceState.Closing;
        }

        private void OnApplicationPause(bool value) { if (value) Pause(); else Resume(); }
    }
}
