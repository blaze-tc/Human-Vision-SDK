#if UNITY_EDITOR
using System;
using System.Linq;
using System.Reflection;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class RuntimePrefabPresentationAcceptanceTests
    {
        private const string ScenePath = "Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity";

        [Test]
        public void SceneContainsOneAuthoredCanvasAndRequiredInspectorReferences()
        {
            var demoType = AppDomain.CurrentDomain.GetAssemblies()
                .Select(assembly => assembly.GetType("HumanVisionAcceptance.RuntimePrefabAcceptance"))
                .FirstOrDefault(type => type != null);
            Assert.That(demoType, Is.Not.Null);
            const BindingFlags instance = BindingFlags.Instance | BindingFlags.NonPublic;
            Assert.That(demoType.GetMethod("BindAcceptanceUi", instance), Is.Not.Null);
            Assert.That(demoType.GetMethod("BuildAcceptanceUi", instance), Is.Null);
            Assert.That(demoType.GetMethod("OnGUI", instance), Is.Null);

            var scene = SceneManager.GetSceneByPath(ScenePath);
            bool openedForTest = !scene.IsValid() || !scene.isLoaded;
            if (openedForTest)
                scene = EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Additive);

            try
            {
                var owner = scene.GetRootGameObjects()
                    .Select(root => root.GetComponent(demoType)).Single(component => component != null);
                var references = new SerializedObject(owner);
                string[] requiredFields =
                {
                    "demoCanvas", "eventSystem", "safeUi", "connectionButton", "previewViewport",
                    "sourceImage", "sceneSkeletonImage", "deviceTemperatures", "logScroll"
                };
                foreach (string field in requiredFields)
                {
                    var property = references.FindProperty(field);
                    Assert.That(property, Is.Not.Null, field + " must be serialized for Inspector assignment.");
                    Assert.That(property.objectReferenceValue, Is.Not.Null, field + " must be assigned in the scene.");
                }

                Assert.That(owner.GetComponentsInChildren<Canvas>(true).Length, Is.EqualTo(1));
                var connection = (Button)references.FindProperty("connectionButton").objectReferenceValue;
                Assert.That(connection.gameObject.name, Is.EqualTo("输入连接"));
                var footer = (Text)references.FindProperty("deviceTemperatures").objectReferenceValue;
                Assert.That(footer.name, Is.EqualTo("Device Temperatures"));
                Assert.That(footer.transform.IsChildOf(((ScrollRect)references.FindProperty("logScroll").objectReferenceValue).transform), Is.False);
                Assert.That(owner.GetComponentsInChildren<RawImage>(true).Count(image => image.name == "Live Source Texture"), Is.EqualTo(1));
            }
            finally
            {
                if (openedForTest)
                    EditorSceneManager.CloseScene(scene, true);
            }
        }
    }
}
#endif
