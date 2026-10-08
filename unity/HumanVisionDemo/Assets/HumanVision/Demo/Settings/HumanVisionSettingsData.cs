using System;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision.Demo
{
    /// <summary>设置场景独立持久化合同：三种输入分别保存，共享人数和区域。与旧 Demo 配置互不覆盖。</summary>
    [Serializable]
    public sealed class HumanVisionSettingsData
    {
        public int Version = 1;
        public HumanVisionSdkConfiguration Recognition = new HumanVisionSdkConfiguration();
        public DemoModeSettings Camera = new DemoModeSettings(), Video = new DemoModeSettings(), Rtsp = new DemoModeSettings();
        public InputKind SourceKind = InputKind.WebCamera;
        public ModelInputQuality InputQuality = ModelInputQuality.Medium;
        public bool UseWindowsCpu = true, RtspTcp = true, AutoStart;
        public bool DetailedLogs;
        public float StatisticsInterval = 2, SkeletonLogInterval = 2;
        public int LogFileMegabytes = 8, RetainedLogSessions = 3;
        /// <summary>设置操作时深复制，不在识别热路径使用 JSON。</summary>
        public HumanVisionSettingsData Clone() => JsonUtility.FromJson<HumanVisionSettingsData>(JsonUtility.ToJson(this));
        /// <summary>当前模式草稿，切换模式不会覆盖其它模式。</summary>
        public DemoModeSettings Mode => SourceKind == InputKind.Video ? Video : SourceKind == InputKind.Rtsp ? Rtsp : Camera;
        /// <summary>转成总控启动配置；只有提交时才要求输入地址。</summary>
        public HumanVisionSdkOptions ToOptions(string runtimeRoot = "") => new HumanVisionSdkOptions {
            Recognition = Recognition.Clone(), SourceKind = SourceKind, Input = JsonUtility.FromJson<DemoModeSettings>(JsonUtility.ToJson(Mode)),
            InputQuality = InputQuality, UseWindowsCpu = UseWindowsCpu, RtspTcp = RtspTcp, RuntimeRoot = runtimeRoot
        };
        /// <summary>验证所有保存模式；未使用模式允许暂时没有地址。</summary>
        public void Validate()
        {
            if (Version != 1 || Recognition == null || Camera == null || Video == null || Rtsp == null ||
                !Enum.IsDefined(typeof(InputKind), SourceKind) || !Enum.IsDefined(typeof(ModelInputQuality), InputQuality))
                throw new ArgumentException("设置版本或输入模式无效。");
            Recognition.Validate(); Camera.Validate(); Video.Validate(); Rtsp.Validate();
            if (!Finite(StatisticsInterval) || StatisticsInterval < .2f || StatisticsInterval > 60 ||
                !Finite(SkeletonLogInterval) || SkeletonLogInterval < .2f || SkeletonLogInterval > 60 ||
                LogFileMegabytes < 1 || LogFileMegabytes > 64 || RetainedLogSessions < 1 || RetainedLogSessions > 20)
                throw new ArgumentException("日志间隔须为 0.2–60 秒，单文件 1–64 MB，保留会话 1–20。");
        }
        private static bool Finite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
    }
}
