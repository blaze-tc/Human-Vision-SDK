using System;
using System.Collections;
using System.Runtime.InteropServices;
using UnityEngine;
namespace HumanVision.Input.Tests
{
    // Input-only bounded device fixture. No inference assembly or native SDK.
    public sealed class AndroidInputCapabilityProbe : MonoBehaviour
    {
        [DllImport("humanvision_input")] private static extern void HV_Input_SelectHardwareCodec(string name);
        [DllImport("humanvision_input")] private static extern void HV_Input_StartCapabilityProbe(string url);
        [DllImport("humanvision_input")] private static extern void HV_Input_StopCapabilityProbe();
        private IEnumerator Start()
        {
            yield return null;
#if UNITY_ANDROID && !UNITY_EDITOR
            try
            {
                using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                    if (version.GetStatic<int>("SDK_INT") < 29)
                        throw new InvalidOperationException("Hardware accelerated codec identity requires Android API29 device query; native remains API26.");
                string selected = null;
                using (var list = new AndroidJavaObject("android.media.MediaCodecList", 0))
                {
                    foreach (var codec in list.Call<AndroidJavaObject[]>("getCodecInfos"))
                    using (codec)
                    {
                        if (codec.Call<bool>("isEncoder") || !codec.Call<bool>("isHardwareAccelerated")) continue;
                        foreach (string type in codec.Call<string[]>("getSupportedTypes"))
                        {
                            if (type != "video/avc") continue;
                            using (var capability = codec.Call<AndroidJavaObject>("getCapabilitiesForType", type))
                            using (var video = capability.Call<AndroidJavaObject>("getVideoCapabilities"))
                            {
                                if (!video.Call<bool>("areSizeAndRateSupported", 640, 360, 25.0)) continue;
                                selected = codec.Call<string>("getName");
                                Debug.Log("HVInputGate selected_hardware_codec=" + selected + " supports_640x360_25=true");
                                break;
                            }
                        }
                        if (selected != null) break;
                    }
                }
                if (selected == null) throw new InvalidOperationException("No hardware AVC decoder supporting controlled fixture.");
                HV_Input_SelectHardwareCodec(selected);
                HV_Input_StartCapabilityProbe(Resources.Load<TextAsset>("input-gate-url").text.Trim());
            }
            catch (Exception exception) { Debug.LogError("HVInputGate capability_result=FAIL managed_stage=" + exception.GetType().Name); }
#else
            Debug.LogError("HVInputGate capability_result=FAIL requires_actual_Android_player");
#endif
            yield return new WaitForSeconds(15);
#if UNITY_ANDROID && !UNITY_EDITOR
            HV_Input_StopCapabilityProbe();
#endif
            Debug.Log("HVInputGate bounded_probe_closed=true");
        }
    }
}
