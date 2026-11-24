#if UNITY_EDITOR
using System;
using System.Linq;
using System.Reflection;
using HumanVision;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace HumanVision.Tests
{
    public sealed class RuntimePrefabSceneWiringTests
    {
        private const string ScenePath = "Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity";

        [Test]
        public void SceneContainsAssignedSdkViewAndSkeletonObjects()
        {
            var type = AppDomain.CurrentDomain.GetAssemblies()
                .Select(assembly => assembly.GetType("HumanVisionAcceptance.RuntimePrefabAcceptance"))
                .FirstOrDefault(value => value != null);
            Assert.That(type, Is.Not.Null);
            Assert.That(type.GetField("sceneRuntimeView", BindingFlags.Instance | BindingFlags.NonPublic), Is.Not.Null,
                "The Demo must reference the existing SDK instance in the scene.");
            var scene = SceneManager.GetSceneByPath(ScenePath);
            bool openedHere = !scene.isLoaded;
            if (openedHere)
                scene = EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Additive);
            try
            {
                var owner = scene.GetRootGameObjects().Select(root => root.GetComponent(type)).First(component => component != null);
                var data = new SerializedObject(owner);
                var runtime = data.FindProperty("sceneRuntimeView").objectReferenceValue as HumanVisionRuntimeView;
                Assert.That(runtime, Is.Not.Null);
                Assert.That(runtime.gameObject.scene, Is.EqualTo(scene));
                Assert.That(scene.GetRootGameObjects().SelectMany(root => root.GetComponentsInChildren<HumanVisionRuntimeView>(true)).Count(),
                    Is.EqualTo(1), "The Demo requires exactly one SDK view owner.");
                Assert.That(runtime.OffscreenCamera, Is.Not.Null);
                var camera = data.FindProperty("skeletonCamera").objectReferenceValue as Camera;
                Assert.That(camera, Is.Not.Null);
                Assert.That(camera.orthographic, Is.True);
                Assert.That(camera.enabled, Is.False);
                var bodies = data.FindProperty("skeletonBodies");
                Assert.That(bodies.arraySize, Is.EqualTo(8));
                for (int i = 0; i < bodies.arraySize; i++)
                {
                    var body = bodies.GetArrayElementAtIndex(i);
                    var root = body.FindPropertyRelative("root").objectReferenceValue as GameObject;
                    Assert.That(root, Is.Not.Null);
                    Assert.That(root.activeSelf, Is.False);
                    Assert.That(body.FindPropertyRelative("joints").arraySize, Is.Positive);
                    Assert.That(root.GetComponentsInChildren<LineRenderer>(true).Length, Is.GreaterThan(25));
                }
            }
            finally
            {
                if (openedHere)
                    EditorSceneManager.CloseScene(scene, true);
            }
        }
    }
}
#endif
