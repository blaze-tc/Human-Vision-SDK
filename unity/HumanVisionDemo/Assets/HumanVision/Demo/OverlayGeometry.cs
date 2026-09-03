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
        internal static readonly Coco17Bone[] Bones =
        {
            new Coco17Bone(0, 1),
            new Coco17Bone(0, 2),
            new Coco17Bone(1, 3),
            new Coco17Bone(2, 4),
            new Coco17Bone(0, 5),
            new Coco17Bone(0, 6),
            new Coco17Bone(5, 6),
            new Coco17Bone(5, 7),
            new Coco17Bone(7, 9),
            new Coco17Bone(6, 8),
            new Coco17Bone(8, 10),
            new Coco17Bone(5, 11),
            new Coco17Bone(6, 12),
            new Coco17Bone(11, 12),
            new Coco17Bone(11, 13),
            new Coco17Bone(13, 15),
            new Coco17Bone(12, 14),
            new Coco17Bone(14, 16)
        };
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
