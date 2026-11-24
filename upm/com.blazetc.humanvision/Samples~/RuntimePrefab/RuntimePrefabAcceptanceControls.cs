using System;
using System.Collections;
using System.Linq;
using HumanVision;
using UnityEngine;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private readonly bool[] installedModes = new bool[3];
        private readonly ModelInputQualityChoice[][] installedQualities = new ModelInputQualityChoice[3][];

        private IEnumerator PrepareManualChoices()
        {
            PrepareInteractiveState();
            status = "正在检查已安装的模型和分析设备…";
            string error = null;
            string manualRuntimeRoot = startup.RuntimeRoot;
            if (string.IsNullOrWhiteSpace(manualRuntimeRoot))
                yield return HumanVisionRuntimeData.Prepare(path => manualRuntimeRoot = path, failure => error = failure);
            if (string.IsNullOrEmpty(manualRuntimeRoot)) {
                status = diagnostics.Redact(error ?? "模型资源未就绪");
                yield break;
            }

            startup.RuntimeRoot = manualRuntimeRoot;
            EnsureRuntimeView();
            HumanVisionAccelerationMode[] modes = {
                HumanVisionAccelerationMode.Cpu, HumanVisionAccelerationMode.Graphics, HumanVisionAccelerationMode.Neural
            };
            foreach (var mode in modes) {
                if (mode == HumanVisionAccelerationMode.Neural && !IsAndroid) {
                    backendReasons[(int)mode] = "Windows 不支持 NPU";
                    continue;
                }
                var requested = startup.Clone();
                requested.AccelerationMode = mode;
                requested.UseWindowsCpu = mode == HumanVisionAccelerationMode.Cpu;
                requested.InputQuality = ModelInputQuality.Low;
                var available = view.Sdk.GetInputQualityCapabilities(requested);
                int index = (int)mode;
                if (!string.IsNullOrEmpty(available.Error)) { backendReasons[index] = available.Error; continue; }
                installedQualities[index] = available.Choices;
                bool modelsPresent = available.Choices.Length > 0 ||
                    IsAndroid && mode == HumanVisionAccelerationMode.Cpu && available.RuntimeProfile == "android-cpu-nohands";
                if (!modelsPresent) { backendReasons[index] = "该后端模型未安装"; continue; }
                yield return ProbeManualBackend(mode, requested);
                if (view.Sdk.State != HumanVisionSdkState.Stopped || view.Sdk.IsInitialized) {
                    status = backendReasons[index]; yield break;
                }
            }

            status = "选择输入、分析设备与模型等级，再连接；基础配置可编辑人数、区域和显示";
#if UNITY_EDITOR
            if (!string.IsNullOrWhiteSpace(editorVideoPath)) {
                yield return ConnectInteractive();
                yield break;
            }
#endif
            if (autoConnect) yield return ConnectInteractive();
        }

        private bool HasManualQuality(HumanVisionAccelerationMode mode, ModelInputQuality quality)
        {
            if (!installedModes[(int)mode]) return false;
            var choices = installedQualities[(int)mode];
            if (IsAndroid && mode == HumanVisionAccelerationMode.Cpu && (choices == null || choices.Length == 0)) return quality == ModelInputQuality.Low;
            return choices != null && choices.Any(item => item.Quality == quality);
        }

    }
}
