using System;
using UnityEngine;

namespace HumanVision.Input
{
    /// <summary>Platform decoder qualification shared by Unity-compatible and autonomous input.</summary>
    public static class HardwareVideoDecoderSelection
    {
        /// <summary>Returns a qualified hardware AVC decoder. Does not open a stream or require Unity graphics.</summary>
        public static string SelectAvcCodec()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            using (var version = new AndroidJavaClass("android.os.Build$VERSION"))
                if (version.GetStatic<int>("SDK_INT") < 29) throw new InvalidOperationException("This Android hardware codec qualification requires API29 device capability query; native ABI remains API26.");
            using (var list = new AndroidJavaObject("android.media.MediaCodecList", 0))
                foreach (var codec in list.Call<AndroidJavaObject[]>("getCodecInfos"))
                using (codec) {
                    if (codec.Call<bool>("isEncoder") || !codec.Call<bool>("isHardwareAccelerated")) continue;
                    foreach (string type in codec.Call<string[]>("getSupportedTypes"))
                        if (type == "video/avc") return codec.Call<string>("getName");
                }
            throw new InvalidOperationException("No hardware H.264 decoder is available; software fallback is disabled.");
#else
            return null;
#endif
        }
    }
}
