using System;

namespace HumanVision
{
    /// <summary>模型等级的真实可用性。固定平台、资源准备中和资源损坏分别说明；不虚构平台支持。</summary>
    public sealed class HumanVisionSdkQualityCapabilities
    {
        private readonly ModelInputQualityChoice[] choices;
        public ModelInputQualityChoice[] Choices => (ModelInputQualityChoice[])choices.Clone();
        public bool Selectable => choices.Length > 1;
        public string RuntimeProfile { get; }
        public string Message { get; }
        public string Error { get; }
        private HumanVisionSdkQualityCapabilities(string profile, string message, ModelInputQualityChoice[] values, string error = "")
        { RuntimeProfile = profile; Message = message; choices = values; Error = error; }

        /// <summary>配置时读取已校验的资源目录，无需初始化 Native 或打开摄像头；不要逐帧调用。</summary>
        public static HumanVisionSdkQualityCapabilities Load(string root, string profile) => Load(root, profile, 4);
        internal static HumanVisionSdkQualityCapabilities Load(string root, string profile, int maxBodies)
        {
            profile = profile ?? "";
            if (profile == HumanVisionAndroidAccelerationSelection.CpuProfile) {
                if (string.IsNullOrEmpty(root)) return new HumanVisionSdkQualityCapabilities(profile,"正在准备 Android CPU 模型资源。",Array.Empty<ModelInputQualityChoice>());
                try {
                    HumanVisionAndroidCpuModelContract.Validate(root);
                    var contract=HumanVision.Demo.AnalysisContract.Load(root,profile,maxBodies);
                    return new HumanVisionSdkQualityCapabilities(profile,"Android CPU 使用固定模型："+contract.ModelPackId+"，骨骼输入 "+contract.PoseWidth+"×"+contract.PoseHeight+
                        "（1–2 人 192×256；3–8 人 416×416）。不提供独立手部模型；实际设备速度以运行统计为准。",Array.Empty<ModelInputQualityChoice>());
                } catch(Exception e) { return Failed(profile,e.Message); }
            }
            if (profile == HumanVisionAndroidAccelerationSelection.NeuralProfile) {
                if (string.IsNullOrEmpty(root)) return new HumanVisionSdkQualityCapabilities(profile, "正在准备 RK3588 NPU 模型资源。", Array.Empty<ModelInputQualityChoice>());
                try {
                    HumanVisionNeuralModelContract.Validate(root);
                    return new HumanVisionSdkQualityCapabilities(profile, "RK3588 NPU 仅提供已通过离线数值验证的低等级 512×288。中 / 高暂不可用；设备性能尚待 RK3588 实测。",
                        new[] { new ModelInputQualityChoice(ModelInputQuality.Low, profile, 512, 288) });
                } catch (Exception e) { return Failed(profile, e.Message); }
            }
            if (profile != HumanVisionModelInputQualities.AdmittedRuntimeMode) {
                string reason = profile.StartsWith("windows-", StringComparison.Ordinal)
                    ? "Windows 使用固定模型。\n高/中/低仅支持 Android NCNN/Vulkan。"
                    : "当前运行配置使用固定模型；等级切换仅适用于 Android NCNN/Vulkan。";
                return new HumanVisionSdkQualityCapabilities(profile, reason, Array.Empty<ModelInputQualityChoice>());
            }
            if (string.IsNullOrEmpty(root))
                return new HumanVisionSdkQualityCapabilities(profile, "正在准备模型等级资源，完成后可在识别启动前选择。", Array.Empty<ModelInputQualityChoice>());
            try {
                var values = HumanVisionModelInputQualities.Load(root).ChoicesForMode(profile);
                return new HumanVisionSdkQualityCapabilities(profile,
                    values.Length == 0 ? "此安装包未声明等级目录，当前 Android 模型只能使用固定等级。"
                        : "选择已安装的模型输入等级；应用后重新加载模型。分辨率越高，计算量越大。", values);
            } catch (Exception e) {
                return new HumanVisionSdkQualityCapabilities(profile, "模型等级资源校验失败：" + e.Message,
                    Array.Empty<ModelInputQualityChoice>(), e.Message);
            }
        }
        internal static HumanVisionSdkQualityCapabilities Failed(string profile, string error) =>
            new HumanVisionSdkQualityCapabilities(profile, "模型等级资源准备失败：" + error, Array.Empty<ModelInputQualityChoice>(), error);
    }
}
