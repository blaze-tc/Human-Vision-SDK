using System.IO;
using UnityEngine;

namespace HumanVision.Demo
{
    [DefaultExecutionOrder(-75)]
    [DisallowMultipleComponent]
    public sealed class HumanVisionDemoBootstrap : MonoBehaviour
    {
        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private VideoPlayerFrameSource frameSource;

        [Header("StreamingAssets paths")]
        [SerializeField] private string detectorModel = "HumanVision/Models/rtmdet_tiny_640.onnx";
        [SerializeField] private string poseModel = "HumanVision/Models/rtmpose_s_256x192.onnx";
        [SerializeField] private string startupVideo = "HumanVision/Media/d0_3_two_people.mp4";

        [Header("Demo defaults")]
        [SerializeField, Min(1)] private int maxBodies = 4;
        [SerializeField, Range(0f, 1f)] private float detectionThreshold = 0.35f;
        [SerializeField, Range(0f, 1f)] private float poseThreshold = 0.30f;
        [SerializeField, Min(1)] private int detectionInterval = 1;

        public void Configure(
            HumanVisionManager visionManager,
            VideoPlayerFrameSource videoFrameSource)
        {
            manager = visionManager;
            frameSource = videoFrameSource;
        }

        private void Start()
        {
            if (manager == null || frameSource == null)
            {
                Debug.LogError("HumanVision Demo bootstrap references are not configured.", this);
                return;
            }

            var config = new HumanVisionConfig
            {
                MaxBodies = maxBodies,
                DetectionThreshold = detectionThreshold,
                PoseThreshold = poseThreshold,
                DetectionInterval = detectionInterval,
                EnableTracking = true,
                DetectorModelPath = Path.Combine(Application.streamingAssetsPath, detectorModel),
                PoseModelPath = Path.Combine(Application.streamingAssetsPath, poseModel)
            };

            if (!manager.TryInitialize(config))
            {
                return;
            }

            frameSource.PlayRelativeVideo(startupVideo);
        }
    }
}
