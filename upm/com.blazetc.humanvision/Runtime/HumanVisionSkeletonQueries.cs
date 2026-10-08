using System;
using UnityEngine;

namespace HumanVision
{
    /// <summary>骨骼复制的来源信息。与用户缓冲区一起保存，避免把历史数据误认为当前帧。</summary>
    public readonly struct HumanVisionSkeletonMetadata
    {
        /// <summary>稳定身份，不是数组 index。</summary>
        public long StableTrackId { get; }
        /// <summary>参与者槽位。</summary>
        public int Index { get; }
        /// <summary>原始结果序号；显示插值不增加这个值。</summary>
        public long ResultSequence { get; }
        /// <summary>推理使用的来源帧。</summary>
        public long SourceFrameId { get; }
        /// <summary>身体实际观测时间，微秒。</summary>
        public long ObservationTimestampUs { get; }
        internal HumanVisionSkeletonMetadata(HumanVisionBody body, long sequence, long frame)
        { StableTrackId = body.StableTrackId; Index = body.RegionIndex; ResultSequence = sequence; SourceFrameId = frame; ObservationTimestampUs = body.ObservationTimestampUs; }
    }

    /// <summary>无模型依赖的语义结果查询。所有调用在 Unity 主线程执行，身体引用仅借用于当前帧。</summary>
    public sealed class HumanVisionSkeletonQueries
    {
        private readonly HumanVisionSdkConfiguration configuration;
        private readonly HumanVisionBody[] slots;
        private bool known;
        private long sequence, frameId, timestamp;
        /// <summary>验证并复制设置；后续修改草稿不会改变本查询器。</summary>
        public HumanVisionSkeletonQueries(HumanVisionSdkConfiguration settings)
        {
            if (settings == null) throw new ArgumentNullException(nameof(settings));
            settings.Validate(); configuration = settings.Clone(); slots = new HumanVisionBody[settings.MaxBodies];
        }
        /// <summary>接收新原始观测。provenanceValid 表示来源与当前输入一致；空结果也是有效观测。</summary>
        public bool Observe(HumanVisionBody[] bodies, int count, long resultSequence, long sourceFrameId, long sourceTimestampUs, bool provenanceValid)
        {
            if (!provenanceValid || sourceTimestampUs < 0 || sourceFrameId < 0 || resultSequence <= sequence ||
                count < 0 || count > (bodies?.Length ?? 0)) return false;
            Array.Clear(slots, 0, slots.Length);
            known = true; sequence = resultSequence; frameId = sourceFrameId; timestamp = sourceTimestampUs;
            for (int i = 0; i < count; i++) {
                var body = bodies[i];
                if (body != null && body.StableTrackId > 0 && body.RegionIndex >= 0 &&
                    body.RegionIndex < slots.Length && slots[body.RegionIndex] == null) slots[body.RegionIndex] = body;
            }
            return true;
        }
        /// <summary>输入变化、区域版本变化或停止后，立即清除当前身份和占用。</summary>
        public void Clear() { Array.Clear(slots, 0, slots.Length); known = false; sequence = 0; frameId = -1; timestamp = 0; }
        /// <summary>判断最近原始结果是否仍有效；nowUs 必须与观测使用同一 Unity 单调时钟。</summary>
        public bool HasFreshResult(long nowUs) => known && Fresh(timestamp, nowUs, configuration.MaximumResultAgeMilliseconds);
        /// <summary>查询 index 槽位。index 可以有空缺；返回的 body 不可跨回调长期保存。</summary>
        public bool TryGetBody(int index, long nowUs, out HumanVisionBody body)
        {
            body = null;
            if (index < 0 || index >= slots.Length || !HasFreshResult(nowUs)) return false;
            var candidate = slots[index];
            if (candidate == null || !Fresh(candidate.ObservationTimestampUs, nowUs, configuration.MaximumResultAgeMilliseconds)) return false;
            body = candidate; return true;
        }
        /// <summary>当前有效参与者数；不能将此数直接当成连续 index 的上限。</summary>
        public int GetUsersCount(long nowUs)
        { int count = 0; for (int i = 0; i < slots.Length; i++) if (TryGetBody(i, nowUs, out _)) count++; return count; }
        /// <summary>稳定 ID 对应的槽位；未检测到返回 -1。</summary>
        public int GetUserIndexById(long id, long nowUs)
        { if (id <= 0) return -1; for (int i = 0; i < slots.Length; i++) if (TryGetBody(i, nowUs, out var body) && body.StableTrackId == id) return i; return -1; }
        /// <summary>返回是否已知当前占用。未知/过期时返回 false；有效空结果返回 true、occupied=false。</summary>
        public bool TryGetOccupancy(int index, long nowUs, out bool occupied)
        {
            occupied = false;
            if (index < 0 || index >= slots.Length || !HasFreshResult(nowUs)) return false;
            // 旧人体不能因为新结果引用而伪装成明确无人。
            if (slots[index] != null && !Fresh(slots[index].ObservationTimestampUs, nowUs, configuration.MaximumResultAgeMilliseconds)) return false;
            occupied = TryGetBody(index, nowUs, out _); return true;
        }
        /// <summary>读取 32 点语义关节。身体和手分别检查时效，并检查有效性、置信度和有限坐标。</summary>
        public bool TryGetJoint(int index, HumanVisionCanonicalJointId id, long nowUs, out HumanVisionCanonicalJoint joint)
        {
            joint = default;
            int value = (int)id;
            if (value < 0 || value >= 32 || !TryGetBody(index, nowUs, out var body)) return false;
            var candidate = body.CanonicalJoints[value]; var p = candidate.Position;
            bool hand = value >= 8 && value <= 10 || value >= 15 && value <= 17;
            float limit = hand ? configuration.MaximumHandAgeMilliseconds : configuration.MaximumResultAgeMilliseconds;
            if (!p.Valid || !HumanVisionSdkConfiguration.Finite(p.Confidence) || p.Confidence < configuration.MinimumJointConfidence ||
                !Finite(p.Normalized) || !Finite(p.Pixel) || !Fresh(candidate.ObservationTimestampUs, nowUs, limit)) return false;
            joint = candidate; return true;
        }
        /// <summary>将当帧有效关节写入用户提供的至少 32 项数组；失效关节写 default，不分配新数组。</summary>
        public bool CopySkeleton(int index, long nowUs, HumanVisionCanonicalJoint[] destination, out HumanVisionSkeletonMetadata metadata)
        {
            metadata = default;
            if (destination == null || destination.Length < 32 || !TryGetBody(index, nowUs, out var body)) return false;
            for (int i = 0; i < 32; i++) { TryGetJoint(index, (HumanVisionCanonicalJointId)i, nowUs, out var joint); destination[i] = joint; }
            metadata = new HumanVisionSkeletonMetadata(body, sequence, frameId); return true;
        }
        /// <summary>映射到实际图像显示矩形中的屏幕像素，左下角原点。输入已旋转/镜像，不再重复变换。</summary>
        public bool TryGetScreenPosition(int index, HumanVisionCanonicalJointId id, long nowUs, Rect imageRect, out Vector2 position)
        {
            position = default;
            if (!ValidRect(imageRect) || !TryGetJoint(index, id, nowUs, out var joint)) return false;
            Vector2 p = joint.Position.Normalized;
            position = new Vector2(imageRect.x + p.x * imageRect.width, imageRect.y + (1 - p.y) * imageRect.height);
            return true;
        }
        /// <summary>映射到平面中心原点的 XY 坐标，再转换为 Unity 世界坐标；不是人体真实深度。</summary>
        public bool TryGetWorldPosition(int index, HumanVisionCanonicalJointId id, long nowUs, Transform plane, Vector2 size, out Vector3 position)
        {
            position = default;
            if (!Finite(size) || size.x <= 0 || size.y <= 0 || !TryGetJoint(index, id, nowUs, out var joint)) return false;
            Vector2 p = joint.Position.Normalized;
            var local = new Vector3((p.x - .5f) * size.x, (.5f - p.y) * size.y, 0);
            position = plane == null ? local : plane.TransformPoint(local);
            return HumanVisionSdkConfiguration.Finite(position.x) && HumanVisionSdkConfiguration.Finite(position.y) && HumanVisionSdkConfiguration.Finite(position.z);
        }
        /// <summary>返回两关节在 XY 图像平面中的单位方向，Y 向上；重合点返回 false。</summary>
        public bool TryGetDirection(int index, HumanVisionCanonicalJointId first, HumanVisionCanonicalJointId second, long nowUs, out Vector3 direction)
        {
            direction = default;
            if (!TryGetJoint(index, first, nowUs, out var a) || !TryGetJoint(index, second, nowUs, out var b)) return false;
            Vector2 delta = b.Position.Normalized - a.Position.Normalized;
            if (delta.sqrMagnitude < 1e-12f) return false;
            direction = new Vector3(delta.x, -delta.y, 0).normalized; return true;
        }
        /// <summary>读取 first-center-last 三点的平面夹角，单位度；不是 3D 关节姿态。</summary>
        public bool TryGetAngle(int index, HumanVisionCanonicalJointId first, HumanVisionCanonicalJointId center,
            HumanVisionCanonicalJointId last, long nowUs, out float degrees)
        {
            degrees = 0;
            if (!TryGetJoint(index, first, nowUs, out var a) || !TryGetJoint(index, center, nowUs, out var b) ||
                !TryGetJoint(index, last, nowUs, out var c)) return false;
            Vector2 left = a.Position.Normalized - b.Position.Normalized, right = c.Position.Normalized - b.Position.Normalized;
            if (left.sqrMagnitude < 1e-12f || right.sqrMagnitude < 1e-12f) return false;
            degrees = Vector2.Angle(left, right); return true;
        }
        private static bool Fresh(long time, long now, float milliseconds) => time >= 0 && now >= time && (double)now - time <= milliseconds * 1000d;
        private static bool Finite(Vector2 p) => HumanVisionSdkConfiguration.Finite(p.x) && HumanVisionSdkConfiguration.Finite(p.y);
        private static bool ValidRect(Rect r) => Finite(r.position) && Finite(r.size) && r.width > 0 && r.height > 0;
    }
}
