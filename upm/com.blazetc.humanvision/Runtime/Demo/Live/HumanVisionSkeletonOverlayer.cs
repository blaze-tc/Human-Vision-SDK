using UnityEngine;
using UnityEngine.UI;

namespace HumanVision
{
    // Independent world-space objects, projected over the preview rectangle.
    // No Azure/Kinect dependency; image-plane positions are not metric depth.
    public sealed class HumanVisionSkeletonOverlayer : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        public RawImage preview;
        public Camera foregroundCamera;
        public GameObject jointPrefab;
        public LineRenderer linePrefab;
        public bool drawSkeleton = true, drawJoints = true, drawBones = true;
        [Min(0.5f)] public float lineWidthPixels = 3;
        [Min(1)] public float jointDiameterPixels = 9;
        [Min(.1f)] public float planeDistance = 1;
        private static readonly int[] Parents = { -1,0,0,1,2,6,5,5,6,7,8,5,6,11,12,13,14,9,17,17,10,20,20 };
        private Transform[] _joints = new Transform[0];
        private LineRenderer[] _lines = new LineRenderer[0];
        private Material[] _materials = new Material[0];
        private readonly Vector3[] _corners = new Vector3[4];
        private readonly Vector3[] _positions = new Vector3[23];
        private readonly bool[] _valid = new bool[23];
        private int _capacity;
        private void Grow(int count)
        {
            if (count <= _capacity) return;
            System.Array.Resize(ref _joints, count * 23);
            System.Array.Resize(ref _lines, count * 23);
            System.Array.Resize(ref _materials, count);
            Shader shader = Resources.Load<Shader>("HumanVisionSkeleton");
            if (shader == null) { Debug.LogError("HumanVision skeleton shader missing", this); enabled = false; return; }
            for (int body = _capacity; body < count; body++) {
                Color color = Color.HSVToRGB((body * .137f) % 1, .75f, 1);
                var material = new Material(shader); material.SetColor("_Color", color); _materials[body] = material;
                for (int joint = 0; joint < 23; joint++) {
                    int i = body * 23 + joint;
                    var point = jointPrefab != null ? Instantiate(jointPrefab, transform) : GameObject.CreatePrimitive(PrimitiveType.Sphere);
                    point.transform.SetParent(transform, false); point.name = "Region " + body + " " + (HumanVisionJointType)joint;
                    var collider = point.GetComponent<Collider>(); if (collider != null) { collider.enabled = false; Destroy(collider); }
                    var renderer = point.GetComponent<Renderer>(); if (renderer != null) renderer.sharedMaterial = material;
                    _joints[i] = point.transform;
                    var line = linePrefab != null ? Instantiate(linePrefab, transform) : new GameObject("Bone " + body + " " + joint).AddComponent<LineRenderer>();
                    line.transform.SetParent(transform, false); line.sharedMaterial = material;
                    line.useWorldSpace = true; line.positionCount = 2; line.numCapVertices = 3;
                    line.startColor = line.endColor = Color.white; _lines[i] = line;
                    point.SetActive(false); line.gameObject.SetActive(false);
                }
            }
            _capacity = count;
        }
        private void LateUpdate()
        {
            if (manager == null || preview == null || foregroundCamera == null || !drawSkeleton) { Hide(); return; }
            Grow(manager.GetRegionCount()); if (!enabled) return;
            preview.rectTransform.GetWorldCorners(_corners);
            Canvas canvas = preview.canvas;
            Camera uiCamera = canvas != null && canvas.renderMode != RenderMode.ScreenSpaceOverlay ? canvas.worldCamera : null;
            Vector2 left = RectTransformUtility.WorldToScreenPoint(uiCamera, _corners[0]);
            Vector2 right = RectTransformUtility.WorldToScreenPoint(uiCamera, _corners[2]);
            float depth = Mathf.Max(foregroundCamera.nearClipPlane + .01f, planeDistance);
            Vector3 origin = foregroundCamera.ScreenToWorldPoint(new Vector3(left.x, left.y, depth));
            float unit = (foregroundCamera.ScreenToWorldPoint(new Vector3(left.x + 1, left.y, depth)) - origin).magnitude;
            for (int body = 0; body < _capacity; body++) {
                for (int j = 0; j < 23; j++) {
                    _valid[j] = manager.TryGetJointByRegionIndex(body, (HumanVisionJointType)j, out var joint);
                    _positions[j] = foregroundCamera.ScreenToWorldPoint(new Vector3(Mathf.Lerp(left.x, right.x, joint.Normalized.x),
                        Mathf.Lerp(right.y, left.y, joint.Normalized.y), depth));
                    Transform point = _joints[body * 23 + j]; point.gameObject.SetActive(drawJoints && _valid[j]);
                    if (_valid[j]) { point.position = _positions[j]; point.localScale = Vector3.one * Mathf.Max(1, jointDiameterPixels) * unit; }
                }
                for (int j = 0; j < 23; j++) {
                    LineRenderer line = _lines[body * 23 + j]; int parent = Parents[j];
                    bool visible = drawBones && parent >= 0 && _valid[j] && _valid[parent];
                    line.gameObject.SetActive(visible);
                    if (visible) { line.startWidth = line.endWidth = Mathf.Max(.5f, lineWidthPixels) * unit; line.SetPosition(0, _positions[parent]); line.SetPosition(1, _positions[j]); }
                }
            }
        }
        private void Hide() { foreach (var joint in _joints) if (joint != null) joint.gameObject.SetActive(false); foreach (var line in _lines) if (line != null) line.gameObject.SetActive(false); }
        private void OnDisable() { Hide(); }
        private void OnDestroy() { foreach (var material in _materials) if (material != null) Destroy(material); }
    }
}
