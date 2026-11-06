using System.Collections.Generic;
using HumanVision;
using UnityEngine;

namespace HumanVisionAcceptance
{
    /// <summary>Game-side adapter over the public SDK. Sample once in Update and use stable track IDs for gameplay.</summary>
    public sealed class RuntimeSkeletonReader
    {
        public const int JointCount = 32;
        public const int SlotCapacity = 8;
        private static readonly int[] Parents = { -1,0,1,2,3,4,5,6,7,8,8,3,11,12,13,14,15,15,0,18,19,20,0,22,23,24,3,26,27,28,27,30 };
        private readonly Dictionary<long, int> slotsById = new Dictionary<long, int>(SlotCapacity);
        private readonly long[] ids = new long[SlotCapacity];
        private readonly Vector3[,] positions = new Vector3[SlotCapacity, JointCount];
        private readonly bool[,] tracked = new bool[SlotCapacity, JointCount];

        public int PlayerCapacity { get; private set; }
        public int RegionCount { get; private set; }
        public int GetJointCount() => JointCount;

        public bool TryRead(HumanVisionRuntimeView view, out HumanVisionPresentedFrame frame)
        {
            frame = default;
            slotsById.Clear();
            PlayerCapacity = view == null ? 0 : Mathf.Clamp(view.Sdk.GetMaxBodies(), 0, SlotCapacity);
            RegionCount = view != null && view.Sdk.RegionsEnabled ? PlayerCapacity : 0;
            if (view == null || !view.TryGetFrame(out frame)) return false;
            for (int slot = 0; slot < PlayerCapacity; slot++) {
                if (!view.TryGetBodyId(slot, out ids[slot])) continue;
                slotsById[ids[slot]] = slot;
                for (int joint = 0; joint < JointCount; joint++)
                    tracked[slot, joint] = view.TryGetJointPosition(slot, (HumanVisionCanonicalJointId)joint, out positions[slot, joint]);
            }
            return true;
        }

        public long GetUserIdByIndex(int index)
        {
            if (index < 0 || index >= PlayerCapacity) return 0;
            long id = ids[index];
            return id > 0 && slotsById.ContainsKey(id) ? id : 0;
        }

        public long GetUserIdByRegionIndex(int regionIndex) =>
            regionIndex >= 0 && regionIndex < RegionCount ? GetUserIdByIndex(regionIndex) : 0;

        /// <summary>Query the drawn world position for a configured, zero-based recognition region.</summary>
        public bool TryGetJointPositionByRegionIndex(int regionIndex, HumanVisionCanonicalJointId joint, out Vector3 position)
        {
            return TryGetJointPosition(GetUserIdByRegionIndex(regionIndex), joint, out position);
        }

        public bool IsUserDetected(int index) => GetUserIdByIndex(index) > 0;
        public bool IsJointTracked(long userId, HumanVisionCanonicalJointId joint) => TryGetJointPosition(userId, joint, out _);

        public bool TryGetJointPosition(long userId, HumanVisionCanonicalJointId joint, out Vector3 position)
        {
            int index = (int)joint;
            if (index >= 0 && index < JointCount && slotsById.TryGetValue(userId, out int slot) && tracked[slot, index]) {
                position = positions[slot, index];
                return true;
            }
            position = default;
            return false;
        }

        public static bool TryGetParentJoint(HumanVisionCanonicalJointId joint, out HumanVisionCanonicalJointId parent)
        {
            int i = (int)joint;
            if (i >= 0 && i < Parents.Length && Parents[i] >= 0) {
                parent = (HumanVisionCanonicalJointId)Parents[i];
                return true;
            }
            parent = default;
            return false;
        }
    }
}
