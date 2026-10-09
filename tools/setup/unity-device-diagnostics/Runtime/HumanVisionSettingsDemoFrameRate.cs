using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// 设置演示场景的 Android 帧率配置。Unity 的默认 -1 在 Android 上通常为 30 FPS；
    /// RTSP 发布要逐帧检查转换/复制 fence，明确请求 60 FPS 为这些异步阶段留出刷新机会。
    /// 这是应用渲染目标，不代表输入、推理或完整骨骼能够达到 60 FPS。
    /// </summary>
    [DefaultExecutionOrder(-600), DisallowMultipleComponent]
    public sealed class HumanVisionSettingsDemoFrameRate : MonoBehaviour
    {
        [SerializeField, Range(30, 120), Tooltip("Android 应用的渲染目标；默认 60。实际帧率另由诊断日志测量。")]
        private int androidTargetFrameRate = 60;
        private int previousTarget, appliedTarget;
        private bool applied;

        public int AndroidTargetFrameRate => androidTargetFrameRate;

        private void OnEnable()
        {
            if (Application.platform != RuntimePlatform.Android) return;
            previousTarget = Application.targetFrameRate;
            appliedTarget = Mathf.Clamp(androidTargetFrameRate, 30, 120);
            Application.targetFrameRate = appliedTarget;
            applied = true;
        }

        private void OnDisable()
        {
            // 只恢复我们仍持有的配置，避免覆盖游戏其他脚本在之后设置的新目标。
            if (applied && Application.targetFrameRate == appliedTarget)
                Application.targetFrameRate = previousTarget;
            applied = false;
        }
    }
}
