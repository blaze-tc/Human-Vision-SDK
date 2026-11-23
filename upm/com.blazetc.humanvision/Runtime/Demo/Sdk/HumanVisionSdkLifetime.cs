using System.Collections;
using System.Threading.Tasks;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision
{
    // Runtime host 独立于用户场景对象。退役时即使场景卸载/总控禁用，也会继续等待复制完成。
    internal sealed class HumanVisionSdkLifetime : MonoBehaviour
    {
        internal bool Complete { get; private set; }
        internal string RetirementDiagnostics { get; private set; } = "";
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
            var deferred = manager != null ? manager.DetachDeferredRuntimeSession() : null;
            if (deferred != null) {
                deferred.RequestClose();
                double nextDiagnosticAt = 0;
                while (!deferred.TryCompleteClose()) {
                    if (Time.realtimeSinceStartupAsDouble >= nextDiagnosticAt) {
                        RetirementDiagnostics = deferred.Diagnostics; nextDiagnosticAt = Time.realtimeSinceStartupAsDouble + .25;
                    }
                    yield return null;
                }
            }
            var neural = manager != null ? manager.DetachNeuralRuntimeSession() : null;
            if (neural != null) {
                var release = Task.Run(() => neural.Dispose());
                while (!release.IsCompleted) yield return null;
                if (release.IsFaulted) Debug.LogException(release.Exception.GetBaseException());
            }
            manager?.Shutdown();
            // Android RTSP retains the process-wide native input owner until
            // its decoder and GPU resources have retired. Keep the host alive
            // and delay Stopped so immediate reinitialization can acquire it.
            while (source != null && source.State == InputSourceState.Closing) yield return null;
            Complete = true;
            complete?.Invoke();
            Destroy(gameObject);
        }
    }
}
