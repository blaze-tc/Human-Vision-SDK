using System;
using UnityEngine;

namespace HumanVision
{
    /// <summary>游戏层识别设置。区域使用图像左上角原点的归一化坐标；修改后通过 SDK 应用。</summary>
    [Serializable]
    public sealed class HumanVisionSdkConfiguration
    {
        /// <summary>并发参与者槽位上限，范围 1–8；与当前检测到的人数不同。</summary>
        [Range(1, 8), Tooltip("识别人数上限（1–8），不是当前实际人数。")]
        public int MaxBodies = 4;
        /// <summary>启用一人一区域；区域数组长度必须等于人数上限。</summary>
        [Tooltip("开启后按区域固定参与者 index，每个区域选择一人。")]
        public bool UseRegions;
        /// <summary>左上角原点，X 向右、Y 向下；范围 0–1，区域不可重叠。</summary>
        public Rect[] Regions = CreateEqualRegions(4);
        /// <summary>低于此置信度的关节查询返回 false。</summary>
        [Range(0, 1), Tooltip("关节最低有效置信度。")]
        public float MinimumJointConfidence = .35f;
        /// <summary>原始身体结果最大年龄，单位毫秒；过期时占用状态变为未知。</summary>
        [Min(1), Tooltip("身体结果最大年龄（毫秒）。")]
        public float MaximumResultAgeMilliseconds = 1000;
        /// <summary>手掌、指尖和拇指使用各自观测时间，单位毫秒。</summary>
        [Min(1), Tooltip("手关节最大年龄（毫秒），独立于身体结果。")]
        public float MaximumHandAgeMilliseconds = 200;

        /// <summary>取得可独立编辑的副本，包含区域数组副本。</summary>
        public HumanVisionSdkConfiguration Clone()
        {
            var copy = (HumanVisionSdkConfiguration)MemberwiseClone();
            copy.Regions = Regions == null ? null : (Rect[])Regions.Clone();
            return copy;
        }

        /// <summary>验证整个草稿；失败抛出明确错误，调用方不得先修改运行设置。</summary>
        public void Validate()
        {
            if (MaxBodies < 1 || MaxBodies > 8) throw new ArgumentOutOfRangeException(nameof(MaxBodies), "人数上限必须为 1–8。");
            if (!Finite(MinimumJointConfidence) || MinimumJointConfidence < 0 || MinimumJointConfidence > 1)
                throw new ArgumentException("关节置信度必须为 0–1。");
            if (!Finite(MaximumResultAgeMilliseconds) || MaximumResultAgeMilliseconds <= 0 ||
                !Finite(MaximumHandAgeMilliseconds) || MaximumHandAgeMilliseconds <= 0)
                throw new ArgumentException("结果与手关节时效必须是大于零的有限毫秒数。");
            if (Regions == null) throw new ArgumentException("区域数组不能为 null；关闭区域时可以使用空数组。");
            if (!UseRegions) return;
            if (Regions.Length != MaxBodies) throw new ArgumentException("开启区域时，需要为每个参与者槽位配置一个区域。");
            for (int i = 0; i < Regions.Length; i++) {
                Rect r = Regions[i];
                if (!Finite(r.x) || !Finite(r.y) || !Finite(r.width) || !Finite(r.height) ||
                    r.x < 0 || r.y < 0 || r.width < .01f || r.height < .01f ||
                    r.xMax > 1 || r.yMax > 1) throw new ArgumentException("区域必须位于图像 0–1 范围内，宽高至少 0.01。");
                for (int j = 0; j < i; j++) {
                    Rect q = Regions[j];
                    if (Mathf.Min(r.xMax, q.xMax) - Mathf.Max(r.xMin, q.xMin) > .000001f &&
                        Mathf.Min(r.yMax, q.yMax) - Mathf.Max(r.yMin, q.yMin) > .000001f)
                        throw new ArgumentException("区域不可重叠，请调整后再应用。");
                }
            }
        }

        /// <summary>显式生成横向均分区域；只供编辑草稿，不会自动改变 Runtime。</summary>
        public static Rect[] CreateEqualRegions(int count)
        {
            if (count < 1 || count > 8) throw new ArgumentOutOfRangeException(nameof(count));
            var regions = new Rect[count];
            for (int i = 0; i < count; i++) {
                float left = (float)i / count, right = (float)(i + 1) / count;
                regions[i] = new Rect(left, 0, right - left, 1);
            }
            return regions;
        }
        internal static bool Finite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
    }
}
