using UnityEngine;

namespace HumanVision
{
    // Example: read a region's real joints, then compare wrist height to shoulder height.
    public sealed class HumanVisionRaisedHandDetector : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        [Range(0, 7)] public int regionIndex;
        [Range(0, .3f)] public float heightMargin = .06f;
        [Range(0, 1)] public float minimumConfidence = .3f;
        [Tooltip("Ignore older poses for interaction even if the overlay still displays them.")]
        public float maximumPoseAgeMilliseconds = 1500;
        public bool LeftHandRaised { get; private set; }
        public bool RightHandRaised { get; private set; }
        public bool IsDetected => LeftHandRaised || RightHandRaised;
        public string Status { get; private set; } = "Waiting for skeleton";

        private void Update()
        {
            LeftHandRaised = RightHandRaised = false;
            if (manager == null || !manager.IsUserDetected(regionIndex)) { Status = "No skeleton in selected region"; return; }
            if (manager.GetComponent<Demo.VideoPlayerFrameSource>().ResultAgeMilliseconds > maximumPoseAgeMilliseconds) {
                Status = "Pose too old for gesture detection"; return;
            }
            LeftHandRaised = IsAbove(HumanVisionJointType.LeftWrist, HumanVisionJointType.LeftShoulder);
            RightHandRaised = IsAbove(HumanVisionJointType.RightWrist, HumanVisionJointType.RightShoulder);
            Status = LeftHandRaised && RightHandRaised ? "Both hands raised" :
                LeftHandRaised ? "Left hand raised" : RightHandRaised ? "Right hand raised" : "Hands not raised";
        }
        private bool IsAbove(HumanVisionJointType wristType, HumanVisionJointType shoulderType)
        {
            if (!manager.TryGetJointByRegionIndex(regionIndex, wristType, out var wrist) ||
                !manager.TryGetJointByRegionIndex(regionIndex, shoulderType, out var shoulder)) return false;
            // Normalized image coordinates start at the top: a smaller Y means higher.
            return wrist.Confidence >= minimumConfidence && shoulder.Confidence >= minimumConfidence &&
                wrist.Normalized.y < shoulder.Normalized.y - heightMargin;
        }
    }
}
