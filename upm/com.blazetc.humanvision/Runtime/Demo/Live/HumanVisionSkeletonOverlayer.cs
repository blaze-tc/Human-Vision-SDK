using HumanVision.Demo;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.UI;

namespace HumanVision
{
    // Pooled semantic objects on a display image plane, with no metric depth.
    // A transparent camera composition keeps them visible beneath overlay UI.
    public sealed class HumanVisionSkeletonOverlayer : MonoBehaviour
    {
        public HumanVisionCameraManager manager;
        public RawImage preview;
        public Camera foregroundCamera;
        [Tooltip("Reserve an unused user layer for skeleton objects. Only the assigned foreground camera is temporarily excluded; other game cameras must exclude this layer themselves.")]
        [Range(8, 30)] public int renderLayer = 30;
        public GameObject jointPrefab;
        public LineRenderer linePrefab;
        public bool drawSkeleton = true, drawJoints = true, drawBones = true;
        [Min(0.5f)] public float lineWidthPixels = 9;
        [Min(1)] public float jointDiameterPixels = 27;
        [Min(.1f)] public float planeDistance = 1;
        private Transform[] _joints = new Transform[0];
        private MeshRenderer[] _pointRenderers = new MeshRenderer[0];
        private LineRenderer[] _lines = new LineRenderer[0];
        private Material[] _materials = new Material[0];
        private HumanVisionBody[] _bodies = new HumanVisionBody[0];
        private int[] _assignments = new int[0];
        private readonly Vector3[] _corners = new Vector3[4];
        private readonly Vector3[] _positions = new Vector3[SkeletonImagePlane.JointCount];
        private readonly bool[] _valid = new bool[SkeletonImagePlane.JointCount];
        private int _capacity;
        private HumanVisionManager _visionManager;
        private VideoPlayerFrameSource _frameSource;
        private SharedRecognitionSettings _settings;
        private Camera _renderCamera;
        private RenderTexture _target;
        private RawImage _composition;
        private int _ownedLayer = -1;
        private bool _layerValidated;
        private Camera[] _sceneCameras = new Camera[0];
        private Camera _maskedForeground;
        private int _foregroundMask;

        internal void Bind(HumanVisionManager visionManager, VideoPlayerFrameSource frameSource, SharedRecognitionSettings settings)
        {
            Unsubscribe(); Hide();
            _visionManager = visionManager; _frameSource = frameSource; _settings = settings;
            if (isActiveAndEnabled) Subscribe();
        }
        private void Grow(int count)
        {
            if (count <= _capacity) return;
            // Reject configuration errors before allocating or resizing owned pools,
            // so correcting the prefab and retrying cannot overwrite partial growth.
            if (jointPrefab != null && jointPrefab.GetComponentInChildren<MeshRenderer>() == null) {
                Debug.LogError("HumanVision joint prefab requires a MeshRenderer", this); enabled = false; return;
            }
            Shader shader = Resources.Load<Shader>("HumanVisionSkeleton");
            if (shader == null) { Debug.LogError("HumanVision skeleton shader missing", this); enabled = false; return; }
            if (!EnsureLayer()) return;
            int joints = SkeletonImagePlane.JointCount;
            System.Array.Resize(ref _joints, count * joints); System.Array.Resize(ref _pointRenderers, count * joints);
            System.Array.Resize(ref _lines, count * joints); System.Array.Resize(ref _materials, count);
            System.Array.Resize(ref _bodies, count); System.Array.Resize(ref _assignments, count);
            for (int body = _capacity; body < count; body++) {
                Color color = _visionManager != null ? (Color)HumanVisionOverlay.BodyColor(body) : Color.HSVToRGB((body * .137f) % 1, .75f, 1);
                var material = new Material(shader); material.SetColor("_Color", color); _materials[body] = material;
                for (int joint = 0; joint < joints; joint++) {
                    int index = body * joints + joint;
                    var point = jointPrefab != null ? Instantiate(jointPrefab, transform) : GameObject.CreatePrimitive(PrimitiveType.Sphere);
                    point.transform.SetParent(transform, false); point.name = "Region " + body + " " + (HumanVisionCanonicalJointId)joint;
                    var collider = point.GetComponent<Collider>(); if (collider != null) { collider.enabled = false; DestroyOwned(collider); }
                    var renderer = point.GetComponentInChildren<MeshRenderer>();
                    if (renderer == null) { DestroyOwned(point); Debug.LogError("HumanVision joint prefab requires a MeshRenderer", this); enabled = false; return; }
                    renderer.sharedMaterial = material; renderer.shadowCastingMode = ShadowCastingMode.Off; renderer.receiveShadows = false;
                    SetLayer(point.transform, _ownedLayer);
                    // Prefab children remain owned, but only its selected point renderer draws.
                    foreach (var childRenderer in point.GetComponentsInChildren<Renderer>(true)) childRenderer.enabled = childRenderer == renderer;
                    renderer.enabled = true;
                    _joints[index] = point.transform; _pointRenderers[index] = renderer;
                    var line = linePrefab != null ? Instantiate(linePrefab, transform) : new GameObject("Bone " + body + " " + (HumanVisionCanonicalJointId)joint).AddComponent<LineRenderer>();
                    line.transform.SetParent(transform, false); line.sharedMaterial = material;
                    line.useWorldSpace = true; line.positionCount = 2; line.numCapVertices = 3; line.alignment = LineAlignment.View;
                    line.shadowCastingMode = ShadowCastingMode.Off; line.receiveShadows = false;
                    SetLayer(line.transform, _ownedLayer);
                    foreach (var childRenderer in line.GetComponentsInChildren<Renderer>(true)) childRenderer.enabled = childRenderer == line;
                    line.enabled = true;
                    line.startColor = line.endColor = Color.white; _lines[index] = line;
                    point.SetActive(false); line.gameObject.SetActive(false);
                }
            }
            _capacity = count;
        }
        private bool CanRender => drawSkeleton && preview != null &&
            (_visionManager != null ? _visionManager.IsInitialized && _frameSource != null &&
                _frameSource.UnifiedHasRecentFrame && _frameSource.CanPresentResult(_visionManager.SourceFrameId) : manager != null && manager.GetUsersCount() > 0);
        private void LateUpdate()
        {
            if (!CanRender || preview.canvas == null) { Hide(); return; }
            int count = _visionManager != null ? _visionManager.MaxBodies : manager.GetRegionCount();
            Grow(count); if (!enabled || !EnsureComposition()) return;
            System.Array.Clear(_bodies, 0, _bodies.Length);
            bool canonical = (_visionManager != null ? _visionManager : manager.GetComponent<HumanVisionManager>()).UsesRuntimeProfile;
            if (_visionManager != null) {
                bool regions = _settings != null && _settings.UseRegions;
                bool assignmentsReady = !regions || canonical || _visionManager.TryCopyRegionAssignments(_assignments, out _);
                var sampled = _visionManager.SampledBodies;
                int sampledCount = sampled == null ? 0 : Mathf.Min(_visionManager.SampledBodyCount, sampled.Length);
                if (assignmentsReady) for (int body = 0; body < sampledCount; body++) {
                    var candidate = sampled[body]; if (candidate == null) continue;
                    int slot = SkeletonImagePlane.DisplaySlot(body, canonical ? candidate.RegionIndex : regions ? _assignments[body] : -1, regions);
                    if (slot >= 0 && slot < count) _bodies[slot] = candidate;
                }
            } else {
                for (int slot = 0; slot < count; slot++) {
                    if (canonical) manager.TryGetSampledBodyByRegionIndex(slot, out _bodies[slot]);
                    else manager.TryGetBodyByRegionIndex(slot, out _bodies[slot]);
                }
            }
            var canvas = preview.canvas.rootCanvas;
            float depth = Mathf.Max(_renderCamera.nearClipPlane + .01f, planeDistance);
            float scale = Mathf.Max(.001f, canvas.scaleFactor);
            int width = _frameSource != null ? _frameSource.SourceWidth : manager.GetColorImageWidth();
            int height = _frameSource != null ? _frameSource.SourceHeight : manager.GetColorImageHeight();
            for (int slot = 0; slot < _capacity; slot++) {
                RenderBody(slot, _bodies[slot], canonical, width, height, _renderCamera, depth, canvas.pixelRect.position, scale);
            }
            RenderObjects();
        }
        private void RenderObjects()
        {
            _renderCamera.Render(); _composition.enabled = true;
        }
        private void RenderBody(int slot, HumanVisionBody body, bool canonical, int sourceWidth, int sourceHeight,
            Camera camera, float depth, Vector2 pixelOrigin, float displayScale)
        {
            if (body == null || !drawSkeleton) { HideBody(slot); return; }
            preview.rectTransform.GetWorldCorners(_corners);
            Canvas canvas = preview.canvas;
            Camera uiCamera = canvas != null && canvas.renderMode != RenderMode.ScreenSpaceOverlay ? canvas.worldCamera : null;
            Vector2 bottomLeft = RectTransformUtility.WorldToScreenPoint(uiCamera, _corners[0]) - pixelOrigin;
            Vector2 topLeft = RectTransformUtility.WorldToScreenPoint(uiCamera, _corners[1]) - pixelOrigin;
            Vector2 bottomRight = RectTransformUtility.WorldToScreenPoint(uiCamera, _corners[3]) - pixelOrigin;
            Vector3 origin = camera.ScreenToWorldPoint(new Vector3(0, 0, depth));
            float unit = (camera.ScreenToWorldPoint(new Vector3(1, 0, depth)) - origin).magnitude;
            float diameter = SkeletonImagePlane.ReferencePixelsToWorld(Mathf.Max(1, jointDiameterPixels), displayScale, unit);
            float width = SkeletonImagePlane.ReferencePixelsToWorld(Mathf.Max(.5f, lineWidthPixels), displayScale, unit);
            var parentScale = transform.lossyScale;
            for (int joint = 0; joint < SkeletonImagePlane.JointCount; joint++) {
                _valid[joint] = SkeletonImagePlane.TryPosition(body, joint, canonical, sourceWidth, sourceHeight, out var normalized);
                int index = slot * SkeletonImagePlane.JointCount + joint;
                var point = _joints[index]; SetVisible(point.gameObject, drawJoints && _valid[joint]);
                if (!_valid[joint]) continue;
                Vector2 screen = SkeletonImagePlane.NormalizedToScreen(normalized, bottomLeft, topLeft, bottomRight);
                _positions[joint] = camera.ScreenToWorldPoint(new Vector3(screen.x, screen.y, depth));
                point.position = _positions[joint];
                point.localScale = new Vector3(diameter / Mathf.Max(.001f, Mathf.Abs(parentScale.x)),
                    diameter / Mathf.Max(.001f, Mathf.Abs(parentScale.y)), diameter / Mathf.Max(.001f, Mathf.Abs(parentScale.z)));
            }
            for (int joint = 0; joint < SkeletonImagePlane.JointCount; joint++) {
                var line = _lines[slot * SkeletonImagePlane.JointCount + joint]; int parent = SkeletonImagePlane.Parent(joint);
                bool visible = drawBones && parent >= 0 && _valid[joint] && _valid[parent]; SetVisible(line.gameObject, visible);
                if (!visible) continue;
                line.startWidth = line.endWidth = width; line.SetPosition(0, _positions[parent]); line.SetPosition(1, _positions[joint]);
            }
        }
        private bool EnsureComposition()
        {
            if (!EnsureLayer() || !IsolateCameras()) return false;
            var canvas = preview.canvas.rootCanvas; var rootRect = canvas.GetComponent<RectTransform>(); var rect = canvas.pixelRect;
            int width = Mathf.RoundToInt(rect.width), height = Mathf.RoundToInt(rect.height);
            if (width <= 0 || height <= 0) { Hide(); return false; }
            if (_renderCamera == null) {
                var cameraObject = new GameObject("HumanVision image-plane camera", typeof(Camera));
                cameraObject.transform.SetParent(transform, false); _renderCamera = cameraObject.GetComponent<Camera>();
                _renderCamera.enabled = false; _renderCamera.clearFlags = CameraClearFlags.SolidColor; _renderCamera.backgroundColor = Color.clear;
                _renderCamera.orthographic = true; _renderCamera.nearClipPlane = .01f; _renderCamera.farClipPlane = 100;
                // An isolated display plane; no inferred scene-space depth is claimed.
                _renderCamera.transform.SetPositionAndRotation(new Vector3(0, 0, -10000), Quaternion.identity);
            }
            _renderCamera.cullingMask = 1 << _ownedLayer;
            if (_composition == null) {
                var image = new GameObject("HumanVision object skeleton composition", typeof(RectTransform), typeof(RawImage));
                image.transform.SetParent(preview.transform, false); image.transform.SetAsFirstSibling();
                _composition = image.GetComponent<RawImage>(); _composition.raycastTarget = false;
            }
            if (_target == null || _target.width != width || _target.height != height) {
                ReleaseTarget(); _target = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32) { name = "HumanVision display skeleton", antiAliasing = 1 };
                if (!_target.Create()) { Debug.LogError("HumanVision skeleton display target could not be created", this); enabled = false; return false; }
                _renderCamera.targetTexture = _target; _composition.texture = _target;
            }
            _renderCamera.orthographicSize = height * .5f; _renderCamera.aspect = (float)width / height;
            // Child ordering follows the preview; counter-transform to the full canvas
            // rectangle so fitted/offset/rotated preview geometry still maps exactly.
            var imageRect = _composition.rectTransform; imageRect.anchorMin = imageRect.anchorMax = imageRect.pivot = new Vector2(.5f, .5f);
            imageRect.sizeDelta = rootRect.rect.size;
            imageRect.SetPositionAndRotation(rootRect.TransformPoint(rootRect.rect.center), rootRect.rotation);
            var rootScale = rootRect.lossyScale; var previewScale = preview.transform.lossyScale;
            imageRect.localScale = new Vector3(rootScale.x / previewScale.x, rootScale.y / previewScale.y, rootScale.z / previewScale.z);
            return true;
        }
        private bool EnsureLayer()
        {
            if (renderLayer < 8 || renderLayer > 30) return ConfigurationError("HumanVision skeleton renderLayer must be an unused user layer (8-30); layer 31 is reserved by the Unity Editor.");
            if (_layerValidated && _ownedLayer == renderLayer) return true;
            // Configuration/lifecycle only. Warmed frames do not enumerate renderers or allocate.
            foreach (var renderer in FindObjectsOfType<Renderer>(true)) {
                if (renderer.gameObject.layer == renderLayer && !OwnsRenderer(renderer))
                    return ConfigurationError("HumanVision skeleton renderLayer " + renderLayer + " is occupied by '" + renderer.name + "'. Reserve an unused layer and configure renderLayer.");
            }
            if (_ownedLayer != renderLayer) {
                RestoreForeground();
                _ownedLayer = renderLayer;
                for (int index = 0; index < _joints.Length; index++) {
                    if (_joints[index] != null) SetLayer(_joints[index], _ownedLayer);
                    if (_lines[index] != null) SetLayer(_lines[index].transform, _ownedLayer);
                }
            }
            _layerValidated = true; return true;
        }
        private bool OwnsRenderer(Renderer renderer)
        {
            for (int index = 0; index < _joints.Length; index++) {
                if (_joints[index] != null && renderer.transform.IsChildOf(_joints[index])) return true;
                if (_lines[index] != null && renderer.transform.IsChildOf(_lines[index].transform)) return true;
            }
            return false;
        }
        private static void SetLayer(Transform owner, int layer)
        {
            owner.gameObject.layer = layer;
            for (int child = 0; child < owner.childCount; child++) SetLayer(owner.GetChild(child), layer);
        }
        private bool IsolateCameras()
        {
            if (_maskedForeground != foregroundCamera) RestoreForeground();
            int mask = 1 << _ownedLayer;
            int count = Camera.allCamerasCount;
            if (_sceneCameras.Length < count) System.Array.Resize(ref _sceneCameras, count);
            count = Camera.GetAllCameras(_sceneCameras);
            for (int index = 0; index < count; index++) {
                var camera = _sceneCameras[index];
                if (camera == _renderCamera || camera == foregroundCamera || camera.cameraType == CameraType.SceneView || camera.cameraType == CameraType.Preview) continue;
                if ((camera.cullingMask & mask) != 0)
                    return ConfigurationError("HumanVision skeleton renderLayer " + _ownedLayer + " is visible to camera '" + camera.name + "'. Exclude that layer on this camera, or assign it as foregroundCamera.");
            }
            if (foregroundCamera != null) {
                if (_maskedForeground == null) { _maskedForeground = foregroundCamera; _foregroundMask = foregroundCamera.cullingMask; }
                foregroundCamera.cullingMask &= ~mask;
            }
            return true;
        }
        private bool ConfigurationError(string message) { Debug.LogError(message, this); enabled = false; Hide(); ReleaseComposition(); return false; }
        private void RestoreForeground()
        {
            if (_maskedForeground != null) {
                int mask = 1 << _ownedLayer;
                _maskedForeground.cullingMask = (_maskedForeground.cullingMask & ~mask) | (_foregroundMask & mask);
            }
            _maskedForeground = null;
        }
        private static void SetVisible(GameObject gameObject, bool visible) { if (gameObject.activeSelf != visible) gameObject.SetActive(visible); }
        private void HideBody(int slot)
        {
            for (int joint = 0; joint < SkeletonImagePlane.JointCount; joint++) {
                int index = slot * SkeletonImagePlane.JointCount + joint;
                if (_joints[index] != null) SetVisible(_joints[index].gameObject, false);
                if (_lines[index] != null) SetVisible(_lines[index].gameObject, false);
            }
        }
        private void Hide()
        {
            for (int slot = 0; slot < _capacity; slot++) HideBody(slot);
            if (_composition != null) _composition.enabled = false;
        }
        private void OnPresentationChanged() { if (!CanRender) Hide(); }
        private void OnResultChanged(long sequence) { if (!CanRender) Hide(); }
        private void Subscribe()
        {
            if (_frameSource != null) _frameSource.PresentationFrameChanged += OnPresentationChanged;
            if (_visionManager != null) _visionManager.ResultUpdated += OnResultChanged;
        }
        private void Unsubscribe()
        {
            if (_frameSource != null) _frameSource.PresentationFrameChanged -= OnPresentationChanged;
            if (_visionManager != null) _visionManager.ResultUpdated -= OnResultChanged;
        }
        private void ReleaseTarget()
        {
            if (_renderCamera != null) _renderCamera.targetTexture = null;
            if (_composition != null) _composition.texture = null;
            if (_target != null) { _target.Release(); DestroyOwned(_target); _target = null; }
        }
        private void ReleaseComposition()
        {
            ReleaseTarget();
            if (_renderCamera != null) { DestroyOwned(_renderCamera.gameObject); _renderCamera = null; }
            if (_composition != null) { DestroyOwned(_composition.gameObject); _composition = null; }
            RestoreForeground(); _layerValidated = false;
        }
        private static void DestroyOwned(Object value) { if (value == null) return; if (Application.isPlaying) Destroy(value); else DestroyImmediate(value); }
        private void OnEnable() => Subscribe();
        private void OnDisable() { Unsubscribe(); Hide(); ReleaseComposition(); }
        private void OnDestroy()
        {
            Unsubscribe(); Hide(); ReleaseComposition();
            foreach (var joint in _joints) if (joint != null) DestroyOwned(joint.gameObject);
            foreach (var line in _lines) if (line != null) DestroyOwned(line.gameObject);
            foreach (var material in _materials) if (material != null) DestroyOwned(material);
        }
    }
}
