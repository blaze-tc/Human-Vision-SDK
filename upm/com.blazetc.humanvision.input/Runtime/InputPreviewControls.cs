using System;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Input
{
    public sealed class InputPreviewControls : MonoBehaviour
    {
        public RawImage Preview;
        public Text Status;
        public bool CompactStatus;
        public IHumanVisionFrameSource Source { get; private set; }
        public HumanVisionSourceSettings Request { get; private set; }
        public string LastError { get; private set; } = "Choose a source and press Start.";
        public event Action SourceClosing;
        public event Action<IHumanVisionFrameSource> SourceOpened;
        private float nextStatus;
        private FramePreview framePreview;
        private AspectRatioFitter fitter;
        public void Open(HumanVisionSourceSettings settings)
        {
            Close(); Request = settings;
            try {
                switch (settings.Kind) {
                    case InputKind.Video: Source = GetOrAdd<VideoFrameSource>(); break;
                    case InputKind.WebCamera: Source = GetOrAdd<WebCameraFrameSource>(); break;
                    case InputKind.Rtsp: Source = GetOrAdd<RtspFrameSource>(); break;
                    default: throw new ArgumentException("Unknown input source.");
                }
                if (Preview != null) {
                    framePreview = Preview.GetComponent<FramePreview>() ?? Preview.gameObject.AddComponent<FramePreview>();
                    fitter = Preview.GetComponent<AspectRatioFitter>(); framePreview.Bind(Source);
                }
                Source.Open(settings); LastError = ""; SourceOpened?.Invoke(Source);
            } catch (Exception error) { LastError = error.Message; }
            nextStatus = 0;
        }
        private T GetOrAdd<T>() where T : Component => GetComponent<T>() ?? gameObject.AddComponent<T>();
        public void Close()
        {
            SourceClosing?.Invoke();
            if (framePreview != null) framePreview.Bind(null);
            if (Source != null) Source.Close();
            Source = null;
            if (Preview != null) Preview.texture = null;
        }
        private void Update()
        {
            if (Source?.CurrentTexture != null && fitter != null)
                fitter.aspectRatio = (float)Source.CurrentTexture.width / Source.CurrentTexture.height;
            if (Status == null || Time.unscaledTime < nextStatus) return;
            nextStatus = Time.unscaledTime + .25f;
            string actual = Source?.CurrentTexture == null ? "waiting for frame" : Source.CurrentTexture.width + "x" + Source.CurrentTexture.height;
            string requested = Request == null ? "" : "Request " + Request.RequestedWidth + "x" + Request.RequestedHeight + " @ " + Request.RequestedFramesPerSecond + " FPS; actual " + actual;
            string error = !string.IsNullOrEmpty(Source?.LastError) ? Source.LastError : LastError;
            Status.text = CompactStatus
                ? (Source == null ? "Stopped" : Source.State.ToString()) + " | " + actual +
                    (string.IsNullOrEmpty(error) ? "" : " | See Settings for details")
                : (Source == null ? "Stopped" : Source.State.ToString()) + "\n" + requested + "\n" + error;
        }
        private void OnDisable() { Close(); }
    }
}
