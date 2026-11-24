#if UNITY_EDITOR
using System;
using System.Linq;
using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class RuntimePrefabPresentationAcceptanceTests
    {
        [Test]
        public void DemoBuildsOneUguiCanvasWithAnInputEntryAndAFixedTemperatureFooter()
        {
            var type = AppDomain.CurrentDomain.GetAssemblies()
                .Select(assembly => assembly.GetType("HumanVisionAcceptance.RuntimePrefabAcceptance"))
                .FirstOrDefault(value => value != null);
            Assert.That(type, Is.Not.Null);
            const BindingFlags instance = BindingFlags.Instance | BindingFlags.NonPublic;
            var build = type.GetMethod("BuildAcceptanceUi", instance);
            Assert.That(build, Is.Not.Null, "The production Demo must create a UGUI presentation.");
            Assert.That(type.GetMethod("OnGUI", instance), Is.Null);
            var owner = new GameObject("Demo presentation test");
            try {
                var demo = owner.AddComponent(type);
                build.Invoke(demo, null);
                build.Invoke(demo, null);
                Assert.That(owner.GetComponentsInChildren<Canvas>(true).Length, Is.EqualTo(1));
                var buttons = owner.GetComponentsInChildren<Button>(true);
                Assert.That(buttons.Count(button => button.GetComponentInChildren<Text>(true)?.text == "输入连接"), Is.EqualTo(1));
                var footer = owner.GetComponentsInChildren<Text>(true).SingleOrDefault(label => label.name == "Device Temperatures");
                Assert.That(footer, Is.Not.Null);
                Assert.That(footer.transform.parent.GetComponent<ScrollRect>(), Is.Null);
                Assert.That(owner.GetComponentsInChildren<RawImage>(true).Count(image => image.name == "Live Source Texture"), Is.EqualTo(1));
            } finally {
                UnityEngine.Object.DestroyImmediate(owner);
            }
        }
    }
}
#endif
