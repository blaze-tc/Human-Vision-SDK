#if UNITY_EDITOR
using System;
using System.Linq;
using System.Reflection;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class RuntimePrefabConnectionUiTests
    {
        [Test]
        public void ConnectionTabsKeepTheirEditsAndCancelLeavesSettingsUntouched()
        {
            var type = AppDomain.CurrentDomain.GetAssemblies()
                .Select(assembly => assembly.GetType("HumanVisionAcceptance.RuntimePrefabAcceptance"))
                .First(value => value != null);
            const BindingFlags fields = BindingFlags.Instance | BindingFlags.NonPublic;
            var build = type.GetMethod("BuildConnectionUi", fields);
            var open = type.GetMethod("OpenConnectionUi", fields);
            Assert.That(build, Is.Not.Null, "The acceptance demo needs a UGUI connection dialog");
            Assert.That(open, Is.Not.Null);
            var root = new GameObject("Connection test", typeof(RectTransform));
            try {
                var owner = root.AddComponent(type);
                var settingsType = type.GetNestedType("InteractiveSettings", BindingFlags.NonPublic);
                object settings = Activator.CreateInstance(settingsType, true);
                var original = new HumanVisionSettingsData { SourceKind = InputKind.Rtsp };
                original.Rtsp.RtspUrl = "rtsp://example.invalid:554/old";
                original.Camera.RequestedFramesPerSecond = 25;
                settingsType.GetField("Data").SetValue(settings, original);
                type.GetField("interactive", fields).SetValue(owner, settings);

                build.Invoke(owner, new object[] { (RectTransform)root.transform });
                open.Invoke(owner, null);
                var dialog = Find(root.transform, "输入连接弹窗");
                Assert.That(dialog.gameObject.activeInHierarchy, Is.True);
                var rtsp = Find(dialog, "RTSP 配置");
                var camera = Find(dialog, "本地相机配置");
                Assert.That(rtsp.gameObject.activeSelf, Is.True);
                Assert.That(camera.gameObject.activeSelf, Is.False);

                var address = rtsp.GetComponentsInChildren<InputField>(true)
                    .Single(field => field.contentType == InputField.ContentType.Password);
                address.text = "rtsp://example.invalid:8554/new";
                Click(dialog, "本地相机");
                Assert.That(rtsp.gameObject.activeSelf, Is.False);
                Assert.That(camera.gameObject.activeSelf, Is.True);
                Assert.That(camera.GetComponentInChildren<Dropdown>(), Is.Not.Null);
                var frameRate = Find(camera, "请求帧率").GetComponentInChildren<InputField>();
                Assert.That(frameRate.text, Is.EqualTo("25"));
                frameRate.text = "60";

                Click(dialog, "RTSP");
                Assert.That(address.text, Is.EqualTo("rtsp://example.invalid:8554/new"));
                Click(dialog, "本地相机");
                Assert.That(frameRate.text, Is.EqualTo("60"));
                Click(dialog, "取消");
                Assert.That(dialog.gameObject.activeSelf, Is.False);
                Assert.That(original.SourceKind, Is.EqualTo(InputKind.Rtsp));
                Assert.That(original.Rtsp.RtspUrl, Is.EqualTo("rtsp://example.invalid:554/old"));
                Assert.That(original.Camera.RequestedFramesPerSecond, Is.EqualTo(25));
            } finally {
                UnityEngine.Object.DestroyImmediate(root);
            }
        }

        private static Transform Find(Transform root, string name) =>
            root.GetComponentsInChildren<Transform>(true).Single(item => item.name == name);

        private static void Click(Transform root, string caption) =>
            root.GetComponentsInChildren<Button>(true)
                .Single(button => button.GetComponentInChildren<Text>(true).text == caption).onClick.Invoke();
    }
}
#endif
