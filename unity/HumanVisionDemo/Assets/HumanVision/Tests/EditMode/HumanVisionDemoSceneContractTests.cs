using System.Linq;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class HumanVisionDemoSceneContractTests
    {
        private const string ScenePath = "Assets/Scenes/HumanVisionD04Demo.unity";

        [Test]
        public void DemoSceneContainsWiredPipelineAndIsEnabledForBuild()
        {
            SceneAsset sceneAsset = AssetDatabase.LoadAssetAtPath<SceneAsset>(ScenePath);
            Assert.That(sceneAsset, Is.Not.Null, "The D0.4 Demo scene must exist.");

            EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);

            HumanVisionManager manager = Object.FindObjectOfType<HumanVisionManager>();
            VideoPlayerFrameSource frameSource = Object.FindObjectOfType<VideoPlayerFrameSource>();
            HumanVisionOverlay overlay = Object.FindObjectOfType<HumanVisionOverlay>();
            HumanVisionHud hud = Object.FindObjectOfType<HumanVisionHud>();
            HumanVisionDemoBootstrap bootstrap = Object.FindObjectOfType<HumanVisionDemoBootstrap>();
            RawImage rawImage = Object.FindObjectOfType<RawImage>();
            AspectRatioFitter aspectRatioFitter = Object.FindObjectOfType<AspectRatioFitter>();

            Assert.That(manager, Is.Not.Null);
            Assert.That(frameSource, Is.Not.Null);
            Assert.That(overlay, Is.Not.Null);
            Assert.That(hud, Is.Not.Null);
            Assert.That(bootstrap, Is.Not.Null);
            Assert.That(rawImage, Is.Not.Null);
            Assert.That(aspectRatioFitter, Is.Not.Null);

            AssertReference(frameSource, "manager", manager);
            AssertReference(frameSource, "targetDisplay", rawImage);
            AssertReference(frameSource, "aspectRatioFitter", aspectRatioFitter);
            AssertReference(overlay, "manager", manager);
            AssertReference(overlay, "frameSource", frameSource);
            AssertReference(hud, "manager", manager);
            AssertReference(hud, "frameSource", frameSource);
            AssertReference(bootstrap, "manager", manager);
            AssertReference(bootstrap, "frameSource", frameSource);

            EditorBuildSettingsScene buildScene = EditorBuildSettings.scenes
                .SingleOrDefault(scene => scene.path == ScenePath);
            Assert.That(buildScene, Is.Not.Null, "The D0.4 Demo scene must be in Build Settings.");
            Assert.That(buildScene.enabled, Is.True, "The D0.4 Demo scene must be enabled.");
        }

        private static void AssertReference(
            Object owner,
            string propertyName,
            Object expected)
        {
            var serializedObject = new SerializedObject(owner);
            SerializedProperty property = serializedObject.FindProperty(propertyName);
            Assert.That(property, Is.Not.Null,
                $"Serialized property '{propertyName}' was not found on {owner.GetType().Name}.");
            Assert.That(property.objectReferenceValue, Is.SameAs(expected),
                $"{owner.GetType().Name}.{propertyName} is not wired correctly.");
        }
    }
}
