using System;
using System.Reflection;
using UnityEngine;
using UnityEngine.Experimental.Rendering;

namespace HumanVision.Input
{
    /// <summary>统一旧版 Unity 的纹理编码和新版 Unity 的内置字体差异。</summary>
    public static class HumanVisionUnityCompatibility
    {
        // 仅初始化一次开放实例委托，查询期间无反射、装箱或分配。
        // Gamma 项目的 graphicsFormat 可能是 UNorm，不能独自代表原始数据编码。
        private static readonly Func<Texture, bool> StoredEncoding = CreateEncodingReader();
        private static Func<Texture, bool> CreateEncodingReader()
        {
            var getter = typeof(Texture).GetProperty("isDataSRGB", BindingFlags.Public | BindingFlags.Instance)?.GetGetMethod();
            return getter == null ? null : (Func<Texture, bool>)Delegate.CreateDelegate(typeof(Func<Texture, bool>), getter);
        }
        /// <summary>读取数据编码；旧 Unity 无公开标记时使用调用方已知的编码。摄像头/视频默认 sRGB。</summary>
        public static bool IsSrgb(Texture texture, bool legacySrgb = true) => texture != null &&
            (StoredEncoding != null ? StoredEncoding(texture) : QualitySettings.activeColorSpace == ColorSpace.Gamma
                ? texture is RenderTexture rt ? rt.sRGB : legacySrgb
                : GraphicsFormatUtility.IsSRGBFormat(texture.graphicsFormat));
        /// <summary>Unity 2022.2 起内置字体改名，编译时选择当前引擎的资源名。</summary>
        public static Font DefaultFont => Resources.GetBuiltinResource<Font>(
#if UNITY_2022_2_OR_NEWER
            "LegacyRuntime.ttf"
#else
            "Arial.ttf"
#endif
        );
    }
}
