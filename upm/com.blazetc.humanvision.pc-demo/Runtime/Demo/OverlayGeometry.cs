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
        private const int PelvisAnchor = 17;
        private const int SpineNavelAnchor = 18;
        private const int SpineChestAnchor = 19;
        private const int NeckAnchor = 20;
        private const int HeadAnchor = 21;
        private const int ClavicleLeftAnchor = 22;
        private const int ClavicleRightAnchor = 23;

        internal const int AnchorCount = 24;

        internal static readonly Coco17Bone[] Bones =
        {
            new Coco17Bone(PelvisAnchor, SpineNavelAnchor),
            new Coco17Bone(SpineNavelAnchor, SpineChestAnchor),
            new Coco17Bone(SpineChestAnchor, NeckAnchor),
            new Coco17Bone(NeckAnchor, HeadAnchor),
            new Coco17Bone(HeadAnchor, 0),
            new Coco17Bone(0, 1),
            new Coco17Bone(1, 3),
            new Coco17Bone(0, 2),
            new Coco17Bone(2, 4),
            new Coco17Bone(SpineChestAnchor, ClavicleLeftAnchor),
            new Coco17Bone(ClavicleLeftAnchor, 5),
            new Coco17Bone(5, 7),
            new Coco17Bone(7, 9),
            new Coco17Bone(SpineChestAnchor, ClavicleRightAnchor),
            new Coco17Bone(ClavicleRightAnchor, 6),
            new Coco17Bone(6, 8),
            new Coco17Bone(8, 10),
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

            if (anchor == PelvisAnchor)
            {
                return TryResolveMidpoint(joints[11], joints[12], out pixel);
            }

            if (anchor == NeckAnchor)
            {
                return TryResolveMidpoint(joints[5], joints[6], out pixel);
            }

            if (anchor == SpineNavelAnchor)
            {
                return TryResolveInterpolatedAnchor(
                    joints,
                    PelvisAnchor,
                    NeckAnchor,
                    0.35f,
                    out pixel);
            }

            if (anchor == SpineChestAnchor)
            {
                return TryResolveInterpolatedAnchor(
                    joints,
                    PelvisAnchor,
                    NeckAnchor,
                    0.75f,
                    out pixel);
            }

            if (anchor == HeadAnchor)
            {
                return TryResolveInterpolatedAnchor(joints, NeckAnchor, 0, 0.5f, out pixel);
            }

            if (anchor == ClavicleLeftAnchor)
            {
                return TryResolveInterpolatedAnchor(
                    joints,
                    SpineChestAnchor,
                    5,
                    0.5f,
                    out pixel);
            }

            if (anchor == ClavicleRightAnchor)
            {
                return TryResolveInterpolatedAnchor(
                    joints,
                    SpineChestAnchor,
                    6,
                    0.5f,
                    out pixel);
            }

            return false;
        }

        private static bool TryResolveInterpolatedAnchor(
            HumanVisionJoint[] joints,
            int firstAnchor,
            int secondAnchor,
            float interpolation,
            out Vector2 pixel)
        {
            pixel = Vector2.zero;
            if (!TryResolveAnchor(joints, firstAnchor, out Vector2 first) ||
                !TryResolveAnchor(joints, secondAnchor, out Vector2 second))
            {
                return false;
            }

            pixel = Vector2.Lerp(first, second, interpolation);
            return true;
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
