using UnityEngine;

namespace HumanVision
{
    public readonly struct HumanVisionJoint
    {
        public const int Count = 17;

        internal HumanVisionJoint(
            Vector2 pixel,
            Vector2 normalized,
            float confidence,
            bool valid, bool derived = false)
        {
            Pixel = pixel;
            Normalized = normalized;
            Confidence = confidence;
            Valid = valid;
            IsDerived = derived;
        }

        public Vector2 Pixel { get; }
        public Vector2 Normalized { get; }
        public float Confidence { get; }
        public bool Valid { get; }
        public bool IsDerived { get; }
    }

    public sealed class HumanVisionBody
    {
        internal HumanVisionBody()
        {
            Joints = new HumanVisionJoint[HumanVisionJoint.Count];
            HandJoints = new HumanVisionJoint[6];
        }

        public int TrackId { get; internal set; }
        public Rect BoundingBoxPixels { get; internal set; }
        public float DetectionConfidence { get; internal set; }
        public HumanVisionJoint[] Joints { get; }
        // Left Hand/Handtip/Thumb then right, same source frame as Joints.
        public HumanVisionJoint[] HandJoints { get; }
    }
}
