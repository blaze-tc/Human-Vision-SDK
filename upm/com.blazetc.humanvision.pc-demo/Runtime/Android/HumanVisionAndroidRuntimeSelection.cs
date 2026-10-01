using System;
using UnityEngine;

namespace HumanVision
{
    public static class HumanVisionAndroidRuntimeSelection
    {
        public const string RuntimeModeMetadataKey = "com.blazetc.humanvision.runtime_mode";
        public const string ProfileIdMetadataKey = "com.blazetc.humanvision.profile_id";

        internal static string ResolveProfile(string configuredProfile)
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            return ResolveConfiguredProfile(configuredProfile, ReadBakedProfileId());
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

#if UNITY_ANDROID && !UNITY_EDITOR
        private static string ReadBakedProfileId()
        {
            using (var player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using (var activity = player.GetStatic<AndroidJavaObject>("currentActivity"))
            using (var packageManager = activity.Call<AndroidJavaObject>("getPackageManager"))
            using (var applicationInfo = packageManager.Call<AndroidJavaObject>(
                       "getApplicationInfo", activity.Call<string>("getPackageName"), 128))
            using (var metadata = applicationInfo.Get<AndroidJavaObject>("metaData"))
            {
                if (metadata == null)
                    return null;
                return metadata.Call<string>("getString", ProfileIdMetadataKey);
            }
        }
#endif
    }
}
