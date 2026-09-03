using UnityEngine;

namespace HumanVision.Demo
{
    internal readonly struct Coco17Bone
    {
        internal Coco17Bone(int start, int end)
        {
            Start = start;
            End = end;
        }

        internal int Start { get; }
        internal int End { get; }
    }

    internal static class Coco17Skeleton
    {
        private const int NeckAnchor = 17;
        private const int PelvisAnchor = 18;

        internal static readonly Coco17Bone[] Bones =
        {
            new Coco17Bone(0, 1),
            new Coco17Bone(0, 2),
            new Coco17Bone(1, 3),
            new Coco17Bone(2, 4),
            new Coco17Bone(0, NeckAnchor),
            new Coco17Bone(NeckAnchor, 5),
            new Coco17Bone(5, 7),
            new Coco17Bone(7, 9),
            new Coco17Bone(NeckAnchor, 6),
            new Coco17Bone(6, 8),
            new Coco17Bone(8, 10),
            new Coco17Bone(NeckAnchor, PelvisAnchor),
            new Coco17Bone(PelvisAnchor, 11),
            new Coco17Bone(11, 13),
            new Coco17Bone(13, 15),
            new Coco17Bone(PelvisAnchor, 12),
            new Coco17Bone(12, 14),
            new Coco17Bone(14, 16)
        };

        internal static bool TryResolveAnchor(
            HumanVisionJoint[] joints,
            int anchor,
            out Vector2 pixel)
        {
            pixel = Vector2.zero;
            if (joints == null || joints.Length < HumanVisionJoint.Count)
            {
                return false;
            }

            if (anchor >= 0 && anchor < HumanVisionJoint.Count)
            {
                HumanVisionJoint joint = joints[anchor];
                if (!joint.Valid)
                {
                    return false;
                }

                pixel = joint.Pixel;
                return true;
            }

            if (anchor == NeckAnchor)
            {
                return TryResolveMidpoint(joints[5], joints[6], out pixel);
            }

            if (anchor == PelvisAnchor)
            {
                return TryResolveMidpoint(joints[11], joints[12], out pixel);
            }

            return false;
        }

        private static bool TryResolveMidpoint(
            HumanVisionJoint first,
            HumanVisionJoint second,
            out Vector2 pixel)
        {
            pixel = Vector2.zero;
            if (!first.Valid || !second.Valid)
            {
                return false;
            }

            pixel = (first.Pixel + second.Pixel) * 0.5f;
            return true;
        }
    }

    internal static class OverlayGeometry
    {
        internal static Rect CalculateAspectFitRect(Rect container, int sourceWidth, int sourceHeight)
        {
            if (sourceWidth <= 0 || sourceHeight <= 0 || container.width <= 0f || container.height <= 0f)
            {
                return new Rect(container.center, Vector2.zero);
            }

            float scale = Mathf.Min(container.width / sourceWidth, container.height / sourceHeight);
            Vector2 size = new Vector2(sourceWidth * scale, sourceHeight * scale);
            return new Rect(container.center - size * 0.5f, size);
        }

        internal static Vector2 SourceToOverlay(
            Vector2 sourcePoint,
            Rect fittedVideoRect,
            int sourceWidth,
            int sourceHeight)
        {
            if (sourceWidth <= 0 || sourceHeight <= 0)
            {
                return fittedVideoRect.center;
            }

            float x = fittedVideoRect.xMin + sourcePoint.x / sourceWidth * fittedVideoRect.width;
            float y = fittedVideoRect.yMax - sourcePoint.y / sourceHeight * fittedVideoRect.height;
            return new Vector2(x, y);
        }
    }
}
