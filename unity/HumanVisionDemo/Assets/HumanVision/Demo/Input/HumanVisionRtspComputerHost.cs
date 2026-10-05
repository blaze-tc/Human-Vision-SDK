using System;
using System.Net;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace HumanVision.Demo
{
    /// <summary>Build-computer LAN address serialized into transient official Demo build scenes.</summary>
    [DisallowMultipleComponent]
    public sealed class HumanVisionRtspComputerHost : MonoBehaviour
    {
        [SerializeField] private string computerHost = "";
        public string ComputerHost => computerHost;
#if UNITY_EDITOR
        // Assigned by the Editor hook once; never enumerate phone/player interfaces.
        public static Func<string> EditorHostResolver;
#endif
        public void Configure(string host)
        {
            if (!string.IsNullOrEmpty(host)) ValidateHost(host);
            computerHost = host ?? "";
        }
        public static string Resolve(Scene scene, string manualHost)
        {
            if (!string.IsNullOrWhiteSpace(manualHost)) { ValidateHost(manualHost); return manualHost; }
            foreach (var root in scene.GetRootGameObjects()) {
                var component = root.GetComponentInChildren<HumanVisionRtspComputerHost>(true);
                if (component != null) { ValidateHost(component.ComputerHost); return component.ComputerHost; }
            }
            throw new ArgumentException("Build-computer LAN IPv4 is unavailable. Enter the Happytime computer LAN IPv4 in Computer host, or rebuild on that computer.");
        }
        public static string BuildUrl(string host, bool camera)
        {
            ValidateHost(host);
            return "rtsp://" + host + ":554/" + (camera ? "videodevice" : "video-1.mp4");
        }
        public static bool IsPrivateIpv4(string host)
        {
            if (string.IsNullOrEmpty(host) || !IPAddress.TryParse(host, out var address) || address.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork || address.ToString() != host) return false;
            var bytes = address.GetAddressBytes();
            return bytes[0] == 10 || (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) || (bytes[0] == 192 && bytes[1] == 168);
        }
        private static void ValidateHost(string host)
        {
            if (!IsPrivateIpv4(host)) throw new ArgumentException("Computer host must be a private LAN IPv4 address (for example 192.168.x.x), without a port or path. Use the manual RTSP URL field for other hosts.");
        }
    }
}
