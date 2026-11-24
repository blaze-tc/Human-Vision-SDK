using System;
using HumanVision;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVisionAcceptance
{
    internal sealed class RuntimePrefabSceneSkeleton : IDisposable
    {
        private const int Layer = RuntimePrefabRedrawRig.PreviewLayer;
        private const int JointCount = RuntimeSkeletonReader.JointCount;
        private const int SlotCount = RuntimeSkeletonReader.SlotCapacity;
        private const float BoneWidth = .0035f;
        private const float PointRadius = .004f;
        private const int PointSections = 8;
        private const int MaximumLongEdge = 1920;
        private const int MaximumShortEdge = 1080;
        private static readonly int[] Parents = ReadParents();

        private readonly Body[] bodies = new Body[SlotCount];
        private GameObject root;
        private Camera camera;
        private RenderTexture output;
        private Material material;
        private bool initialized;

        private sealed class Body
        {
            internal GameObject Root;
            internal readonly LineRenderer[] Joints = new LineRenderer[JointCount];
            internal readonly LineRenderer[] Bones = new LineRenderer[JointCount];
        }

        internal RenderTexture Output => camera != null && camera.enabled ? output : null;

        internal void Refresh(RuntimePrefabLiveSkeleton sample, int sourceWidth, int sourceHeight)
        {
            if (sample == null || sample.VisibleJoints == 0) {
                if (camera != null) camera.enabled = false;
                foreach (Body body in bodies) {
                    if (body != null && body.Root.activeSelf) body.Root.SetActive(false);
                }
                return;
            }

            if (!Initialize()) return;
            int width = sourceWidth > 0 && sourceHeight > 0 ? sourceWidth : 1280;
            int height = sourceWidth > 0 && sourceHeight > 0 ? sourceHeight : 720;
            float aspect = (float)width / height;
            bool targetReady = EnsureOutput(width, height);
            if (camera.aspect != aspect) camera.aspect = aspect;
            for (int slot = 0; slot < bodies.Length; slot++) RefreshBody(sample, slot, aspect);
            if (camera.enabled != targetReady) camera.enabled = targetReady;
        }

        private bool Initialize()
        {
            if (initialized) return root != null;
            initialized = true;
            var shader = Shader.Find("Sprites/Default");
            if (shader == null) {
                Debug.LogWarning("The live skeleton overlay requires Sprites/Default.");
                return false;
            }

            material = new Material(shader) { name = "Live Skeleton Lines" };
            root = CreateObject("Live Skeleton Scene", null);
            camera = CreateObject("Skeleton Camera", root.transform).AddComponent<Camera>();
            camera.enabled = false;
            camera.transform.localPosition = new Vector3(0f, 0f, -2f);
            camera.orthographic = true;
            camera.orthographicSize = .5f;
            camera.nearClipPlane = .05f;
            camera.farClipPlane = 5f;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.clear;
            camera.cullingMask = 1 << Layer;
            camera.useOcclusionCulling = false;
            camera.allowHDR = false;
            camera.allowMSAA = false;
            camera.stereoTargetEye = StereoTargetEyeMask.None;
            return true;
        }

        private void RefreshBody(RuntimePrefabLiveSkeleton sample, int slot, float aspect)
        {
            Body body = bodies[slot];
            bool active = sample.HasBody(slot);
            if (active && body == null) bodies[slot] = body = CreateBody(slot);
            if (body == null) return;
            if (body.Root.activeSelf != active) body.Root.SetActive(active);
            if (!active) return;

            for (int joint = 0; joint < JointCount; joint++) {
                LineRenderer marker = body.Joints[joint];
                bool valid = sample.TryGetPoint(slot, joint, out var point);
                if (marker.enabled != valid) marker.enabled = valid;
                if (valid) marker.transform.localPosition = new Vector3((point.x - .5f) * aspect, .5f - point.y, 0f);
            }
            for (int joint = 0; joint < JointCount; joint++) {
                int parent = Parents[joint];
                if (parent < 0) continue;
                LineRenderer bone = body.Bones[joint];
                LineRenderer from = body.Joints[parent];
                LineRenderer to = body.Joints[joint];
                Vector3 a = from.transform.localPosition;
                Vector3 b = to.transform.localPosition;
                bool valid = from.enabled && to.enabled && a != b;
                if (bone.enabled != valid) bone.enabled = valid;
                if (!valid) continue;
                bone.SetPosition(0, a);
                bone.SetPosition(1, b);
            }
        }

        private Body CreateBody(int slot)
        {
            var body = new Body { Root = CreateObject("Body " + (slot + 1), root.transform) };
            var joints = CreateObject("Joints", body.Root.transform).transform;
            var bones = CreateObject("Bones", body.Root.transform).transform;
            Color color = RuntimePrefabRedrawRig.GetSlotColor(slot);

            for (int joint = 0; joint < JointCount; joint++) {
                string name = ((HumanVisionCanonicalJointId)joint).ToString();
                var marker = CreateLine(name, joints, color, 2);
                marker.loop = true;
                marker.positionCount = PointSections;
                for (int segment = 0; segment < PointSections; segment++) {
                    float angle = Mathf.PI * 2 * segment / PointSections;
                    marker.SetPosition(segment, new Vector3(Mathf.Cos(angle) * PointRadius, Mathf.Sin(angle) * PointRadius));
                }
                body.Joints[joint] = marker;

                int parent = Parents[joint];
                if (parent < 0) continue;
                var bone = CreateLine(((HumanVisionCanonicalJointId)parent) + " - " + name, bones, color, 1);
                bone.positionCount = 2;
                bone.numCapVertices = 2;
                body.Bones[joint] = bone;
            }
            return body;
        }

        private LineRenderer CreateLine(string name, Transform parent, Color color, int order)
        {
            var line = CreateObject(name, parent).AddComponent<LineRenderer>();
            line.enabled = false;
            line.useWorldSpace = false;
            line.widthMultiplier = BoneWidth;
            line.sharedMaterial = material;
            line.startColor = line.endColor = color;
            line.shadowCastingMode = ShadowCastingMode.Off;
            line.receiveShadows = false;
            line.sortingOrder = order;
            return line;
        }

        private bool EnsureOutput(int sourceWidth, int sourceHeight)
        {
            float scale = Mathf.Min(1f, Mathf.Min(
                (float)MaximumLongEdge / Mathf.Max(sourceWidth, sourceHeight),
                (float)MaximumShortEdge / Mathf.Min(sourceWidth, sourceHeight)));
            int width = Mathf.Max(1, Mathf.RoundToInt(sourceWidth * scale));
            int height = Mathf.Max(1, Mathf.RoundToInt(sourceHeight * scale));
            if (output != null && output.width == width && output.height == height && output.IsCreated()) return true;

            ReleaseOutput();
            output = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32) {
                name = "Live Skeleton Overlay",
                useMipMap = false,
                autoGenerateMips = false,
                wrapMode = TextureWrapMode.Clamp,
                filterMode = FilterMode.Bilinear
            };
            if (!output.Create()) {
                ReleaseOutput();
                return false;
            }
            camera.targetTexture = output;
            return true;
        }

        private void ReleaseOutput()
        {
            if (camera != null) camera.targetTexture = null;
            if (output == null) return;
            output.Release();
            Destroy(output);
            output = null;
        }

        public void Dispose()
        {
            if (camera != null) camera.enabled = false;
            ReleaseOutput();
            Destroy(root);
            Destroy(material);
            root = null;
            camera = null;
            material = null;
        }

        private static GameObject CreateObject(string name, Transform parent)
        {
            var child = new GameObject(name) { layer = Layer };
            if (parent != null) child.transform.SetParent(parent, false);
            return child;
        }

        private static void Destroy(UnityEngine.Object value)
        {
            if (value == null) return;
            if (Application.isPlaying) UnityEngine.Object.Destroy(value);
            else UnityEngine.Object.DestroyImmediate(value);
        }

        private static int[] ReadParents()
        {
            var parents = new int[JointCount];
            for (int joint = 0; joint < parents.Length; joint++)
                parents[joint] = RuntimeSkeletonReader.TryGetParentJoint((HumanVisionCanonicalJointId)joint, out var parent)
                    ? (int)parent : -1;
            return parents;
        }
    }
}
