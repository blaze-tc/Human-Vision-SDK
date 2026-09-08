using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;

namespace HumanVision.Editor
{
    public sealed class HumanVisionAndroidBuildSettings : IPreprocessBuildWithReport
    {
        public int callbackOrder => 0;
        public void OnPreprocessBuild(BuildReport report)
        {
            if (report.summary.platform != BuildTarget.Android) return;
            if (PlayerSettings.Android.targetArchitectures != AndroidArchitecture.ARM64)
                throw new BuildFailedException("HumanVision SDK contains Android ARM64 libraries. Select IL2CPP and ARM64 only in Player Settings.");
            if ((int)PlayerSettings.Android.minSdkVersion < 24)
                throw new BuildFailedException("HumanVision live camera SDK requires Android API level 24 or newer.");
        }
    }
}
