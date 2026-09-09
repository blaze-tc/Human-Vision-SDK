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

        private static readonly Vector2[] JointRing =
        {
            new Vector2(1f, 0f),
            new Vector2(0.7071068f, -0.7071068f),
            new Vector2(0f, -1f),
            new Vector2(-0.7071068f, -0.7071068f),
            new Vector2(-1f, 0f),
            new Vector2(-0.7071068f, 0.7071068f),
            new Vector2(0f, 1f),
            new Vector2(0.7071068f, 0.7071068f)
        };

        [SerializeField] private HumanVisionManager manager;
        [SerializeField] private VideoPlayerFrameSource frameSource;
        [SerializeField, Min(1f)] private float boxThickness = 3f;
        [SerializeField, Min(1f)] private float boneThickness = 4f;
        [SerializeField, Min(1f)] private float jointSize = 7f;

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
                manager.BodyCount <= 0 || manager.Bodies == null ||
                !frameSource.CanPresentResult(manager.SourceFrameId))
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
                if (!Coco17Skeleton.TryResolveAnchor(joints, bone.Start, out Vector2 start) ||
                    !Coco17Skeleton.TryResolveAnchor(joints, bone.End, out Vector2 end))
                {
                    continue;
                }

                AddLine(
                    vertexHelper,
                    ToOverlay(start, videoRect),
                    ToOverlay(end, videoRect),
                    boneThickness,
                    bodyColor);
            }

            for (int anchorIndex = 0; anchorIndex < Coco17Skeleton.AnchorCount; anchorIndex++)
            {
                if (!Coco17Skeleton.TryResolveAnchor(joints, anchorIndex, out Vector2 anchor))
                {
                    continue;
                }

                AddCircle(vertexHelper, ToOverlay(anchor, videoRect), jointSize, bodyColor);
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

        private static void AddCircle(
            VertexHelper vertexHelper,
            Vector2 center,
            float radius,
            Color32 circleColor)
        {
            int centerVertex = vertexHelper.currentVertCount;
            vertexHelper.AddVert(center, circleColor, Vector2.zero);
            for (int ringIndex = 0; ringIndex < JointRing.Length; ringIndex++)
            {
                vertexHelper.AddVert(center + JointRing[ringIndex] * radius, circleColor, Vector2.zero);
            }

            for (int ringIndex = 0; ringIndex < JointRing.Length; ringIndex++)
            {
                int current = centerVertex + 1 + ringIndex;
                int next = centerVertex + 1 + (ringIndex + 1) % JointRing.Length;
                vertexHelper.AddTriangle(centerVertex, current, next);
            }
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
                frameSource.PresentationFrameChanged -= OnPresentationFrameChanged;
                frameSource.PresentationFrameChanged += OnPresentationFrameChanged;
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
                frameSource.PresentationFrameChanged -= OnPresentationFrameChanged;
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

        private void OnPresentationFrameChanged()
        {
            if (manager == null || frameSource == null ||
                !frameSource.CanPresentResult(manager.SourceFrameId))
            {
                canvasRenderer.Clear();
                // Clear removes materials as well as geometry. Restore them in the
                // next UI rebuild, otherwise later valid skeleton meshes stay invisible.
                SetMaterialDirty();
            }
            SetVerticesDirty();
        }
    }
}
