using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    [DisallowMultipleComponent]
    public sealed class HumanVisionOverlay : MaskableGraphic
    {
        private static readonly Color32[] BodyColors =
        {
            new Color32(0, 229, 255, 255),
            new Color32(255, 196, 0, 255),
            new Color32(255, 86, 153, 255),
            new Color32(110, 255, 129, 255),
            new Color32(182, 128, 255, 255),
            new Color32(255, 128, 64, 255),
            new Color32(64, 160, 255, 255),
            new Color32(230, 255, 96, 255)
        };

        private static readonly int[] DigitMasks =
        {
            0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
        };

        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private VideoPlayerFrameSource frameSource;
        [SerializeField, Min(1f)] private float boxThickness = 3f;
        [SerializeField, Min(1f)] private float boneThickness = 2f;
        [SerializeField, Min(1f)] private float jointSize = 4f;

        private readonly int[] _digits = new int[12];

        public override Texture mainTexture => Texture2D.whiteTexture;

        protected override void OnEnable()
        {
            base.OnEnable();
            Subscribe();
            raycastTarget = false;
        }

        protected override void OnDisable()
        {
            Unsubscribe();
            base.OnDisable();
        }

        protected override void OnRectTransformDimensionsChange()
        {
            base.OnRectTransformDimensionsChange();
            SetVerticesDirty();
        }

        public void Configure(HumanVisionManager visionManager, VideoPlayerFrameSource videoFrameSource)
        {
            Unsubscribe();
            manager = visionManager;
            frameSource = videoFrameSource;
            Subscribe();
            SetVerticesDirty();
        }

        protected override void OnPopulateMesh(VertexHelper vertexHelper)
        {
            vertexHelper.Clear();
            if (manager == null || frameSource == null ||
                frameSource.SourceWidth <= 0 || frameSource.SourceHeight <= 0 ||
                manager.BodyCount <= 0 || manager.Bodies == null)
            {
                return;
            }

            Rect videoRect = OverlayGeometry.CalculateAspectFitRect(
                rectTransform.rect,
                frameSource.SourceWidth,
                frameSource.SourceHeight);

            int bodyCount = Mathf.Min(manager.BodyCount, manager.Bodies.Length);
            for (int bodyIndex = 0; bodyIndex < bodyCount; bodyIndex++)
            {
                HumanVisionBody body = manager.Bodies[bodyIndex];
                Color32 bodyColor = BodyColors[bodyIndex % BodyColors.Length];
                DrawBody(vertexHelper, body, videoRect, bodyColor);
            }
        }

        private void DrawBody(VertexHelper vertexHelper, HumanVisionBody body, Rect videoRect, Color32 bodyColor)
        {
            Rect sourceBox = body.BoundingBoxPixels;
            Vector2 topLeft = ToOverlay(new Vector2(sourceBox.xMin, sourceBox.yMin), videoRect);
            Vector2 bottomRight = ToOverlay(new Vector2(sourceBox.xMax, sourceBox.yMax), videoRect);
            DrawRectangle(vertexHelper, topLeft, bottomRight, boxThickness, bodyColor);

            HumanVisionJoint[] joints = body.Joints;
            for (int boneIndex = 0; boneIndex < Coco17Skeleton.Bones.Length; boneIndex++)
            {
                Coco17Bone bone = Coco17Skeleton.Bones[boneIndex];
                HumanVisionJoint start = joints[bone.Start];
                HumanVisionJoint end = joints[bone.End];
                if (!start.Valid || !end.Valid)
                {
                    continue;
                }

                AddLine(
                    vertexHelper,
                    ToOverlay(start.Pixel, videoRect),
                    ToOverlay(end.Pixel, videoRect),
                    boneThickness,
                    bodyColor);
            }

            for (int jointIndex = 0; jointIndex < joints.Length; jointIndex++)
            {
                if (!joints[jointIndex].Valid)
                {
                    continue;
                }

                Vector2 center = ToOverlay(joints[jointIndex].Pixel, videoRect);
                AddQuad(
                    vertexHelper,
                    center + new Vector2(-jointSize, -jointSize),
                    center + new Vector2(jointSize, jointSize),
                    bodyColor);
            }

            if (body.TrackId >= 0)
            {
                DrawNumber(vertexHelper, body.TrackId, topLeft + new Vector2(6f, -6f), bodyColor);
            }
        }

        private Vector2 ToOverlay(Vector2 sourcePoint, Rect videoRect)
        {
            return OverlayGeometry.SourceToOverlay(
                sourcePoint,
                videoRect,
                frameSource.SourceWidth,
                frameSource.SourceHeight);
        }

        private void DrawRectangle(
            VertexHelper vertexHelper,
            Vector2 topLeft,
            Vector2 bottomRight,
            float thickness,
            Color32 lineColor)
        {
            Vector2 topRight = new Vector2(bottomRight.x, topLeft.y);
            Vector2 bottomLeft = new Vector2(topLeft.x, bottomRight.y);
            AddLine(vertexHelper, topLeft, topRight, thickness, lineColor);
            AddLine(vertexHelper, topRight, bottomRight, thickness, lineColor);
            AddLine(vertexHelper, bottomRight, bottomLeft, thickness, lineColor);
            AddLine(vertexHelper, bottomLeft, topLeft, thickness, lineColor);
        }

        private void DrawNumber(VertexHelper vertexHelper, int value, Vector2 origin, Color32 numberColor)
        {
            int digitCount = 0;
            do
            {
                _digits[digitCount++] = value % 10;
                value /= 10;
            }
            while (value > 0 && digitCount < _digits.Length);

            const float digitWidth = 10f;
            const float digitHeight = 18f;
            const float spacing = 4f;
            for (int outputIndex = 0; outputIndex < digitCount; outputIndex++)
            {
                int digit = _digits[digitCount - outputIndex - 1];
                DrawDigit(
                    vertexHelper,
                    digit,
                    origin + new Vector2(outputIndex * (digitWidth + spacing), 0f),
                    digitWidth,
                    digitHeight,
                    numberColor);
            }
        }

        private static void DrawDigit(
            VertexHelper vertexHelper,
            int digit,
            Vector2 origin,
            float width,
            float height,
            Color32 digitColor)
        {
            int mask = DigitMasks[digit];
            float middleY = origin.y - height * 0.5f;
            float bottomY = origin.y - height;
            const float thickness = 2f;

            if ((mask & (1 << 0)) != 0) AddLine(vertexHelper, origin, origin + Vector2.right * width, thickness, digitColor);
            if ((mask & (1 << 1)) != 0) AddLine(vertexHelper, origin + Vector2.right * width, new Vector2(origin.x + width, middleY), thickness, digitColor);
            if ((mask & (1 << 2)) != 0) AddLine(vertexHelper, new Vector2(origin.x + width, middleY), new Vector2(origin.x + width, bottomY), thickness, digitColor);
            if ((mask & (1 << 3)) != 0) AddLine(vertexHelper, new Vector2(origin.x, bottomY), new Vector2(origin.x + width, bottomY), thickness, digitColor);
            if ((mask & (1 << 4)) != 0) AddLine(vertexHelper, new Vector2(origin.x, middleY), new Vector2(origin.x, bottomY), thickness, digitColor);
            if ((mask & (1 << 5)) != 0) AddLine(vertexHelper, origin, new Vector2(origin.x, middleY), thickness, digitColor);
            if ((mask & (1 << 6)) != 0) AddLine(vertexHelper, new Vector2(origin.x, middleY), new Vector2(origin.x + width, middleY), thickness, digitColor);
        }

        private static void AddLine(
            VertexHelper vertexHelper,
            Vector2 start,
            Vector2 end,
            float thickness,
            Color32 lineColor)
        {
            Vector2 direction = end - start;
            if (direction.sqrMagnitude < 0.0001f)
            {
                return;
            }

            Vector2 normal = new Vector2(-direction.y, direction.x).normalized * (thickness * 0.5f);
            int firstVertex = vertexHelper.currentVertCount;
            vertexHelper.AddVert(start - normal, lineColor, Vector2.zero);
            vertexHelper.AddVert(start + normal, lineColor, Vector2.zero);
            vertexHelper.AddVert(end + normal, lineColor, Vector2.zero);
            vertexHelper.AddVert(end - normal, lineColor, Vector2.zero);
            vertexHelper.AddTriangle(firstVertex, firstVertex + 1, firstVertex + 2);
            vertexHelper.AddTriangle(firstVertex, firstVertex + 2, firstVertex + 3);
        }

        private static void AddQuad(
            VertexHelper vertexHelper,
            Vector2 minimum,
            Vector2 maximum,
            Color32 quadColor)
        {
            int firstVertex = vertexHelper.currentVertCount;
            vertexHelper.AddVert(new Vector2(minimum.x, minimum.y), quadColor, Vector2.zero);
            vertexHelper.AddVert(new Vector2(minimum.x, maximum.y), quadColor, Vector2.zero);
            vertexHelper.AddVert(new Vector2(maximum.x, maximum.y), quadColor, Vector2.zero);
            vertexHelper.AddVert(new Vector2(maximum.x, minimum.y), quadColor, Vector2.zero);
            vertexHelper.AddTriangle(firstVertex, firstVertex + 1, firstVertex + 2);
            vertexHelper.AddTriangle(firstVertex, firstVertex + 2, firstVertex + 3);
        }

        private void Subscribe()
        {
            if (!isActiveAndEnabled)
            {
                return;
            }

            if (manager != null)
            {
                manager.ResultUpdated -= OnResultUpdated;
                manager.ResultUpdated += OnResultUpdated;
            }

            if (frameSource != null)
            {
                frameSource.VideoLayoutChanged -= OnVideoLayoutChanged;
                frameSource.VideoLayoutChanged += OnVideoLayoutChanged;
            }
        }

        private void Unsubscribe()
        {
            if (manager != null)
            {
                manager.ResultUpdated -= OnResultUpdated;
            }

            if (frameSource != null)
            {
                frameSource.VideoLayoutChanged -= OnVideoLayoutChanged;
            }
        }

        private void OnResultUpdated(long sequence)
        {
            SetVerticesDirty();
        }

        private void OnVideoLayoutChanged()
        {
            SetVerticesDirty();
        }
    }
}
