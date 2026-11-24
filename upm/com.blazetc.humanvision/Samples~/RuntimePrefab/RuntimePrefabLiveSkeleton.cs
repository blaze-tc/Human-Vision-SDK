using HumanVision;
using HumanVision.Input;
using UnityEngine;

namespace HumanVisionAcceptance
{
    // Writes only the sampled points and lines onto the visible preview.
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

        internal bool HasBody(int slot) => visible[slot];

        internal bool TryGetPoint(int slot, int joint, out Vector2 point)
        {
            bool tracked = visible[slot] && valid[slot, joint];
            point = tracked ? points[slot, joint] : default;
            return tracked;
        }

        internal void Clear() => ClearGeometry();

        private void ClearGeometry()
        {
            VisibleBodies = VisibleJoints = VisibleBones = 0;
            System.Array.Clear(visible, 0, visible.Length);
        }

        private static int[] ReadParents()
        {
            var parents = new int[JointCount];
            for (int joint = 0; joint < parents.Length; joint++)
                parents[joint] = RuntimeSkeletonReader.TryGetParentJoint((HumanVisionCanonicalJointId)joint, out var parent)
                    ? (int)parent : -1;
            return parents;
        }

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
