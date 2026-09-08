using System.IO;
using System.Text;
using UnityEngine;

namespace HumanVision.Demo
{
    [DefaultExecutionOrder(100)]
    [DisallowMultipleComponent]
    public sealed class HumanVisionHud : MonoBehaviour
    {
        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private VideoPlayerFrameSource frameSource;
        [SerializeField] private string onePersonVideo = "HumanVision/Media/d0_3_one_person.mp4";
        [SerializeField] private string multiPersonVideo = "HumanVision/Media/d0_3_two_people.mp4";

        private readonly StringBuilder _builder = new StringBuilder(768);
        private string _statusText = "HumanVision Demo starting...";
        private float _nextRefresh;

        public void Configure(
            HumanVisionManager visionManager,
            VideoPlayerFrameSource videoFrameSource,
            string onePersonRelativePath,
            string multiPersonRelativePath)
        {
            manager = visionManager;
            frameSource = videoFrameSource;
            onePersonVideo = onePersonRelativePath;
            multiPersonVideo = multiPersonRelativePath;
        }

        private void Update()
        {
            if (Time.unscaledTime < _nextRefresh)
            {
                return;
            }

            _nextRefresh = Time.unscaledTime + 0.25f;
            RefreshText();
        }

        private void OnGUI()
        {
            const float panelWidth = 430f;
            const float panelHeight = 285f;
            GUI.Box(new Rect(12f, 12f, panelWidth, panelHeight), "HumanVisionSDK D0.4");
            GUI.Label(new Rect(26f, 40f, panelWidth - 28f, 185f), _statusText);

            if (GUI.Button(new Rect(26f, 230f, 34f, 26f), "-"))
            {
                manager?.TrySetMaxBodies(Mathf.Max(1, manager.MaxBodies - 1));
            }

            if (GUI.Button(new Rect(66f, 230f, 34f, 26f), "+"))
            {
                manager?.TrySetMaxBodies(manager.MaxBodies + 1);
            }

            if (GUI.Button(new Rect(112f, 230f, 86f, 26f), "1 Person"))
            {
                frameSource?.PlayRelativeVideo(onePersonVideo);
            }

            if (GUI.Button(new Rect(204f, 230f, 86f, 26f), "2 People"))
            {
                frameSource?.PlayRelativeVideo(multiPersonVideo);
            }

            if (GUI.Button(new Rect(296f, 230f, 120f, 26f), "Camera image"))
            {
                if (manager != null && manager.TrySetMaxBodies(8))
                    frameSource?.PlayRelativeVideo("HumanVision/Media/cameraImage.png");
            }

            GUI.Label(new Rect(26f, 262f, panelWidth - 28f, 22f),
                "MaxBodies changes at runtime; native DLL is not rebuilt.");
        }

        private void RefreshText()
        {
            _builder.Length = 0;
            if (manager == null || frameSource == null)
            {
                _builder.Append("Scene references are not configured.");
                _statusText = _builder.ToString();
                return;
            }

            HumanVisionStats stats = manager.Stats;
            float renderFps = Time.unscaledDeltaTime > 0f ? 1f / Time.unscaledDeltaTime : 0f;
            _builder.Append(frameSource.IsStillImage ? "Image (single inference): " : "Video: ");
            _builder.Append(string.IsNullOrWhiteSpace(frameSource.CurrentVideoPath)
                ? "not selected"
                : Path.GetFileName(frameSource.CurrentVideoPath));
            _builder.Append('\n');
            _builder.Append("Source: ").Append(frameSource.SourceWidth).Append('x').Append(frameSource.SourceHeight)
                .Append(" @ ").Append(frameSource.VideoFrameRate.ToString("F1")).Append(" fps\n");
            _builder.Append("Render: ").Append(renderFps.ToString("F1")).Append(" fps   Inference: ")
                .Append(stats.InferenceFps.ToString("F1")).Append(" fps\n");
            long resultAge = frameSource.LatestSubmittedFrameId >= manager.SourceFrameId && manager.SourceFrameId >= 0
                ? frameSource.LatestSubmittedFrameId - manager.SourceFrameId
                : -1;
            long videoDelay = frameSource.LatestSubmittedFrameId >= frameSource.PresentationFrameId &&
                frameSource.PresentationFrameId >= 0
                ? frameSource.LatestSubmittedFrameId - frameSource.PresentationFrameId
                : -1;
            _builder.Append("Video delay: ").Append(videoDelay)
                .Append(" frames   Result age: ").Append(resultAge).Append(" frames\n");
            _builder.Append("Bodies: ").Append(manager.BodyCount).Append(" / MaxBodies: ")
                .Append(manager.MaxBodies).Append("   Sequence: ").Append(manager.ResultSequence).Append('\n');
            _builder.Append("Detector: ").Append(stats.DetectionMs.ToString("F1")).Append(" ms   Pose: ")
                .Append(stats.PoseMs.ToString("F1")).Append(" ms\n");
            _builder.Append("Tracker: ").Append(stats.TrackingMs.ToString("F2")).Append(" ms   Total: ")
                .Append(stats.TotalMs.ToString("F1")).Append(" ms\n");
            _builder.Append("Submitted/Processed/Dropped: ").Append(stats.SubmittedFrames).Append(" / ")
                .Append(stats.ProcessedFrames).Append(" / ").Append(stats.DroppedFrames).Append('\n');
            _builder.Append("GPU readback pool drops/errors: ").Append(frameSource.ReadbackDrops).Append(" / ")
                .Append(frameSource.ReadbackErrors);

            string error = !string.IsNullOrWhiteSpace(manager.LastError)
                ? manager.LastError
                : frameSource.LastError;
            if (!string.IsNullOrWhiteSpace(error))
            {
                _builder.Append("\nError: ").Append(error);
            }

            _statusText = _builder.ToString();
        }
    }
}
