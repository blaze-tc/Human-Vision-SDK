using HumanVision;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.UI;

namespace HumanVisionAcceptance
{
    // A game-owned presentation rig. This component has no SDK session or frame-source access.
    public sealed class RuntimePrefabRedrawRig : MonoBehaviour
    {
        public const int PreviewLayer = 29;
        [SerializeField] private Camera redrawCamera;
        [SerializeField] private RawImage matchedColor;
        [SerializeField] private Transform skeletonObjects;
        [SerializeField] private GameObject jointPrefab;
        [SerializeField] private LineRenderer linePrefab;

        private static readonly Color[] TrackColors = { new Color(.22f,.91f,1), new Color(1,.72f,.24f), new Color(.84f,.60f,1), new Color(.42f,1,.64f) };
        private readonly Body[] pool = new Body[8];
        private Mesh markerMesh;
        private RenderTexture output;
        private float aspect = 16f / 9f;

        private sealed class Body
        {
            internal GameObject Root;
            internal Material Material;
            internal long TrackId;
            internal readonly Transform[] Points = new Transform[RuntimeSkeletonReader.JointCount];
            internal readonly LineRenderer[] Lines = new LineRenderer[RuntimeSkeletonReader.JointCount];
        }

        internal static Color GetSlotColor(int slot) => TrackColors[slot % TrackColors.Length];

        public RenderTexture Output => output;
        public int VisibleLines { get; private set; }

        // The editor creates and saves this rig. No prefab internals are modified.
        public void Configure(Camera camera, RawImage color, Transform objects)
        {
            redrawCamera = camera;
            matchedColor = color;
            skeletonObjects = objects;
        }

        public void Draw(Texture color, long[] ids, Vector2[,] viewport, bool[,] valid)
        {
            VisibleLines = 0;
            if (color == null) {
                matchedColor.texture = null;
                matchedColor.enabled = false;
                redrawCamera.enabled = false;
                foreach (var body in pool)
                    if (body != null && body.Root.activeSelf) body.Root.SetActive(false);
                return;
            }
            EnsureOutput(color.width, color.height);
            matchedColor.texture = color;
            matchedColor.enabled = true;
            redrawCamera.enabled = true;
            for (int slot = 0; slot < pool.Length; slot++) {
                var body = pool[slot];
                if (ids[slot] <= 0) {
                    if (body != null && body.Root.activeSelf) body.Root.SetActive(false);
                    continue;
                }
                if (body == null) pool[slot] = body = CreateBody(slot);
                if (!body.Root.activeSelf) body.Root.SetActive(true);
                if (body.TrackId != ids[slot]) {
                    body.TrackId = ids[slot];
                    body.Root.name = $"Region {slot}  -  Track ID {body.TrackId}";
                }
                for (int j = 0; j < RuntimeSkeletonReader.JointCount; j++) {
                    var point = body.Points[j];
                    if (point.gameObject.activeSelf != valid[slot, j]) point.gameObject.SetActive(valid[slot, j]);
                    if (!valid[slot, j]) continue;
                    Vector2 p = viewport[slot, j];
                    var next = new Vector3((p.x - .5f) * aspect, p.y - .5f, 0);
                    if (!point.localPosition.Equals(next)) point.localPosition = next;
                }
                for (int j = 0; j < RuntimeSkeletonReader.JointCount; j++) {
                    if (!RuntimeSkeletonReader.TryGetParentJoint((HumanVisionCanonicalJointId)j, out var parentJoint)) continue;
                    int parent = (int)parentJoint;
                    var line = body.Lines[j];
                    Vector3 a = body.Points[parent].localPosition;
                    Vector3 b = body.Points[j].localPosition;
                    bool visible = valid[slot, parent] && valid[slot, j] && (a - b).sqrMagnitude > 1e-8f;
                    if (line.enabled != visible) line.enabled = visible;
                    if (!visible) continue;
                    a.z = b.z = .005f;
                    line.SetPosition(0, a);
                    line.SetPosition(1, b);
                    VisibleLines++;
                }
            }
        }

        private void EnsureOutput(int width, int height)
        {
            if (output != null && output.width == width && output.height == height && output.IsCreated()) return;
            ReleaseOutput();
            aspect = (float)width / height;
            // The canvas is world space: the RawImage is the background of the
            // same camera that sees the MeshRenderer and LineRenderer objects.
            var canvas = (RectTransform)matchedColor.transform.parent;
            canvas.sizeDelta = new Vector2(aspect * 1000, 1000);
            redrawCamera.aspect = aspect;
            output = new RenderTexture(width, height, 24, RenderTextureFormat.ARGB32) {
                name = "Demo Camera - Matched RGB and Bones",
                useMipMap = false,
                autoGenerateMips = false
            };
            output.Create();
            redrawCamera.targetTexture = output;
        }

        private Body CreateBody(int slot)
        {
            if (markerMesh == null) markerMesh = BuildMarker();
            var body = new Body {
                Root = Child(skeletonObjects, "Slot " + slot),
                Material = new Material(Shader.Find("Sprites/Default")) { name = "Demo Skeleton - Slot " + slot, color = GetSlotColor(slot) }
            };
            var joints = Child(body.Root.transform, "Joint Meshes").transform;
            // Lines live directly below this identity transform in the same plane.
            var bones = Child(body.Root.transform, "Bone LineRenderers").transform;
            for (int i = 0; i < RuntimeSkeletonReader.JointCount; i++) {
                string jointName = ((HumanVisionCanonicalJointId)i).ToString();
                var point = jointPrefab == null ? Child(joints, jointName) : Instantiate(jointPrefab, joints, false);
                point.name = jointName;
                SetPreviewLayer(point.transform);
                if (jointPrefab == null) {
                    point.AddComponent<MeshFilter>().sharedMesh = markerMesh;
                    var pointRenderer = point.AddComponent<MeshRenderer>();
                    pointRenderer.sharedMaterial = body.Material;
                    pointRenderer.shadowCastingMode = ShadowCastingMode.Off;
                    pointRenderer.receiveShadows = false;
                    pointRenderer.sortingOrder = 2;
                }
                body.Points[i] = point.transform;
                if (!RuntimeSkeletonReader.TryGetParentJoint((HumanVisionCanonicalJointId)i, out var parent)) continue;
                string boneName = $"{parent} - {jointName}";
                var line = linePrefab == null ? Child(bones, boneName).AddComponent<LineRenderer>() : Instantiate(linePrefab, bones, false);
                line.name = boneName;
                SetPreviewLayer(line.transform);
                line.useWorldSpace = false;
                line.positionCount = 2;
                line.widthMultiplier = .0045f;
                line.numCapVertices = 2;
                if (linePrefab == null) line.sharedMaterial = body.Material;
                line.shadowCastingMode = ShadowCastingMode.Off;
                line.receiveShadows = false;
                line.sortingOrder = 1;
                line.enabled = false;
                body.Lines[i] = line;
            }
            return body;
        }

        private static GameObject Child(Transform parent, string name)
        {
            var child = new GameObject(name) { layer = PreviewLayer };
            child.transform.SetParent(parent, false);
            return child;
        }

        private static void SetPreviewLayer(Transform owner)
        {
            owner.gameObject.layer = PreviewLayer;
            foreach (Transform child in owner) SetPreviewLayer(child);
        }

        private static Mesh BuildMarker()
        {
            const int sides = 12;
            const float radius = .006f;
            var vertices = new Vector3[sides + 1];
            var triangles = new int[sides * 3];
            var colors = new Color[sides + 1];
            colors[0] = Color.white;
            for (int i = 0; i < sides; i++) {
                float radians = Mathf.PI * 2 * i / sides;
                vertices[i + 1] = new Vector3(Mathf.Cos(radians) * radius, Mathf.Sin(radians) * radius, 0);
                colors[i + 1] = Color.white;
                triangles[i * 3] = 0;
                triangles[i * 3 + 1] = (i + 1) % sides + 1;
                triangles[i * 3 + 2] = i + 1;
            }
            var mesh = new Mesh { name = "Game-owned joint marker" };
            mesh.vertices = vertices;
            mesh.colors = colors;
            mesh.triangles = triangles;
            mesh.RecalculateBounds();
            return mesh;
        }

        private void ReleaseOutput()
        {
            if (redrawCamera != null) redrawCamera.targetTexture = null;
            if (output != null) Destroy(output);
            output = null;
        }

        private void OnDestroy()
        {
            ReleaseOutput();
            if (markerMesh != null) Destroy(markerMesh);
            foreach (var body in pool)
                if (body != null && body.Material != null) Destroy(body.Material);
        }
    }
}
