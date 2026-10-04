using UnityEngine;

namespace HumanVision.Demo
{
    // Normalized input is already upright/display-mirrored by the source contract.
    // These are image-plane display coordinates, never inferred metric depth.
    internal static class SkeletonImagePlane
    {
        internal const int JointCount = 32;
        private static readonly int[] Parents = { -1,0,1,2,3,4,5,6,7,8,8,3,11,12,13,14,15,15,0,18,19,20,0,22,23,24,3,26,27,28,27,30 };
        private static readonly int[] CompatibilityAnchors = { 17,18,19,20,22,5,7,9,-1,-1,-1,23,6,8,10,-1,-1,-1,11,13,15,-1,12,14,16,-1,21,0,1,3,2,4 };

        internal static int Parent(int joint) => Parents[joint];
        internal static int DisplaySlot(int bodyIndex, int regionIndex, bool useRegions) => useRegions ? regionIndex : bodyIndex;

        internal static Vector2 NormalizedToScreen(Vector2 normalized, Vector2 bottomLeft, Vector2 topLeft, Vector2 bottomRight)
            => bottomLeft + (bottomRight - bottomLeft) * normalized.x + (topLeft - bottomLeft) * (1 - normalized.y);

        internal static float ReferencePixelsToWorld(float referencePixels, float canvasScale, float unitsPerPixel)
            => referencePixels * canvasScale * unitsPerPixel;

        internal static bool TryPosition(HumanVisionBody body, int joint, bool canonical, int sourceWidth, int sourceHeight, out Vector2 normalized)
        {
            normalized = Vector2.zero;
            if (body == null || joint < 0 || joint >= JointCount) return false;
            if (canonical) {
                var position = body.CanonicalJoints[joint].Position;
                normalized = position.Normalized;
                return position.Valid && Finite(normalized);
            }
            // V1 has no canonical buffer. Preserve its established real COCO/derived
            // torso anchors without fabricating unavailable hand or foot landmarks.
            int hand = joint >= 8 && joint <= 10 ? joint - 8 : joint >= 15 && joint <= 17 ? joint - 12 : -1;
            if (hand >= 0) {
                var position = body.HandJoints[hand];
                normalized = position.Normalized;
                return position.Valid && Finite(normalized);
            }
            int anchor = CompatibilityAnchors[joint];
            return anchor >= 0 && TryNormalizedAnchor(body.Joints, anchor, out normalized);
        }

        // Same established V1 torso interpolation as Coco17Skeleton, in normalized
        // space so downsampled analysis pixels never become preview coordinates.
        private static bool TryNormalizedAnchor(HumanVisionJoint[] joints, int anchor, out Vector2 point)
        {
            point = Vector2.zero;
            if (anchor < HumanVisionJoint.Count) { point = joints[anchor].Normalized; return joints[anchor].Valid && Finite(point); }
            int first, second; float ratio = .5f;
            switch (anchor) {
                case 17: first = 11; second = 12; break;
                case 18: first = 17; second = 20; ratio = .35f; break;
                case 19: first = 17; second = 20; ratio = .75f; break;
                case 20: first = 5; second = 6; break;
                case 21: first = 20; second = 0; break;
                case 22: first = 19; second = 5; break;
                case 23: first = 19; second = 6; break;
                default: return false;
            }
            if (!TryNormalizedAnchor(joints, first, out var a) || !TryNormalizedAnchor(joints, second, out var b)) return false;
            point = Vector2.Lerp(a, b, ratio); return Finite(point);
        }

        private static bool Finite(Vector2 point) => !float.IsNaN(point.x) && !float.IsNaN(point.y) && !float.IsInfinity(point.x) && !float.IsInfinity(point.y);
    }
}
