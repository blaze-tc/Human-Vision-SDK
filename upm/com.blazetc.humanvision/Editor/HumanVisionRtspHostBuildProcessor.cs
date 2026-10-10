using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.NetworkInformation;
using HumanVision.Demo;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace HumanVision.Editor
{
    [InitializeOnLoad]
    public sealed class HumanVisionRtspHostBuildProcessor : IProcessSceneWithReport
    {
        public int callbackOrder => 100;
        static HumanVisionRtspHostBuildProcessor() { HumanVisionRtspComputerHost.EditorHostResolver = DetectHost; }
        public void OnProcessScene(Scene scene, BuildReport report)
        {
            if (report != null) Bake(scene, DetectHost());
        }
        public static void Bake(Scene scene, string host)
        {
            // 设置 Demo 同样包含电脑摄像头快捷入口；只在构建副本中写入地址。
            if (scene.name != "HumanVisionCameraDemo" && scene.name != "HumanVisionVideoDemo" &&
                scene.name != "HumanVisionRtspDemo" && scene.name != "HumanVisionSettingsDemo") return;
            var existing = scene.GetRootGameObjects().SelectMany(root => root.GetComponentsInChildren<HumanVisionRtspComputerHost>(true)).ToArray();
            if (existing.Length != 0) {
                existing[0].Configure(host);
                foreach (var duplicate in existing.Skip(1)) UnityEngine.Object.DestroyImmediate(duplicate);
                return;
            }
            var baked = new GameObject("HumanVision RTSP build computer", typeof(HumanVisionRtspComputerHost));
            SceneManager.MoveGameObjectToScene(baked, scene);
            baked.GetComponent<HumanVisionRtspComputerHost>().Configure(host);
        }
        public static bool IsEligibleInterface(NetworkInterfaceType type, string name, string description, bool up, string gateway)
        {
            if (!up || (type != NetworkInterfaceType.Ethernet && type != NetworkInterfaceType.Wireless80211) || !HumanVisionRtspComputerHost.IsPrivateIpv4(gateway)) return false;
            string identity = (name + " " + description).ToLowerInvariant();
            return !new[] { "virtual", "vpn", "tunnel", " tun", "tap", "loopback", "hyper-v", "vmware", "vbox", "docker", "meta" }.Any(identity.Contains);
        }
        public static string DetectHost()
        {
            // This enumerates only the local Editor's configured interfaces, never scans the LAN.
            var candidates = new List<Tuple<int, string, string>>();
            try {
                foreach (var nic in NetworkInterface.GetAllNetworkInterfaces()) {
                    var properties = nic.GetIPProperties();
                    bool eligible = properties.GatewayAddresses.Any(gateway => IsEligibleInterface(nic.NetworkInterfaceType, nic.Name, nic.Description,
                        nic.OperationalStatus == OperationalStatus.Up, gateway.Address.ToString()));
                    if (!eligible) continue;
                    foreach (var address in properties.UnicastAddresses) {
                        string host = address.Address.ToString();
                        if (HumanVisionRtspComputerHost.IsPrivateIpv4(host)) candidates.Add(Tuple.Create(nic.NetworkInterfaceType == NetworkInterfaceType.Wireless80211 ? 0 : 1, nic.Id, host));
                    }
                }
            } catch (NetworkInformationException) { return ""; }
            // Prefer a physical WLAN, then stable interface ID/address ordering. Override remains available.
            return candidates.OrderBy(item => item.Item1).ThenBy(item => item.Item2, StringComparer.Ordinal).ThenBy(item => item.Item3, StringComparer.Ordinal).Select(item => item.Item3).FirstOrDefault() ?? "";
        }
    }
}
