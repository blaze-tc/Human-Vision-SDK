using System;
using System.Collections.Generic;
using System.Linq;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class HumanVisionPcDemoBuilderTests
    {
        [Test]
        public void CreatingTwoPcScenesPreservesUnsavedSceneAndWiresIndependentPreviewOverlay()
        {
            var originalBuildScenes = EditorBuildSettings.scenes;
            Scene originalActive = SceneManager.GetActiveScene();
            var created = new List<string>();
            Scene unsaved = originalActive;
            var sentinel = new GameObject("Unsaved scene sentinel");
            SceneManager.MoveGameObjectToScene(sentinel, unsaved);
            EditorSceneManager.MarkSceneDirty(unsaved);
            try {
                Type builder = typeof(HumanVision.Demo.Editor.HumanVisionDemoSceneBuilder).Assembly
                    .GetType("HumanVision.Demo.Editor.HumanVisionPcDemoBuilder");
                Assert.That(builder, Is.Not.Null, "PC scene menu builder is missing.");
                for (int i = 0; i < 2; i++) {
                    string path = (string)builder.GetMethod("CreateScene").Invoke(null, null);
                    created.Add(path);
                    Scene scene = SceneManager.GetSceneByPath(path);
                    Assert.That(scene.IsValid(), Is.True);
                    Assert.That(unsaved.IsValid() && unsaved.isDirty && sentinel != null, Is.True);
                    var roots = scene.GetRootGameObjects();
                    var manager = roots.SelectMany(x => x.GetComponentsInChildren<HumanVisionManager>()).Single();
                    var bridge = roots.SelectMany(x => x.GetComponentsInChildren<VideoPlayerFrameSource>()).Single();
                    var image = roots.SelectMany(x => x.GetComponentsInChildren<RawImage>()).Single();
                    var overlay = roots.SelectMany(x => x.GetComponentsInChildren<HumanVisionOverlay>()).Single();
                    var camera = roots.SelectMany(x => x.GetComponentsInChildren<Camera>()).Single();
                    Assert.That(camera.enabled, Is.True);
                    Assert.That(image.GetComponent<AspectRatioFitter>().aspectMode, Is.EqualTo(AspectRatioFitter.AspectMode.FitInParent));
                    Assert.That(new SerializedObject(bridge).FindProperty("manager").objectReferenceValue, Is.EqualTo(manager));
                    Assert.That(new SerializedObject(bridge).FindProperty("targetDisplay").objectReferenceValue, Is.EqualTo(image));
                    Assert.That(new SerializedObject(bridge).FindProperty("useRealtimeVideoTimestamps").boolValue, Is.True);
                    var drawing = new SerializedObject(overlay);
                    Assert.That(drawing.FindProperty("frameSource").objectReferenceValue, Is.EqualTo(bridge));
                    Assert.That(drawing.FindProperty("boneThickness").floatValue, Is.EqualTo(3));
                    Assert.That(drawing.FindProperty("jointSize").floatValue, Is.EqualTo(5));
                    Assert.That(image.gameObject, Is.Not.EqualTo(overlay.gameObject));
                    Assert.That(EditorBuildSettings.scenes.Any(x => x.path == path && x.enabled), Is.True);
                }
                Assert.That(created[0], Is.Not.EqualTo(created[1]), "A second creation must not overwrite the first scene.");
            } finally {
                foreach (string path in created) {
                    Scene scene = SceneManager.GetSceneByPath(path);
                    if (scene.IsValid()) EditorSceneManager.CloseScene(scene, true);
                    AssetDatabase.DeleteAsset(path);
                }
                if (sentinel != null) UnityEngine.Object.DestroyImmediate(sentinel);
                EditorBuildSettings.scenes = originalBuildScenes;
                if (originalActive.IsValid()) SceneManager.SetActiveScene(originalActive);
            }
        }
    }
}
