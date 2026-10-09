using System;
using System.IO;
using System.Linq;
using HumanVision.Demo;
using UnityEngine;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>
    /// 当前实测项目的随包视频目录。构建器把 StreamingAssets 中真实存在的 MP4
    /// 生成一个 Resources 清单；Android 不调用 Directory.GetFiles 枚举 APK。
    /// 只注册播放 URL，视频仍由 SDK Input 的 VideoPlayer 打开和解码。
    /// </summary>
    [DefaultExecutionOrder(-450), DisallowMultipleComponent]
    public sealed class HumanVisionSettingsBundledVideos : MonoBehaviour
    {
        public const string ResourceName = "HumanVisionSettingsVideos";
        [Serializable] public sealed class Catalog { public Entry[] videos = Array.Empty<Entry>(); }
        [Serializable] public sealed class Entry { public string relativePath, sha256; public long bytes; }

        private void Awake()
        {
            var controller = GetComponent<HumanVisionSettingsController>();
            var text = Resources.Load<TextAsset>(ResourceName);
            if (controller == null || controller.View == null || text == null) {
                Debug.LogWarning("[HumanVisionSettingsDemo videos] 未找到随包视频清单或设置 View。", this); return;
            }
            try {
                var catalog = JsonUtility.FromJson<Catalog>(text.text);
                var paths = catalog.videos.Select(v => ResolvePath(Application.streamingAssetsPath, v.relativePath)).ToArray();
                // 早于 Controller.Start 注册，后续刷新列表依然保留这些真实构建条目。
                controller.View.SetBundledVideos(paths);
                foreach (var video in catalog.videos)
                    Debug.Log("[HumanVisionSettingsDemo videos] bundled=" + video.relativePath + "; bytes=" + video.bytes + "; sha256=" + video.sha256, this);
            } catch (Exception error) { Debug.LogException(error, this); }
        }
        /// <summary>Windows 使用普通文件路径，Android jar URL 的每个相对路径段单独编码。</summary>
        public static string ResolvePath(string root, string relative)
        {
            if (string.IsNullOrWhiteSpace(root) || string.IsNullOrWhiteSpace(relative) ||
                relative.Contains('\\') || relative.Contains(':') || relative.StartsWith("/") ||
                relative.Split('/').Any(p => p == ".." || p == "." || p.Length == 0))
                throw new ArgumentException("随包视频必须是 StreamingAssets 内的相对路径。");
            if (root.Contains("://") || root.StartsWith("jar:", StringComparison.OrdinalIgnoreCase))
                return root.TrimEnd('/') + "/" + string.Join("/", relative.Split('/').Select(Uri.EscapeDataString));
            return Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
        }
    }
}
