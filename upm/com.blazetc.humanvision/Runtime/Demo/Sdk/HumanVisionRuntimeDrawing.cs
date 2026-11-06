using System;
using System.Collections.Generic;
using HumanVision.Demo;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision
{
    // Joint and bone objects represent the pose; one mesh draws them to the private target.
    // The camera supplies the projection. It never joins the scene render loop or culls other objects.
    internal sealed class HumanVisionRuntimeDrawing : IDisposable
    {
        internal const int Capacity = 8;
        private const int JointCount = SkeletonImagePlane.JointCount;
        private sealed class Body
        {
            internal long Id;
            internal readonly GameObject Root;
            internal readonly Transform[] Points = new Transform[JointCount], Bones = new Transform[JointCount];

            internal Body(Transform parent, int slot)
            {
                Root = new GameObject("Body " + slot);
                Root.transform.SetParent(parent, false);
                for (int i = 0; i < JointCount; i++) {
                    string name = ((HumanVisionCanonicalJointId)i).ToString();
                    Points[i] = CreateChild(Root.transform, name);
                    if (SkeletonImagePlane.Parent(i) >= 0)
                        Bones[i] = CreateChild(Root.transform, "Bone to " + name);
                }
                Root.SetActive(false);
            }
        }

        private readonly Camera camera;
        private readonly Transform plane;
        private readonly Body[] bodies = new Body[Capacity];
        private readonly Mesh mesh;
        private readonly Material material;
        private readonly CommandBuffer commands = new CommandBuffer { name = "HumanVision offscreen pose" };
        private readonly List<Vector3> vertices = new List<Vector3>(Capacity * JointCount * 8);
        private readonly List<int> indices = new List<int>(Capacity * JointCount * 12);
        private float aspect;
        internal RenderTexture Target { get; private set; }

        internal HumanVisionRuntimeDrawing(Transform root, Camera offscreen)
        {
            camera = offscreen;
            camera.enabled = false;
            camera.cullingMask = 0;
            camera.orthographic = true;
            camera.orthographicSize = .5f;
            plane = CreateChild(root, "Skeleton Plane", true);
            mesh = new Mesh { name = "HumanVision pose batch" };
            mesh.MarkDynamic();
            material = new Material(Shader.Find("HumanVision/Skeleton")) { color = new Color(.12f, 1f, .35f, 1f) };
        }

        private static Transform CreateChild(Transform parent, string name, bool active = false)
        {
            var child = new GameObject(name);
            child.transform.SetParent(parent, false);
            child.SetActive(active);
            return child.transform;
        }

        internal bool SetSize(int width, int height)
        {
            if (Target != null && Target.IsCreated() && Target.width == width && Target.height == height) return false;
            ReleaseTarget();
            aspect = (float)width / height;
            Target = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32) {
                name = "HumanVision offscreen color and skeleton", useMipMap = false, autoGenerateMips = false
            };
            Target.Create();
            camera.targetTexture = Target;
            camera.aspect = aspect;
            return true;
        }

        internal long GetBodyId(int slot) => slot >= 0 && slot < Capacity ? bodies[slot]?.Id ?? 0 : 0;

        internal Transform GetJoint(int slot, int joint)
        {
            if (GetBodyId(slot) <= 0 || joint < 0 || joint >= JointCount) return null;
            var point = bodies[slot].Points[joint];
            return point.gameObject.activeSelf ? point : null;
        }

        internal bool SetBody(int slot, long id, HumanVisionCanonicalJoint[] joints)
        {
            var body = bodies[slot] ?? (bodies[slot] = new Body(plane, slot));
            bool changed = body.Id != id;
            body.Id = id;
            SetActive(body.Root, true);
            for (int i = 0; i < JointCount; i++) {
                var value = joints[i].Position;
                var point = body.Points[i];
                changed |= SetActive(point.gameObject, value.Valid);
                if (!value.Valid) continue;
                var local = new Vector3((value.Normalized.x - .5f) * aspect, .5f - value.Normalized.y, 0);
                if (!point.localPosition.Equals(local)) {
                    point.localPosition = local;
                    changed = true;
                }
            }
            if (!changed) return false;
            for (int i = 0; i < JointCount; i++) {
                var bone = body.Bones[i];
                if (bone == null) continue;
                var start = body.Points[SkeletonImagePlane.Parent(i)];
                var end = body.Points[i];
                var delta = end.localPosition - start.localPosition;
                bool visible = start.gameObject.activeSelf && end.gameObject.activeSelf && delta.sqrMagnitude > 1e-10f;
                changed |= SetActive(bone.gameObject, visible);
                if (!visible) continue;
                bone.localPosition = (start.localPosition + end.localPosition) * .5f;
                bone.localRotation = Quaternion.Euler(0, 0, Mathf.Atan2(delta.y, delta.x) * Mathf.Rad2Deg);
                bone.localScale = new Vector3(delta.magnitude, .004f, 1f);
            }
            return changed;
        }

        internal bool HideBody(int slot)
        {
            var body = bodies[slot];
            if (body == null || body.Id == 0) return false;
            body.Id = 0;
            return SetActive(body.Root, false);
        }

        private static bool SetActive(GameObject owner, bool visible)
        {
            if (owner.activeSelf == visible) return false;
            owner.SetActive(visible);
            return true;
        }

        internal void Draw(Texture source)
        {
            vertices.Clear(); indices.Clear();
            foreach (var body in bodies) {
                if (body == null || body.Id == 0) continue;
                for (int i = 0; i < JointCount; i++) {
                    var bone = body.Bones[i];
                    if (bone != null && bone.gameObject.activeSelf)
                        AddQuad(bone.localPosition, bone.localRotation * Vector3.right * (bone.localScale.x * .5f),
                            bone.localRotation * Vector3.up * (bone.localScale.y * .5f));
                    var point = body.Points[i];
                    if (point.gameObject.activeSelf)
                        AddQuad(point.localPosition, Vector3.right * .006f, Vector3.up * .006f);
                }
            }

            commands.Clear();
            commands.Blit(source, Target);
            if (vertices.Count > 0) {
                mesh.Clear();
                mesh.SetVertices(vertices);
                mesh.SetTriangles(indices, 0);
                commands.SetRenderTarget(Target);
                commands.SetViewport(new Rect(0, 0, Target.width, Target.height));
                // These vertices overlay a Blit in the same command buffer;
                // use its bottom-left viewport convention, without the extra
                // vertical inversion of a camera rendering to a target texture.
                commands.SetViewProjectionMatrices(camera.worldToCameraMatrix, GL.GetGPUProjectionMatrix(camera.projectionMatrix, false));
                commands.DrawMesh(mesh, plane.localToWorldMatrix, material, 0, 0);
            }
            Graphics.ExecuteCommandBuffer(commands);
        }

        private void AddQuad(Vector3 center, Vector3 x, Vector3 y)
        {
            int start = vertices.Count;
            vertices.Add(center - x - y); vertices.Add(center + x - y);
            vertices.Add(center + x + y); vertices.Add(center - x + y);
            indices.Add(start); indices.Add(start + 1); indices.Add(start + 2);
            indices.Add(start); indices.Add(start + 2); indices.Add(start + 3);
        }

        internal void Clear(bool release)
        {
            for (int i = 0; i < Capacity; i++) HideBody(i);
            if (release) ReleaseTarget();
            else if (Target != null && Target.IsCreated()) {
                commands.Clear(); commands.SetRenderTarget(Target);
                commands.ClearRenderTarget(false, true, Color.clear);
                Graphics.ExecuteCommandBuffer(commands);
            }
        }

        private void ReleaseTarget()
        {
            if (Target == null) return;
            if (camera != null) camera.targetTexture = null;
            // Unity owns retirement of queued render commands referencing this object.
            UnityEngine.Object.Destroy(Target);
            Target = null;
        }

        public void Dispose()
        {
            ReleaseTarget();
            commands.Dispose();
            UnityEngine.Object.Destroy(mesh);
            UnityEngine.Object.Destroy(material);
        }
    }
}
