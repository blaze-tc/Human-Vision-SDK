using UnityEngine;
using UnityEngine.UI;
namespace HumanVision
{
    // One CanvasRenderer mesh for all people. Positions share the preview's
    // rect; camera orientation has already been applied to both input and image.
    public sealed class HumanVisionSkeletonGraphic : MaskableGraphic
    {
        internal HumanVisionSkeletonOverlayer Owner;
        private static readonly int[] Parents = { -1,0,1,2,3,4,5,6,7,8,8,3,11,12,13,14,15,15,0,18,19,20,0,22,23,24,3,26,27,28,27,30 };
        private readonly Vector2[] _points = new Vector2[32];
        private readonly bool[] _valid = new bool[32];
        protected override void OnPopulateMesh(VertexHelper mesh)
        {
            mesh.Clear();
            if (Owner == null || !Owner.drawSkeleton || Owner.manager == null) return;
            Rect rect = rectTransform.rect;
            float scale = canvas != null ? Mathf.Max(.001f, canvas.scaleFactor) : 1;
            for (int bodyIndex = 0; bodyIndex < Owner.manager.GetRegionCount(); bodyIndex++) {
                if (!Owner.manager.TryGetSampledBodyByRegionIndex(bodyIndex, out var body)) continue;
                Color32 tint = Color.HSVToRGB((bodyIndex * .137f) % 1, .75f, 1);
                AppendBody(mesh, body, rect, Owner.drawBones, Owner.drawJoints, Owner.lineWidthPixels, Owner.jointDiameterPixels, scale, tint);
            }
        }
        internal void AppendBody(VertexHelper mesh, HumanVisionBody body, Rect rect, bool bones, bool joints, float lineWidth, float diameter, float scale, Color32 tint)
        {
                for (int j = 0; j < 32; j++) {
                    var point = body.CanonicalJoints[j].Position;
                    _valid[j] = point.Valid;
                    _points[j] = new Vector2(rect.xMin + rect.width * point.Normalized.x, rect.yMax - rect.height * point.Normalized.y);
                }
                if (bones) for (int j = 0; j < 32; j++) {
                    int parent = Parents[j];
                    if (parent < 0 || !_valid[parent] || !_valid[j]) continue;
                    Line(mesh, _points[parent], _points[j], Mathf.Max(.5f, lineWidth) / scale, tint);
                }
                if (joints) for (int j = 0; j < 32; j++) if (_valid[j])
                    Dot(mesh, _points[j], Mathf.Max(1, diameter) * .5f / scale, tint);
        }
        private static void Line(VertexHelper mesh, Vector2 a, Vector2 b, float width, Color32 tint)
        {
            Vector2 direction = b - a; if (direction.sqrMagnitude < .0001f) return;
            Vector2 normal = new Vector2(-direction.y, direction.x).normalized * width * .5f;
            int first = mesh.currentVertCount;
            mesh.AddVert(a - normal, tint, Vector2.zero); mesh.AddVert(a + normal, tint, Vector2.zero);
            mesh.AddVert(b + normal, tint, Vector2.zero); mesh.AddVert(b - normal, tint, Vector2.zero);
            mesh.AddTriangle(first, first+1, first+2); mesh.AddTriangle(first, first+2, first+3);
        }
        private static void Dot(VertexHelper mesh, Vector2 center, float radius, Color32 tint)
        {
            int first = mesh.currentVertCount; mesh.AddVert(center, tint, Vector2.zero);
            const int segments = 10;
            for (int i = 0; i < segments; i++) {
                float angle = i * (2 * Mathf.PI / segments);
                mesh.AddVert(center + new Vector2(Mathf.Cos(angle), Mathf.Sin(angle)) * radius, tint, Vector2.zero);
            }
            for (int i = 0; i < segments; i++) mesh.AddTriangle(first, first+1+i, first+1+(i+1)%segments);
        }
    }
}
