using System;
using UnityEngine;

namespace HumanVision
{
    // Configuration-time admission. No GPU name heuristics and no silent fallback.
    internal static class HumanVisionAndroidAccelerationSelection
    {
        internal const string DualMode = "android-dual-vulkan-npu";
        internal const string NeuralProfile = "android-rknn-npu-quality-low";
        internal const string CpuProfile = "android-cpu-nohands";
        internal static bool SupportsNeuralDevice(string soc, string hardware)
        {
            return IsSupportedSoc(soc) || IsSupportedSoc(hardware);
        }
        private static bool IsSupportedSoc(string value)
        {
            switch ((value ?? "").Trim().ToLowerInvariant()) {
                case "rk3588": case "rk3588s": case "rk3588_t": return true;
                default: return false;
            }
        }
        internal static string Resolve(HumanVisionAccelerationMode mode, ModelInputQuality quality,
            string bakedProfile, string bakedMode, string[] admitted, string soc, string hardware)
        {
            if (!Enum.IsDefined(typeof(HumanVisionAccelerationMode), mode)) throw new ArgumentException("Invalid acceleration preference.");
            if (!Enum.IsDefined(typeof(ModelInputQuality), quality)) throw new ArgumentException("Invalid model quality.");
            if (mode == HumanVisionAccelerationMode.Graphics)
                return HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", bakedProfile, bakedMode, admitted);
            if (mode == HumanVisionAccelerationMode.Cpu)
                return HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(CpuProfile, bakedProfile, bakedMode, admitted);
            // Validate the exact baked map even if the requested profile occurs in a forged partial map.
            HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(NeuralProfile, bakedProfile, bakedMode, admitted);
            if (bakedMode != DualMode) throw new InvalidOperationException("RK3588 NPU is not installed in this APK. Build android-dual-vulkan-npu.");
            if (!SupportsNeuralDevice(soc, hardware))
                throw new InvalidOperationException("RK3588 NPU requires RK3588 / RK3588S / RK3588_T; this device reports ro.soc.model='" + soc + "', ro.hardware='" + hardware + "'. Current session is retained.");
            if (quality != ModelInputQuality.Low)
                throw new InvalidOperationException("RK3588 NPU has only the validated Low 512×288 model. Medium / High are unavailable.");
            return NeuralProfile;
        }
        internal static string ResolveInstalled(HumanVisionAccelerationMode mode, ModelInputQuality quality, bool checkDevice = true)
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
            using (var packageManager = activity.Call<AndroidJavaObject>("getPackageManager"))
            using (var applicationInfo = packageManager.Call<AndroidJavaObject>("getApplicationInfo", activity.Call<string>("getPackageName"), 128))
            using (var metadata = applicationInfo.Get<AndroidJavaObject>("metaData")) {
                if (metadata == null) throw new InvalidOperationException("Android runtime metadata is missing.");
                string soc = "rk3588", hardware = "";
                if (mode == HumanVisionAccelerationMode.Neural && checkDevice) ReadNeuralIdentity(out soc, out hardware);
                return Resolve(mode, quality, metadata.Call<string>("getString", HumanVisionAndroidRuntimeSelection.ProfileIdMetadataKey),
                    metadata.Call<string>("getString", HumanVisionAndroidRuntimeSelection.RuntimeModeMetadataKey),
                    HumanVisionAndroidRuntimeSelection.ParseBakedQualityProfiles(metadata.Call<string>("getString", HumanVisionAndroidRuntimeSelection.QualityProfilesMetadataKey)),
                    soc, hardware);
            }
#else
            if (mode == HumanVisionAccelerationMode.Neural) throw new InvalidOperationException("Neural acceleration is available only in an admitted Android build.");
            return HumanVisionAndroidRuntimeSelection.ResolveProfile("auto");
#endif
        }
#if UNITY_ANDROID && !UNITY_EDITOR
        private static void ReadNeuralIdentity(out string soc, out string hardware)
        {
            soc = hardware = "";
            try {
                using(var build=new AndroidJavaClass("android.os.Build")) using(var version=new AndroidJavaClass("android.os.Build$VERSION")) {
                    if(version.GetStatic<int>("SDK_INT")>=31) soc=build.GetStatic<string>("SOC_MODEL")??"";
                    hardware=build.GetStatic<string>("HARDWARE")??"";
                    string board=build.GetStatic<string>("BOARD")??"";
                    if(!SupportsNeuralDevice(soc,hardware)&&IsSupportedSoc(board)) hardware=board;
                }
            } catch(Exception) { /* An unavailable identity remains unsupported. */ }
            if(SupportsNeuralDevice(soc,hardware)) return;
            // Older Rockchip Android images can expose identity only through these properties.
            // This optional lookup is Neural-only and never a dependency of Graphics/CPU startup.
            try {
                using(var properties=new AndroidJavaClass("android.os.SystemProperties")) {
                    string model=properties.CallStatic<string>("get","ro.soc.model","");
                    string platform=properties.CallStatic<string>("get","ro.board.platform","");
                    string observedHardware=properties.CallStatic<string>("get","ro.hardware","");
                    if(IsSupportedSoc(model)) soc=model;
                    if(IsSupportedSoc(platform)) hardware=platform;
                    else if(IsSupportedSoc(observedHardware)) hardware=observedHardware;
                    else if(string.IsNullOrEmpty(hardware)) hardware=observedHardware??"";
                    if(string.IsNullOrEmpty(soc)) soc=model??"";
                }
            } catch(Exception) { /* Hidden API restrictions reject Neural admission without breaking other modes. */ }
        }
#endif
    }
}
