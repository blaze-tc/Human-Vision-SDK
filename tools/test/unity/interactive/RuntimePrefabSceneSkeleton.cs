using System;
using HumanVision;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVisionAcceptance
{
    internal sealed class RuntimePrefabSceneSkeleton : IDisposable
    {
        private const int JointCount = RuntimeSkeletonReader.JointCount;
        private const int SlotCount = RuntimeSkeletonReader.SlotCapacity;
        private const int MaximumLongEdge = 1920;
        private const int MaximumShortEdge = 1080;
        private static readonly int[] parents = ReadParents();

        [Serializable]
        internal sealed class Body
        {
            [SerializeField] private GameObject root;
            [SerializeField] private LineRenderer[] joints;
            [SerializeField] private LineRenderer[] bones;

            internal GameObject Root => root;
            internal LineRenderer[] Joints => joints;
            internal LineRenderer[] Bones => bones;

            internal bool HasReferences()
            {
                if (root == null || joints == null || bones == null ||
                    joints.Length != JointCount || bones.Length != JointCount)
                    return false;

                for (int i = 0; i < JointCount; i++)
                {
                    if (joints[i] == null || (parents[i] >= 0 && bones[i] == null))
                        return false;
                }
                return true;
            }

#if UNITY_EDITOR
            internal Body(GameObject rootObject, LineRenderer[] jointLines, LineRenderer[] boneLines)
            {
                root = rootObject;
                joints = jointLines;
                bones = boneLines;
            }
#endif
        }

        private Camera skeletonCamera;
        private Body[] bodies;
        private Material material;
        private RenderTexture output;

        internal RenderTexture Output => skeletonCamera != null && skeletonCamera.enabled ? output : null;

        internal bool Bind(Camera camera, Body[] sceneBodies, Shader lineShader)
        {
            if (camera == null || lineShader == null || sceneBodies == null || sceneBodies.Length != SlotCount)
                return false;

            foreach (Body body in sceneBodies)
            {
                if (body == null || !body.HasReferences())
                    return false;
            }

            skeletonCamera = camera;
            bodies = sceneBodies;
            material = new Material(lineShader) { name = "Live Skeleton Lines" };
            foreach (Body body in bodies)
            {
                foreach (LineRenderer joint in body.Joints)
                    joint.sharedMaterial = material;
                foreach (LineRenderer bone in body.Bones)
                {
                    if (bone != null)
                        bone.sharedMaterial = material;
                }
            }
            Hide();
            return true;
        }

        internal void Refresh(RuntimePrefabLiveSkeleton sample, int sourceWidth, int sourceHeight)
        {
            if (skeletonCamera == null)
                return;

            if (sample == null || sample.VisibleJoints == 0)
            {
                Hide();
                return;
            }

            int width = sourceWidth > 0 ? sourceWidth : 1280;
            int height = sourceHeight > 0 ? sourceHeight : 720;
            float aspect = (float)width / height;
            bool targetReady = EnsureOutput(width, height);
            if (skeletonCamera.aspect != aspect)
                skeletonCamera.aspect = aspect;

            for (int slot = 0; slot < bodies.Length; slot++)
                UpdateBody(sample, slot, aspect);

            if (skeletonCamera.enabled != targetReady)
                skeletonCamera.enabled = targetReady;
        }

        private void UpdateBody(RuntimePrefabLiveSkeleton sample, int slot, float aspect)
        {
            Body body = bodies[slot];
            bool active = sample.HasBody(slot);
            if (body.Root.activeSelf != active)
                body.Root.SetActive(active);
            if (!active)
                return;

            for (int joint = 0; joint < JointCount; joint++)
            {
                LineRenderer marker = body.Joints[joint];
                bool valid = sample.TryGetPoint(slot, joint, out var point);
                if (marker.enabled != valid)
                    marker.enabled = valid;
                if (valid)
                    marker.transform.localPosition = new Vector3((point.x - .5f) * aspect, .5f - point.y, 0f);
            }

            for (int joint = 0; joint < JointCount; joint++)
            {
                int parent = parents[joint];
                if (parent < 0)
                    continue;

                LineRenderer bone = body.Bones[joint];
                LineRenderer from = body.Joints[parent];
                LineRenderer to = body.Joints[joint];
                Vector3 a = from.transform.localPosition;
                Vector3 b = to.transform.localPosition;
                bool valid = from.enabled && to.enabled && a != b;
                if (bone.enabled != valid)
                    bone.enabled = valid;
                if (!valid)
                    continue;

                bone.SetPosition(0, a);
                bone.SetPosition(1, b);
            }
        }

        private void Hide()
        {
            skeletonCamera.enabled = false;
            foreach (Body body in bodies)
            {
                if (body.Root != null && body.Root.activeSelf)
                    body.Root.SetActive(false);
            }
        }

        private bool EnsureOutput(int sourceWidth, int sourceHeight)
        {
            float scale = Mathf.Min(1f, Mathf.Min(
                (float)MaximumLongEdge / Mathf.Max(sourceWidth, sourceHeight),
                (float)MaximumShortEdge / Mathf.Min(sourceWidth, sourceHeight)));
            int width = Mathf.Max(1, Mathf.RoundToInt(sourceWidth * scale));
            int height = Mathf.Max(1, Mathf.RoundToInt(sourceHeight * scale));
            if (output != null && output.width == width && output.height == height && output.IsCreated())
                return true;

            ReleaseOutput();
            output = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32)
            {
                name = "Live Skeleton Overlay",
                wrapMode = TextureWrapMode.Clamp,
                filterMode = FilterMode.Bilinear
            };
            if (!output.Create())
            {
                ReleaseOutput();
                return false;
            }

            skeletonCamera.targetTexture = output;
            return true;
        }

        private void ReleaseOutput()
        {
            if (skeletonCamera != null)
                skeletonCamera.targetTexture = null;
            if (output == null)
                return;

            output.Release();
            DestroyResource(output);
            output = null;
        }

        public void Dispose()
        {
            if (skeletonCamera != null)
                Hide();
            ReleaseOutput();
            DestroyResource(material);
            skeletonCamera = null;
            bodies = null;
            material = null;
        }

        private static void DestroyResource(UnityEngine.Object resource)
        {
            if (resource == null)
                return;
            if (Application.isPlaying)
                UnityEngine.Object.Destroy(resource);
            else
                UnityEngine.Object.DestroyImmediate(resource);
        }

        private static int[] ReadParents()
        {
            var result = new int[JointCount];
            for (int i = 0; i < result.Length; i++)
            {
                result[i] = RuntimeSkeletonReader.TryGetParentJoint((HumanVisionCanonicalJointId)i, out var parent)
                    ? (int)parent : -1;
            }
            return result;
        }

#if UNITY_EDITOR
        internal static Body[] CreateSceneObjects(Transform root, out Camera camera)
        {
            camera = AddChild(root, "Skeleton Camera").AddComponent<Camera>();
            camera.enabled = false;
            camera.transform.localPosition = new Vector3(0f, 0f, -2f);
            camera.orthographic = true;
            camera.orthographicSize = .5f;
            camera.nearClipPlane = .05f;
            camera.farClipPlane = 5f;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.clear;
            camera.cullingMask = 1 << RuntimePrefabRedrawRig.PreviewLayer;
            camera.useOcclusionCulling = false;
            camera.allowHDR = false;
            camera.allowMSAA = false;
            camera.stereoTargetEye = StereoTargetEyeMask.None;

            var sceneBodies = new Body[SlotCount];
            for (int slot = 0; slot < sceneBodies.Length; slot++)
                sceneBodies[slot] = CreateSceneBody(root, slot);
            return sceneBodies;
        }

        private static Body CreateSceneBody(Transform root, int slot)
        {
            const float pointRadius = .004f;
            const int pointSections = 8;
            GameObject body = AddChild(root, "Body " + (slot + 1));
            Transform joints = AddChild(body.transform, "Joints").transform;
            Transform bones = AddChild(body.transform, "Bones").transform;
            var jointLines = new LineRenderer[JointCount];
            var boneLines = new LineRenderer[JointCount];
            Color color = RuntimePrefabRedrawRig.GetSlotColor(slot);

            for (int joint = 0; joint < JointCount; joint++)
            {
                var kind = (HumanVisionCanonicalJointId)joint;
                var marker = AddLine(joints, kind.ToString(), color, 2);
                marker.loop = true;
                marker.positionCount = pointSections;
                for (int segment = 0; segment < pointSections; segment++)
                {
                    float angle = Mathf.PI * 2 * segment / pointSections;
                    marker.SetPosition(segment, new Vector3(Mathf.Cos(angle) * pointRadius, Mathf.Sin(angle) * pointRadius));
                }
                jointLines[joint] = marker;

                int parent = parents[joint];
                if (parent < 0)
                    continue;
                var bone = AddLine(bones, (HumanVisionCanonicalJointId)parent + " - " + kind, color, 1);
                bone.positionCount = 2;
                bone.numCapVertices = 2;
                boneLines[joint] = bone;
            }
            body.SetActive(false);
            return new Body(body, jointLines, boneLines);
        }

        private static LineRenderer AddLine(Transform parent, string name, Color color, int order)
        {
            var line = AddChild(parent, name).AddComponent<LineRenderer>();
            line.enabled = false;
            line.useWorldSpace = false;
            line.widthMultiplier = .0035f;
            line.startColor = line.endColor = color;
            line.shadowCastingMode = ShadowCastingMode.Off;
            line.receiveShadows = false;
            line.sortingOrder = order;
            return line;
        }

        private static GameObject AddChild(Transform parent, string name)
        {
            var child = new GameObject(name) { layer = RuntimePrefabRedrawRig.PreviewLayer };
            child.transform.SetParent(parent, false);
            return child;
        }
#endif
    }
}
