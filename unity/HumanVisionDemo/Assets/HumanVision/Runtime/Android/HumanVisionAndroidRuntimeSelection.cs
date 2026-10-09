using System;
using UnityEngine;

namespace HumanVision
{
    public static class HumanVisionAndroidRuntimeSelection
    {
        public const string RuntimeModeMetadataKey = "com.blazetc.humanvision.runtime_mode";
        public const string ProfileIdMetadataKey = "com.blazetc.humanvision.profile_id";
        public const string QualityProfilesMetadataKey = "com.blazetc.humanvision.quality_profiles";

        internal static string ResolveProfile(string configuredProfile)
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            return ResolveBakedProfile(configuredProfile);
#else
            return configuredProfile;
#endif
        }

        public static string ResolveConfiguredProfile(string configuredProfile, string bakedProfile)
        {
            if (string.IsNullOrWhiteSpace(bakedProfile))
                throw new InvalidOperationException("HumanVision Android runtime metadata is missing '" + ProfileIdMetadataKey + "'. Rebuild the APK from Project Settings.");

            if (string.IsNullOrWhiteSpace(configuredProfile) || string.Equals(configuredProfile, "auto", StringComparison.OrdinalIgnoreCase))
                return bakedProfile;

            if (!string.Equals(configuredProfile, bakedProfile, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    "HumanVision Android runtime profile '" + configuredProfile + "' conflicts with baked profile '" + bakedProfile + "'.");
            return bakedProfile;
        }

        public static string ResolveConfiguredProfile(string configuredProfile, string bakedProfile, string runtimeMode, string[] admittedProfiles)
        {
            // Only legacy single-mode packages may omit their quality catalog.
            if (admittedProfiles == null || admittedProfiles.Length == 0) {
                if (runtimeMode == HumanVisionAndroidAccelerationSelection.DualMode)
                    throw new InvalidOperationException("HumanVision Android dual mode requires its exact five-profile allowlist and NCNN Vulkan baked default.");
                return ResolveConfiguredProfile(configuredProfile, bakedProfile);
            }
            const string mode = HumanVisionModelInputQualities.AdmittedRuntimeMode;
            bool dual = runtimeMode == "android-dual-vulkan-npu";
            var expected = new System.Collections.Generic.HashSet<string>(new[] { mode + "-quality-low", mode, mode + "-quality-high" }, StringComparer.Ordinal);
            if (dual) { expected.Add(HumanVisionAndroidAccelerationSelection.NeuralProfile); expected.Add(HumanVisionAndroidAccelerationSelection.CpuProfile); }
            var actual = new System.Collections.Generic.HashSet<string>(admittedProfiles, StringComparer.Ordinal);
            if ((!dual && runtimeMode != mode) || bakedProfile != mode || admittedProfiles.Length != expected.Count || actual.Count != expected.Count || !actual.SetEquals(expected))
                throw new InvalidOperationException("HumanVision Android baked quality profile allowlist does not match the selected runtime mode.");
            if (string.IsNullOrWhiteSpace(configuredProfile) || string.Equals(configuredProfile, "auto", StringComparison.OrdinalIgnoreCase)) return bakedProfile;
            if (!actual.Contains(configuredProfile))
                throw new InvalidOperationException("HumanVision Android runtime profile '" + configuredProfile + "' is not admitted by the baked same-mode quality allowlist.");
            return configuredProfile;
        }

        public static string[] ParseBakedQualityProfiles(string metadata)
        {
            if (string.IsNullOrEmpty(metadata)) return new string[0];
            try
            {
                var values = HumanVisionConfigurationJson.Array(HumanVisionConfigurationJson.Parse(metadata));
                var result = new string[values.Count];
                for (int i = 0; i < result.Length; i++) result[i] = values[i] as string ?? throw new System.IO.InvalidDataException("Expected quality profile string.");
                return result;
            }
            catch (Exception error) { throw new InvalidOperationException("HumanVision Android baked quality metadata is malformed.", error); }
        }

#if UNITY_ANDROID && !UNITY_EDITOR
        private static string ResolveBakedProfile(string configuredProfile)
        {
            using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
            using (var packageManager = activity.Call<AndroidJavaObject>("getPackageManager"))
            using (var applicationInfo = packageManager.Call<AndroidJavaObject>(
                       "getApplicationInfo", activity.Call<string>("getPackageName"), 128))
            using (var metadata = applicationInfo.Get<AndroidJavaObject>("metaData"))
            {
                if (metadata == null) return ResolveConfiguredProfile(configuredProfile, null);
                return ResolveConfiguredProfile(configuredProfile, metadata.Call<string>("getString", ProfileIdMetadataKey),
                    metadata.Call<string>("getString", RuntimeModeMetadataKey),
                    ParseBakedQualityProfiles(metadata.Call<string>("getString", QualityProfilesMetadataKey)));
            }
        }
#endif
    }
}
