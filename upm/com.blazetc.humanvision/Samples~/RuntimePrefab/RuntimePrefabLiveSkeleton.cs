using HumanVision;
using HumanVision.Input;
using UnityEngine;

namespace HumanVisionAcceptance
{
    // Reads valid SDK 2D coordinates in Update; draws ordinary points and lines.
    internal sealed class RuntimePrefabLiveSkeleton
    {
        private const int Capacity = 8;
        private const int JointCount = RuntimeSkeletonReader.JointCount;
        private static readonly int[] Parents = ReadParents();
        private readonly Vector2[,] points = new Vector2[Capacity, JointCount];
        private readonly bool[,] valid = new bool[Capacity, JointCount];
        private readonly bool[] visible = new bool[Capacity];

        internal int VisibleBodies { get; private set; }
        internal int VisibleJoints { get; private set; }
        internal int VisibleBones { get; private set; }

        internal void Sample(HumanVisionSdk sdk)
        {
            ClearGeometry();
            if (sdk == null) return;
            // Ordinary business Update reads the latest valid SDK coordinates.
            // The SDK applies confidence, source identity and body/hand age limits.
            // No preview texture, new result event or matching frame is required.
            int capacity = System.Math.Min(Capacity, sdk.GetMaxBodies());
            for (int slot = 0; slot < capacity; slot++) {
                int count = 0;
                for (int joint = 0; joint < JointCount; joint++) {
                    bool accepted = sdk.TryGetJointNormalizedPosition(slot, (HumanVisionCanonicalJointId)joint, out var uv);
                    valid[slot, joint] = accepted && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
                    points[slot, joint] = uv;
                    if (valid[slot, joint]) ++count;
                }
                if (count == 0) continue;
                visible[slot] = true;
                ++VisibleBodies;
                VisibleJoints += count;
                for (int joint = 0; joint < JointCount; joint++) {
                    int parent = Parents[joint];
                    if (parent >= 0 && valid[slot, joint] && valid[slot, parent] &&
                        points[slot, joint] != points[slot, parent]) ++VisibleBones;
                }
            }
        }

        internal void Clear() => ClearGeometry();

        private void ClearGeometry()
        {
            VisibleBodies = VisibleJoints = VisibleBones = 0;
            System.Array.Clear(visible, 0, visible.Length);
        }

        internal void Draw(Rect image, float strokeWidth)
        {
            if (Event.current.type != EventType.Repaint || VisibleJoints == 0) return;
            Texture2D pixel = Texture2D.whiteTexture;
            Color previousColor = GUI.color;
            Matrix4x4 previousMatrix = GUI.matrix;
            try {
                for (int slot = 0; slot < Capacity; slot++) {
                    if (!visible[slot]) continue;
                    GUI.color = RuntimePrefabRedrawRig.GetSlotColor(slot);
                    for (int joint = 0; joint < JointCount; joint++) {
                        if (!valid[slot, joint]) continue;
                        int parent = Parents[joint];
                        if (parent < 0 || !valid[slot, parent]) continue;
                        Vector2 a = ToImage(points[slot, parent], image);
                        Vector2 delta = ToImage(points[slot, joint], image) - a;
                        float length = delta.magnitude;
                        if (length < .01f) continue;

                        // A unit axis along the bone and another perpendicular to it.
                        Vector2 direction = delta / length;
                        var bone = Matrix4x4.identity;
                        bone.m00 = bone.m11 = direction.x;
                        bone.m10 = direction.y;
                        bone.m01 = -direction.y;
                        bone.m03 = a.x;
                        bone.m13 = a.y;
                        GUI.matrix = previousMatrix * bone;
                        GUI.DrawTexture(new Rect(0, -strokeWidth * .5f, length, strokeWidth), pixel);
                    }
                    GUI.matrix = previousMatrix;
                    float radius = strokeWidth * .85f;
                    for (int joint = 0; joint < JointCount; joint++) {
                        if (!valid[slot, joint]) continue;
                        Vector2 p = ToImage(points[slot, joint], image);
                        GUI.DrawTexture(new Rect(p.x - radius, p.y - radius, radius * 2, radius * 2), pixel);
                    }
                }
            } finally {
                GUI.color = previousColor;
                GUI.matrix = previousMatrix;
            }
        }

        private static int[] ReadParents()
        {
            var parents = new int[JointCount];
            for (int joint = 0; joint < parents.Length; joint++)
                parents[joint] = RuntimeSkeletonReader.TryGetParentJoint((HumanVisionCanonicalJointId)joint, out var parent)
                    ? (int)parent : -1;
            return parents;
        }

        private static Vector2 ToImage(Vector2 point, Rect image) =>
            new Vector2(image.x + point.x * image.width, image.y + point.y * image.height);

        internal static Rect Fit(Rect box, int width, int height, bool fill = false)
        {
            float x = box.width / width;
            float y = box.height / height;
            float scale = fill ? Mathf.Max(x, y) : Mathf.Min(x, y);
            Vector2 size = new Vector2(width, height) * scale;
            return new Rect(box.center - size * .5f, size);
        }
    }
}
