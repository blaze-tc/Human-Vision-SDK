#if UNITY_EDITOR
using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using HumanVision;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class RuntimePrefabSceneSkeletonTests
    {
        private const BindingFlags Members = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

        [Test]
        public void CameraDrawsSelectableBonesWithSourceCoordinatesAndReusesObjects()
        {
            Type rendererType = FindDemoType("RuntimePrefabSceneSkeleton");
            if (rendererType == null) throw new AssertionException("The Demo requires a scene skeleton camera renderer.");
            VerifyCameraRenderer(rendererType);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void VerifyCameraRenderer(Type rendererType)
        {
            object renderer = Activator.CreateInstance(rendererType, true);
            var root = new GameObject("Scene skeleton projection test");
            try {
                var createArguments = new object[] { root.transform, null };
                var sceneBodies = rendererType.GetMethod("CreateSceneObjects", BindingFlags.Static | BindingFlags.NonPublic)
                    .Invoke(null, createArguments);
                var camera = (Camera)createArguments[1];
                var bound = (bool)rendererType.GetMethod("Bind", Members).Invoke(renderer,
                    new[] { camera, sceneBodies, Shader.Find("Sprites/Default") });
                Assert.That(bound, Is.True);
                object sample = Activator.CreateInstance(FindDemoType("RuntimePrefabLiveSkeleton"), true);
                Type sampleType = sample.GetType();
                var points = (Vector2[,])sampleType.GetField("points", Members).GetValue(sample);
                var valid = (bool[,])sampleType.GetField("valid", Members).GetValue(sample);
                var visible = (bool[])sampleType.GetField("visible", Members).GetValue(sample);
                points[0, 0] = new Vector2(.25f, .2f);
                points[0, 1] = new Vector2(.65f, .8f);
                valid[0, 0] = valid[0, 1] = visible[0] = true;
                sampleType.GetProperty("VisibleJoints", Members).SetValue(sample, 2);
                sampleType.GetProperty("VisibleBodies", Members).SetValue(sample, 1);
                sampleType.GetProperty("VisibleBones", Members).SetValue(sample, 1);

                MethodInfo refresh = rendererType.GetMethod("Refresh", Members);
                var arguments = new object[] { sample, 1920, 1080 };
                refresh.Invoke(renderer, arguments);
                Transform body = root.transform.Find("Body 1");
                string parentName = ((HumanVisionCanonicalJointId)0).ToString();
                string jointName = ((HumanVisionCanonicalJointId)1).ToString();
                var point = body.Find("Joints/" + parentName).GetComponent<LineRenderer>();
                var bone = body.Find("Bones/" + parentName + " - " + jointName).GetComponent<LineRenderer>();

                Assert.That(bone.enabled, Is.True);
                Assert.That(point.enabled, Is.True);
                Assert.That(root.hideFlags & (HideFlags.HideInHierarchy | HideFlags.NotEditable), Is.EqualTo(HideFlags.None));
                Assert.That(bone.hideFlags & (HideFlags.HideInHierarchy | HideFlags.NotEditable), Is.EqualTo(HideFlags.None));
                Assert.That(camera.enabled, Is.True);
                Assert.That(camera.orthographic, Is.True);
                Assert.That(camera.backgroundColor.a, Is.Zero);
                Assert.That(camera.cullingMask, Is.EqualTo(1 << bone.gameObject.layer));
                var output = (RenderTexture)rendererType.GetProperty("Output", Members).GetValue(renderer);
                Assert.That(output, Is.SameAs(camera.targetTexture));
                Assert.That(output.width, Is.EqualTo(1920));
                Assert.That(output.height, Is.EqualTo(1080));
                AssertViewport(camera, point.transform.position, .25f, .8f);
                AssertViewport(camera, bone.transform.TransformPoint(bone.GetPosition(0)), .25f, .8f);
                AssertViewport(camera, bone.transform.TransformPoint(bone.GetPosition(1)), .65f, .2f);

                points[0, 0] = new Vector2(.35f, .4f);
                refresh.Invoke(renderer, arguments);
                Assert.That(body.Find("Bones/" + parentName + " - " + jointName).GetComponent<LineRenderer>(), Is.SameAs(bone));
                Assert.That(rendererType.GetProperty("Output", Members).GetValue(renderer), Is.SameAs(output));
                AssertViewport(camera, point.transform.position, .35f, .6f);

                sampleType.GetMethod("Clear", Members).Invoke(sample, null);
                refresh.Invoke(renderer, arguments);
                Assert.That(camera.enabled, Is.False);
                Assert.That(body.gameObject.activeSelf, Is.False);
                Assert.That(rendererType.GetProperty("Output", Members).GetValue(renderer), Is.Null);
            } finally {
                ((IDisposable)renderer).Dispose();
                UnityEngine.Object.DestroyImmediate(root);
            }
        }

        private static Type FindDemoType(string name) => AppDomain.CurrentDomain.GetAssemblies()
            .Select(a => a.GetType("HumanVisionAcceptance." + name))
            .FirstOrDefault(t => t != null);

        private static void AssertViewport(Camera camera, Vector3 position, float x, float y)
        {
            Vector3 projected = camera.WorldToViewportPoint(position);
            Assert.That(projected.x, Is.EqualTo(x).Within(.0001f));
            Assert.That(projected.y, Is.EqualTo(y).Within(.0001f));
        }
    }
}
#endif
