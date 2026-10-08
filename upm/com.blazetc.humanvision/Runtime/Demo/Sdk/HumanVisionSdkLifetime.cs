using System.Collections;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision
{
    // Runtime host 独立于用户场景对象。退役时即使场景卸载/总控禁用，也会继续等待复制完成。
    internal sealed class HumanVisionSdkLifetime : MonoBehaviour
    {
        internal bool Complete { get; private set; }
        private bool retiring;
        internal void Retire(HumanVisionManager manager, VideoPlayerFrameSource bridge,
            IHumanVisionFrameSource source, System.Action complete)
        {
            if (retiring) return;
            retiring = true; bridge?.DetachUnifiedSource();
            StartCoroutine(WaitAndRelease(manager, bridge, source, complete));
        }
        private IEnumerator WaitAndRelease(HumanVisionManager manager, VideoPlayerFrameSource bridge,
            IHumanVisionFrameSource source, System.Action complete)
        {
            // 等待的是帧复制/读回退役，不是在主线程等待推理。
            while (bridge != null && bridge.UnifiedRetirementPending) yield return null;
            source?.Close();
            manager?.Shutdown();
            Complete = true;
            complete?.Invoke();
            Destroy(gameObject);
        }
    }
}
