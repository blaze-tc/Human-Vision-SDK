using System;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision
{
    /// <summary>
    /// 项目自己的 SDK 默认配置资产。初始化始终读取深副本，运行时改人数、区域或地址不会修改资产。
    /// 地址可留空，由 CreateOptionsForInput 或 SDK.InitializeFromPreset 在初始化代码中提供。
    /// </summary>
    [CreateAssetMenu(fileName = "HumanVisionSdkConfiguration", menuName = "Human Vision/SDK Configuration")]
    public sealed class HumanVisionSdkPreset : ScriptableObject
    {
        [SerializeField] private HumanVisionSdkOptions options = new HumanVisionSdkOptions();

        /// <summary>读取独立启动草稿；CPU/GPU/NPU 选择同时映射旧版 Windows CPU 开关。</summary>
        public HumanVisionSdkOptions CreateOptions()
        {
            var value = (options ?? new HumanVisionSdkOptions()).Clone();
            value.UseWindowsCpu = value.AccelerationMode == HumanVisionAccelerationMode.Cpu;
            return value;
        }

        /// <summary>
        /// 读取默认配置并覆盖本次输入。WebCamera 的 address 是设备名（空串自动选择）；
        /// Rtsp 是 rtsp:// 地址；Video 是本地完整路径、StreamingAssets 完整地址或受支持的视频 URL。
        /// 不修改资产；返回后仍可调整本次启动参数。启动时检查地址和平台能力。
        /// </summary>
        public HumanVisionSdkOptions CreateOptionsForInput(InputKind kind, string address)
        {
            if (!Enum.IsDefined(typeof(InputKind), kind)) throw new ArgumentOutOfRangeException(nameof(kind));
            var value = CreateOptions();
            value.SourceKind = kind;
            // 不把资产里其他输入源的地址带入本次初始化，避免意外连接旧地址。
            value.Input.CameraDevice = ""; value.Input.RtspUrl = ""; value.Input.VideoPath = "";
            switch (kind) {
                case InputKind.WebCamera: value.Input.CameraDevice = address ?? ""; break;
                case InputKind.Rtsp: value.Input.RtspUrl = address ?? ""; break;
                case InputKind.Video: value.Input.VideoPath = address ?? ""; break;
            }
            return value;
        }

        /// <summary>
        /// 写入资产内存中的默认设置并深复制。编辑器工具保存资产时需自行 SetDirty/SaveAssets。
        /// 校验通用参数但允许地址暂缺；最终初始化仍执行完整校验。
        /// </summary>
        public void SetOptions(HumanVisionSdkOptions value)
        {
            if (value == null) throw new ArgumentNullException(nameof(value));
            var copy = value.Clone();
            bool open = copy.OpenInputOnInitialize;
            copy.OpenInputOnInitialize = false;
            copy.Validate();
            copy.OpenInputOnInitialize = open;
            copy.UseWindowsCpu = copy.AccelerationMode == HumanVisionAccelerationMode.Cpu;
            options = copy;
        }
    }
}
