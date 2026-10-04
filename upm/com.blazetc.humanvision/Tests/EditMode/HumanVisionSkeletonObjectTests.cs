using System.Reflection;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.TestTools;
using HumanVision.Demo;
using HumanVision.Input;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSkeletonObjectTests
    {
        private Camera[] _ambientCameras;
        private bool[] _ambientEnabled;
        [SetUp]
        public void IsolateFixtureFromDefaultSceneCamera()
        {
            // Include untagged, disabled and inactive scene cameras. Only capture
            // cameras present at SetUp so test-created guard cameras stay enabled.
            _ambientCameras = Object.FindObjectsOfType<Camera>(true);
            _ambientEnabled = new bool[_ambientCameras.Length];
            for (int i = 0; i < _ambientCameras.Length; i++) {
                var camera = _ambientCameras[i];
                if (!camera.gameObject.scene.IsValid() || !camera.gameObject.scene.isLoaded) { _ambientCameras[i] = null; continue; }
                _ambientEnabled[i] = camera.enabled;
                camera.enabled = false;
            }
        }
        [TearDown]
        public void RestoreDefaultSceneCamera()
        {
            if (_ambientCameras == null) return;
            for (int i = 0; i < _ambientCameras.Length; i++)
                if (_ambientCameras[i] != null) _ambientCameras[i].enabled = _ambientEnabled[i];
            _ambientCameras = null; _ambientEnabled = null;
        }

        [Test]
        public void FixtureIsolatesUntaggedAmbientCamerasAndRestoresTheirExactEnabledStates()
        {
            var fixture = new HumanVisionSkeletonObjectTests();
            var tagged = new GameObject("Ambient main", typeof(Camera)).GetComponent<Camera>();
            var untagged = new GameObject("Presentation Camera", typeof(Camera)).GetComponent<Camera>();
            var disabled = new GameObject("Disabled ambient", typeof(Camera)).GetComponent<Camera>();
            var inactive = new GameObject("Inactive ambient", typeof(Camera)).GetComponent<Camera>();
            var destroyed = new GameObject("Retired ambient", typeof(Camera)).GetComponent<Camera>();
            var root = new GameObject("Ambient isolation renderer");
            var canvas = new GameObject("Ambient isolation canvas", typeof(RectTransform), typeof(Canvas));
            try {
                tagged.tag = "MainCamera"; disabled.enabled = false; inactive.gameObject.SetActive(false);
                int taggedMask = tagged.cullingMask, untaggedMask = untagged.cullingMask;
                fixture.IsolateFixtureFromDefaultSceneCamera();
                Assert.That(tagged.enabled, Is.False);
                Assert.That(untagged.enabled, Is.False, "Loaded scene cameras need isolation even without the MainCamera tag.");
                Assert.That(disabled.enabled, Is.False); Assert.That(inactive.enabled, Is.False);
                Assert.That(tagged.cullingMask, Is.EqualTo(taggedMask)); Assert.That(untagged.cullingMask, Is.EqualTo(untaggedMask));
                var previewObject = new GameObject("Preview", typeof(RectTransform), typeof(RawImage));
                previewObject.transform.SetParent(canvas.transform, false);
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = previewObject.GetComponent<RawImage>();
                Call(owner, "Grow", 1);
                Assert.That((bool)Call(owner, "EnsureComposition"), Is.True, "Preexisting untagged cameras must not interfere with renderer tests.");
                Object.DestroyImmediate(destroyed.gameObject);
                fixture.RestoreDefaultSceneCamera();
                Assert.That(tagged.enabled, Is.True); Assert.That(untagged.enabled, Is.True);
                Assert.That(disabled.enabled, Is.False); Assert.That(inactive.enabled, Is.True);
                Assert.That(inactive.gameObject.activeSelf, Is.False);
                Assert.That(tagged.cullingMask, Is.EqualTo(taggedMask)); Assert.That(untagged.cullingMask, Is.EqualTo(untaggedMask));
            } finally {
                fixture.RestoreDefaultSceneCamera();
                Object.DestroyImmediate(root); Object.DestroyImmediate(canvas);
                Object.DestroyImmediate(tagged.gameObject); Object.DestroyImmediate(untagged.gameObject);
                Object.DestroyImmediate(disabled.gameObject); Object.DestroyImmediate(inactive.gameObject);
                if (destroyed != null) Object.DestroyImmediate(destroyed.gameObject);
            }
        }
        private static object Call(HumanVisionSkeletonOverlayer owner, string method, params object[] arguments)
        {
            var member = typeof(HumanVisionSkeletonOverlayer).GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic);
            Assert.That(member, Is.Not.Null, "Missing production object rendering operation " + method);
            return member.Invoke(owner, arguments);
        }
        private static T Field<T>(HumanVisionSkeletonOverlayer owner, string name) => (T)typeof(HumanVisionSkeletonOverlayer)
            .GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(owner);

        private static void Set(object owner, string name, object value) => owner.GetType()
            .GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(owner, value);
        private static void Invoke(object owner, string name) => owner.GetType()
            .GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic).Invoke(owner, null);

        // Only the native session and camera publication boundaries are substituted;
        // assignment, freshness admission and rendering use the real production classes.
        private sealed class V1Session : IHumanVisionSession
        {
            public HumanVisionBody[] Bodies { get; set; }
            public int[] Assignments;
            public int MaxBodies => 2;
            public int BodyCount => Bodies.Length;
            public long ResultSequence => 1;
            public long SourceFrameId => 1;
            public long SourceTimestampUs => 0;
            public HumanVisionStats Stats => default;
            public bool SubmitFrame(System.IntPtr data, int width, int height, int stride, HumanVisionPixelFormat format, long id, long timestamp, int bytes) => false;
            public bool PollLatestResult() => false;
            public void SetRegions(Rect[] regions, long revision) { }
            public bool CopyRegions(long sequence, int[] indices, out long revision) { Assignments.CopyTo(indices, 0); revision = 7; return true; }
            public void RefreshStats() { }
            public void ReconfigureMaxBodies(int count) { }
            public void Dispose() { }
        }
        private sealed class CameraPublication : IHumanVisionFrameSource
        {
            public Texture CurrentTexture { get; set; }
            public InputSourceState State => InputSourceState.Streaming;
            public string LastError => string.Empty;
            public void Open(HumanVisionSourceSettings settings) { }
            public void Close() { }
            public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease) { lease = default; return false; }
            public bool TryGetLatestFrame(long after, out HumanVisionTextureFrame frame) {
                frame = new HumanVisionTextureFrame(1, 1, 1, CurrentTexture, InputMonotonicClock.NowUs, -1, 0, false,
                    FrameRowOrigin.UnityBottomLeft, FrameColorSpace.Srgb, FrameTimestampKind.UnityObserved, 1);
                return after < 1;
            }
        }

        [TestCase(7)]
        [TestCase(31)]
        [TestCase(32)]
        public void BuiltinAndEditorReservedLayersCannotClaimThePool(int layer)
        {
            var root = new GameObject("Invalid reserved layer");
            try {
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.renderLayer = layer;
                LogAssert.Expect(LogType.Error, "HumanVision skeleton renderLayer must be an unused user layer (8-30); layer 31 is reserved by the Unity Editor.");
                Call(owner, "Grow", 1); Assert.That(Field<int>(owner, "_capacity"), Is.Zero);
                Assert.That(Field<Material[]>(owner, "_materials"), Is.Empty); Assert.That(owner.enabled, Is.False);
            } finally { Object.DestroyImmediate(root); }
        }

        [Test]
        public void OccupiedLayerRejectsBeforeAllocationAndCanRetryOnAnUnusedLayer()
        {
            var root = new GameObject("Reserved layer validation");
            var occupied = GameObject.CreatePrimitive(PrimitiveType.Cube); occupied.name = "Existing scene geometry"; occupied.layer = 30;
            try {
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>();
                LogAssert.Expect(LogType.Error, "HumanVision skeleton renderLayer 30 is occupied by 'Existing scene geometry'. Reserve an unused layer and configure renderLayer.");
                Call(owner, "Grow", 1);
                Assert.That(owner.enabled, Is.False); Assert.That(Field<int>(owner, "_capacity"), Is.Zero);
                Assert.That(Field<Material[]>(owner, "_materials"), Is.Empty);
                Assert.That(occupied.layer, Is.EqualTo(30), "Validation cannot relayer user objects.");
                owner.renderLayer = 29; owner.enabled = true; Call(owner, "Grow", 1);
                foreach (var renderer in root.GetComponentsInChildren<Renderer>(true)) Assert.That(renderer.gameObject.layer, Is.EqualTo(29));
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(occupied); }
        }

        [Test]
        public void AdditionalGameCameraIsRejectedWithoutChangingAnyCameraMask()
        {
            var root = new GameObject("Camera isolation validation");
            var canvas = new GameObject("Canvas", typeof(RectTransform), typeof(Canvas));
            var foreground = new GameObject("Assigned foreground", typeof(Camera)).GetComponent<Camera>();
            var other = new GameObject("Unassigned game camera", typeof(Camera)).GetComponent<Camera>();
            try {
                var previewObject = new GameObject("Preview", typeof(RectTransform), typeof(RawImage)); previewObject.transform.SetParent(canvas.transform, false);
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = previewObject.GetComponent<RawImage>(); owner.foregroundCamera = foreground;
                int foregroundMask = foreground.cullingMask, otherMask = other.cullingMask;
                Call(owner, "Grow", 1);
                LogAssert.Expect(LogType.Error, "HumanVision skeleton renderLayer 30 is visible to camera 'Unassigned game camera'. Exclude that layer on this camera, or assign it as foregroundCamera.");
                Assert.That((bool)Call(owner, "EnsureComposition"), Is.False);
                Assert.That(foreground.cullingMask, Is.EqualTo(foregroundMask)); Assert.That(other.cullingMask, Is.EqualTo(otherMask));
                Assert.That(Field<Camera>(owner, "_renderCamera"), Is.Null);
                other.cullingMask &= ~(1 << 30); owner.enabled = true;
                Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                Assert.That(foreground.cullingMask, Is.EqualTo(foregroundMask & ~(1 << 30)));
                Assert.That(other.cullingMask, Is.EqualTo(otherMask & ~(1 << 30)));
                Call(owner, "OnDisable"); Assert.That(foreground.cullingMask, Is.EqualTo(foregroundMask));
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(canvas); Object.DestroyImmediate(foreground.gameObject); Object.DestroyImmediate(other.gameObject); }
        }

        [Test]
        public void LayerReconfigurationRetainsNestedPrefabPoolAndRestoresForegroundMask()
        {
            var root = new GameObject("Layer reconfiguration");
            var canvas = new GameObject("Canvas", typeof(RectTransform), typeof(Canvas));
            var foreground = new GameObject("Foreground", typeof(Camera)).GetComponent<Camera>();
            var prefab = new GameObject("Nested point prefab");
            var child = GameObject.CreatePrimitive(PrimitiveType.Sphere); child.transform.SetParent(prefab.transform, false);
            try {
                var previewObject = new GameObject("Preview", typeof(RectTransform), typeof(RawImage)); previewObject.transform.SetParent(canvas.transform, false);
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = previewObject.GetComponent<RawImage>(); owner.foregroundCamera = foreground; owner.jointPrefab = prefab;
                foreground.cullingMask = (1 << 30) | (1 << 29) | 5; int originalMask = foreground.cullingMask;
                Call(owner, "Grow", 1); Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                var point = Field<Transform[]>(owner, "_joints")[31]; var line = Field<LineRenderer[]>(owner, "_lines")[31];
                int pointId = point.GetInstanceID(), lineId = line.GetInstanceID();
                owner.renderLayer = 29; Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                Assert.That(point.GetInstanceID(), Is.EqualTo(pointId)); Assert.That(line.GetInstanceID(), Is.EqualTo(lineId));
                foreach (var nested in point.GetComponentsInChildren<Transform>(true)) Assert.That(nested.gameObject.layer, Is.EqualTo(29));
                Assert.That(Field<MeshRenderer[]>(owner, "_pointRenderers")[31].enabled, Is.True);
                Assert.That(line.enabled, Is.True); Assert.That(line.gameObject.layer, Is.EqualTo(29));
                Assert.That(Field<Camera>(owner, "_renderCamera").cullingMask, Is.EqualTo(1 << 29));
                Assert.That(foreground.cullingMask, Is.EqualTo(originalMask & ~(1 << 29)), "Reconfiguration restores the old reserved bit before excluding the new one.");
                Call(owner, "OnDisable"); Assert.That(foreground.cullingMask, Is.EqualTo(originalMask));
                Assert.That((bool)Call(owner, "EnsureComposition"), Is.True); Call(owner, "OnDestroy");
                Assert.That(foreground.cullingMask, Is.EqualTo(originalMask));
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(canvas); Object.DestroyImmediate(foreground.gameObject); Object.DestroyImmediate(prefab); }
        }

        [Test]
        public void ForegroundMaskEditsOutsideOwnedLayerSurviveLayerChangeAndDisable()
        {
            var root = new GameObject("Foreground mask ownership");
            var canvas = new GameObject("Canvas", typeof(RectTransform), typeof(Canvas));
            var foreground = new GameObject("Foreground", typeof(Camera)).GetComponent<Camera>();
            try {
                var previewObject = new GameObject("Preview", typeof(RectTransform), typeof(RawImage)); previewObject.transform.SetParent(canvas.transform, false);
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = previewObject.GetComponent<RawImage>(); owner.foregroundCamera = foreground;
                foreground.cullingMask = (1 << 30) | (1 << 29) | 5;
                Call(owner, "Grow", 1); Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                foreground.cullingMask = (foreground.cullingMask | (1 << 6)) & ~1;
                int editedMask = foreground.cullingMask;
                owner.renderLayer = 29; Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                Assert.That(foreground.cullingMask, Is.EqualTo((editedMask | (1 << 30)) & ~(1 << 29)), "Layer changes must retain unrelated mask edits made by the application.");
                foreground.cullingMask |= 1 << 8; int beforeDisable = foreground.cullingMask;
                Call(owner, "OnDisable"); Assert.That(foreground.cullingMask, Is.EqualTo(beforeDisable | (1 << 29)), "Disable restores only the bit this component excluded.");
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(canvas); Object.DestroyImmediate(foreground.gameObject); }
        }

        [Test]
        public void NewlyEnabledUnassignedCameraHidesPoolAndReleasesCompositionWithoutMaskChanges()
        {
            var root = new GameObject("Camera added after initialization");
            var canvas = new GameObject("Canvas", typeof(RectTransform), typeof(Canvas));
            var foreground = new GameObject("Foreground", typeof(Camera)).GetComponent<Camera>();
            Camera other = null;
            try {
                var previewObject = new GameObject("Preview", typeof(RectTransform), typeof(RawImage)); previewObject.transform.SetParent(canvas.transform, false);
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = previewObject.GetComponent<RawImage>(); owner.foregroundCamera = foreground;
                int originalMask = foreground.cullingMask;
                Call(owner, "Grow", 1); Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                var point = Field<Transform[]>(owner, "_joints")[0]; var line = Field<LineRenderer[]>(owner, "_lines")[0];
                point.gameObject.SetActive(true); line.gameObject.SetActive(true);
                other = new GameObject("New game camera", typeof(Camera)).GetComponent<Camera>(); int otherMask = other.cullingMask;
                LogAssert.Expect(LogType.Error, "HumanVision skeleton renderLayer 30 is visible to camera 'New game camera'. Exclude that layer on this camera, or assign it as foregroundCamera.");
                Assert.That((bool)Call(owner, "EnsureComposition"), Is.False);
                Assert.That(owner.enabled, Is.False); Assert.That(point.gameObject.activeSelf, Is.False); Assert.That(line.gameObject.activeSelf, Is.False);
                Assert.That(Field<RenderTexture>(owner, "_target"), Is.Null); Assert.That(Field<Camera>(owner, "_renderCamera"), Is.Null);
                Assert.That(foreground.cullingMask, Is.EqualTo(originalMask)); Assert.That(other.cullingMask, Is.EqualTo(otherMask));
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(canvas); Object.DestroyImmediate(foreground.gameObject); if (other != null) Object.DestroyImmediate(other.gameObject); }
        }

        [TestCase(1, 0)]
        [TestCase(0, 1)]
        public void UnboundV1RendersBothActualFacadeAssignmentsAndObservedLandmarks(int firstSlot, int secondSlot)
        {
            var root = new GameObject("V1 renderer and real facade");
            var canvas = new GameObject("V1 preview canvas", typeof(RectTransform), typeof(Canvas));
            var texture = new Texture2D(640, 360);
            try {
                canvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
                var previewObject = new GameObject("V1 preview", typeof(RectTransform), typeof(RawImage));
                previewObject.transform.SetParent(canvas.transform, false);
                var preview = previewObject.GetComponent<RawImage>(); preview.rectTransform.sizeDelta = new Vector2(320, 180);
                Canvas.ForceUpdateCanvases();
                var facade = root.AddComponent<HumanVisionCameraManager>(); Invoke(facade, "Awake");
                var manager = root.GetComponent<HumanVisionManager>();
                var bodies = new[] { new HumanVisionBody(), new HumanVisionBody() };
                for (int i = 0; i < 2; i++) {
                    bodies[i].Joints[5] = new HumanVisionJoint(Vector2.zero, new Vector2(.2f + i*.5f, .3f), 1, true);
                    bodies[i].HandJoints[0] = new HumanVisionJoint(Vector2.zero, new Vector2(.25f + i*.5f, .6f), 1, true);
                    Assert.That(bodies[i].RegionIndex, Is.Zero, "V1 native copies do not populate canonical RegionIndex.");
                }
                Set(manager, "_session", new V1Session { Bodies = bodies, Assignments = new[] { firstSlot, secondSlot } });
                var bridge = root.GetComponent<VideoPlayerFrameSource>(); Invoke(bridge, "Awake"); bridge.Configure(manager, preview, null);
                Set(bridge, "_livePreview", true); Set(bridge, "_acceptReadbacks", true);
                var live = root.GetComponent<HumanVisionLiveSource>(); Invoke(live, "Awake");
                Set(live, "source", new CameraPublication { CurrentTexture = texture });
                Set(facade, "_slots", new HumanVisionBody[2]); Set(facade, "_assignments", new int[2]);
                Set(facade, "_revision", 7L); Set(facade, "_regionsEnabled", true);
                typeof(HumanVisionCameraManager).GetMethod("OnResult", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(facade, new object[] { 1L });
                Assert.That(facade.GetUsersCount(), Is.EqualTo(2), "Fixture must pass real source/result freshness checks.");
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.manager = facade; owner.preview = preview;
                Call(owner, "LateUpdate");
                var selected = Field<HumanVisionBody[]>(owner, "_bodies");
                Assert.That(selected[firstSlot], Is.SameAs(bodies[0])); Assert.That(selected[secondSlot], Is.SameAs(bodies[1]));
                var points = Field<Transform[]>(owner, "_joints"); var camera = Field<Camera>(owner, "_renderCamera");
                for (int i = 0; i < 2; i++) foreach (int joint in new[] { 5, 8 }) {
                    int slot = i == 0 ? firstSlot : secondSlot;
                    var point = points[slot*32 + joint]; Assert.That(point.gameObject.activeSelf, Is.True);
                    var normalized = joint == 5 ? bodies[i].Joints[5].Normalized : bodies[i].HandJoints[0].Normalized;
                    var rect = preview.rectTransform.rect;
                    var expected = RectTransformUtility.WorldToScreenPoint(null, preview.rectTransform.TransformPoint(new Vector3(rect.xMin + rect.width*normalized.x, rect.yMax - rect.height*normalized.y, 0)));
                    var actual = camera.WorldToScreenPoint(point.position);
                    Assert.That(actual.x, Is.EqualTo(expected.x).Within(.01f)); Assert.That(actual.y, Is.EqualTo(expected.y).Within(.01f));
                    Assert.That(points[slot*32 + 9].gameObject.activeSelf, Is.False, "Unavailable Handtip remains hidden.");
                }
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(canvas); Object.DestroyImmediate(texture); }
        }

        [Test]
        public void InvalidPrefabCannotAllocateOrOverwritePoolBeforeValidRetry()
        {
            var root = new GameObject("Recoverable pool growth");
            var invalid = new GameObject("Invalid joint prefab");
            try {
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); Call(owner, "Grow", 1);
                var originalMaterial = Field<Material[]>(owner, "_materials")[0];
                var originalPoint = Field<Transform[]>(owner, "_joints")[0];
                owner.jointPrefab = invalid;
                LogAssert.Expect(LogType.Error, "HumanVision joint prefab requires a MeshRenderer"); Call(owner, "Grow", 2);
                Assert.That(Field<int>(owner, "_capacity"), Is.EqualTo(1));
                Assert.That(Field<Material[]>(owner, "_materials").Length, Is.EqualTo(1), "Rejected configuration cannot allocate an unowned retry material.");
                Assert.That(root.GetComponentsInChildren<MeshRenderer>(true).Length, Is.EqualTo(32));
                owner.jointPrefab = null; owner.enabled = true; Call(owner, "Grow", 2);
                Assert.That(Field<int>(owner, "_capacity"), Is.EqualTo(2));
                Assert.That(Field<Material[]>(owner, "_materials")[0], Is.SameAs(originalMaterial));
                Assert.That(Field<Transform[]>(owner, "_joints")[0], Is.SameAs(originalPoint));
                Assert.That(root.GetComponentsInChildren<MeshRenderer>(true).Length, Is.EqualTo(64));
                Assert.That(root.GetComponentsInChildren<LineRenderer>(true).Length, Is.EqualTo(64));
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(invalid); }
        }

        [Test]
        public void SixBodiesPoolAllCanonicalMeshPointsAndLineBonesAndReuseThem()
        {
            var root = new GameObject("Canonical renderer test");
            try {
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>();
                Call(owner, "Grow", 6);
                var points = root.GetComponentsInChildren<MeshRenderer>(true);
                var lines = root.GetComponentsInChildren<LineRenderer>(true);
                Assert.That(points.Length, Is.EqualTo(192));
                Assert.That(lines.Length, Is.EqualTo(192));
                foreach (var point in points) Assert.That(point.enabled, Is.True, "Pooled points use normal MeshRenderer rendering.");
                foreach (var line in lines) Assert.That(line.enabled, Is.True, "Pooled bones use normal LineRenderer rendering.");
                var pointId = points[191].GetInstanceID(); var lineId = lines[191].GetInstanceID();
                Call(owner, "Grow", 6);
                Assert.That(root.GetComponentsInChildren<MeshRenderer>(true)[191].GetInstanceID(), Is.EqualTo(pointId));
                Assert.That(root.GetComponentsInChildren<LineRenderer>(true)[191].GetInstanceID(), Is.EqualTo(lineId));
                owner.enabled = false;
                foreach (var point in points) Assert.That(point.gameObject.activeSelf, Is.False);
                foreach (var line in lines) Assert.That(line.gameObject.activeSelf, Is.False);
            } finally { Object.DestroyImmediate(root); }
        }

        [Test]
        public void CanonicalPoint31AndItsBoneUseActualPreviewAndUnavailableBodyIsHidden()
        {
            var root = new GameObject("Canonical renderer test");
            var canvas = new GameObject("Preview Canvas", typeof(RectTransform), typeof(Canvas));
            var camera = new GameObject("Plane Camera", typeof(Camera)).GetComponent<Camera>();
            try {
                var previewObject = new GameObject("Preview", typeof(RectTransform), typeof(RawImage));
                previewObject.transform.SetParent(canvas.transform, false);
                var preview = previewObject.GetComponent<RawImage>();
                preview.rectTransform.sizeDelta = new Vector2(960, 540);
                preview.rectTransform.anchoredPosition = new Vector2(70, 40);
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = preview; owner.foregroundCamera = camera;
                Call(owner, "Grow", 1);
                var body = new HumanVisionBody();
                body.CanonicalJoints[30] = new HumanVisionCanonicalJoint(new HumanVisionJoint(Vector2.zero,new Vector2(.2f,.3f),1,true),1,0);
                body.CanonicalJoints[31] = new HumanVisionCanonicalJoint(new HumanVisionJoint(Vector2.zero,new Vector2(.4f,.6f),1,true),1,0);
                Call(owner, "RenderBody", 0, body, true, 1280, 720, camera, 1f, Vector2.zero, 1f);
                var points = root.GetComponentsInChildren<MeshRenderer>(true); var lines = root.GetComponentsInChildren<LineRenderer>(true);
                Assert.That(points[31].gameObject.activeSelf, Is.True);
                Assert.That(lines[31].gameObject.activeSelf, Is.True);
                var local = new Vector3(preview.rectTransform.rect.xMin + preview.rectTransform.rect.width*.4f,
                    preview.rectTransform.rect.yMax - preview.rectTransform.rect.height*.6f, 0);
                var expected = RectTransformUtility.WorldToScreenPoint(null, preview.rectTransform.TransformPoint(local));
                var actual = camera.WorldToScreenPoint(points[31].transform.position);
                Assert.That(actual.x, Is.EqualTo(expected.x).Within(.01f)); Assert.That(actual.y, Is.EqualTo(expected.y).Within(.01f));
                Call(owner, "RenderBody", 0, null, true, 1280, 720, camera, 1f, Vector2.zero, 1f);
                foreach (var point in points) Assert.That(point.gameObject.activeSelf, Is.False);
                foreach (var line in lines) Assert.That(line.gameObject.activeSelf, Is.False);
            } finally { Object.DestroyImmediate(root); Object.DestroyImmediate(canvas); Object.DestroyImmediate(camera.gameObject); }
        }

        [Test]
        public void TransparentCameraCompositionDrawsActualBoneAndPointsAndRetiresOnDisable()
        {
            var root = new GameObject("Composition renderer test");
            var canvas = new GameObject("Actual overlay canvas", typeof(RectTransform), typeof(Canvas));
            Texture2D read = null;
            Camera sceneCamera = null;
            Camera inspectionCamera = null;
            RenderTexture sceneTarget = null;
            var previousTarget = RenderTexture.active;
            try {
                canvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
                var previewObject = new GameObject("Offset fitted preview", typeof(RectTransform), typeof(RawImage));
                previewObject.transform.SetParent(canvas.transform, false);
                var preview = previewObject.GetComponent<RawImage>(); preview.rectTransform.sizeDelta = new Vector2(300, 180);
                preview.rectTransform.anchoredPosition = new Vector2(30, 20);
                Canvas.ForceUpdateCanvases();
                sceneCamera = new GameObject("Explicit foreground camera", typeof(Camera)).GetComponent<Camera>();
                int originalMask = sceneCamera.cullingMask;
                var owner = root.AddComponent<HumanVisionSkeletonOverlayer>(); owner.preview = preview; owner.foregroundCamera = sceneCamera;
                Call(owner, "Grow", 1); Assert.That((bool)Call(owner, "EnsureComposition"), Is.True);
                var camera = Field<Camera>(owner, "_renderCamera"); var target = Field<RenderTexture>(owner, "_target");
                Assert.That(camera.commandBufferCount, Is.Zero, "Skeleton drawing must use the ordinary camera culling/rendering pipeline.");
                Assert.That(camera.cullingMask, Is.Not.Zero);
                var pooledPoint = Field<MeshRenderer[]>(owner, "_pointRenderers")[31];
                var pooledLine = Field<LineRenderer[]>(owner, "_lines")[31];
                Assert.That(pooledPoint.enabled && pooledLine.enabled, Is.True);
                Assert.That(camera.cullingMask, Is.EqualTo(1 << pooledPoint.gameObject.layer));
                Assert.That(pooledLine.gameObject.layer, Is.EqualTo(pooledPoint.gameObject.layer));
                Assert.That(sceneCamera.cullingMask & camera.cullingMask, Is.Zero, "Only the assigned foreground camera excludes the reserved skeleton layer.");
                TestContext.WriteLine("Actual graphics: " + SystemInfo.graphicsDeviceType + "; " + SystemInfo.graphicsDeviceName + "; " + SystemInfo.graphicsDeviceVersion);
                var image = Field<RawImage>(owner, "_composition");
                Assert.That(image.transform.parent, Is.EqualTo(preview.transform)); Assert.That(image.raycastTarget, Is.False);
                Assert.That(target.width, Is.EqualTo(Mathf.RoundToInt(canvas.GetComponent<Canvas>().pixelRect.width)));
                var body = new HumanVisionBody();
                body.CanonicalJoints[30] = new HumanVisionCanonicalJoint(new HumanVisionJoint(Vector2.zero,new Vector2(.25f,.5f),1,true),1,0);
                body.CanonicalJoints[31] = new HumanVisionCanonicalJoint(new HumanVisionJoint(Vector2.zero,new Vector2(.75f,.5f),1,true),1,0);
                owner.drawJoints = false;
                Call(owner, "RenderBody", 0, body, true, 1280, 720, camera, 1f, Vector2.zero, 1f);
                Call(owner, "RenderObjects");
                var center = RectTransformUtility.WorldToScreenPoint(null, preview.rectTransform.TransformPoint(preview.rectTransform.rect.center));
                read = new Texture2D(target.width, target.height, TextureFormat.RGBA32, false);
                RenderTexture.active = target; read.ReadPixels(new Rect(0,0,target.width,target.height),0,0); read.Apply();
                Assert.That(read.GetPixel(Mathf.RoundToInt(center.x), Mathf.RoundToInt(center.y)).a, Is.GreaterThan(.9f), "Actual LineRenderer must contribute opaque pixels.");
                Assert.That(read.GetPixel(Mathf.RoundToInt(center.x), Mathf.RoundToInt(center.y + 30)).a, Is.LessThan(.1f), "Transparent background must preserve the preview.");
                TestContext.WriteLine("Bone center alpha=" + read.GetPixel(Mathf.RoundToInt(center.x), Mathf.RoundToInt(center.y)).a);
                owner.drawJoints = true; owner.drawBones = false;
                Call(owner, "RenderBody", 0, body, true, 1280, 720, camera, 1f, Vector2.zero, 1f); Call(owner, "RenderObjects");
                read.ReadPixels(new Rect(0,0,target.width,target.height),0,0); read.Apply();
                Assert.That(read.GetPixel(Mathf.RoundToInt(center.x), Mathf.RoundToInt(center.y)).a, Is.LessThan(.1f), "Hidden bones cannot leave a stale composition.");
                Assert.That(read.GetPixel(Mathf.RoundToInt(center.x-75), Mathf.RoundToInt(center.y)).a, Is.GreaterThan(.9f), "Actual MeshRenderer point must contribute opaque pixels.");
                int opaquePixels = 0, coloredPixels = 0;
                foreach (var pixel in read.GetPixels32()) if (pixel.a > 128) { opaquePixels++; if (pixel.r != pixel.g || pixel.g != pixel.b) coloredPixels++; }
                TestContext.WriteLine("Point target opaque pixels=" + opaquePixels + "; colored pixels=" + coloredPixels);
                Assert.That(coloredPixels, Is.GreaterThan(0));
                // A normal camera pointed at this same plane must see no pooled objects.
                int isolatedMask = sceneCamera.cullingMask; sceneCamera.CopyFrom(camera);
                sceneCamera.transform.SetPositionAndRotation(camera.transform.position, camera.transform.rotation); sceneCamera.cullingMask = isolatedMask;
                sceneTarget = new RenderTexture(target.width, target.height, 0, RenderTextureFormat.ARGB32); sceneTarget.Create(); sceneCamera.targetTexture = sceneTarget;
                sceneCamera.Render(); RenderTexture.active = sceneTarget;
                read.ReadPixels(new Rect(0,0,target.width,target.height),0,0); read.Apply();
                Assert.That(read.GetPixel(Mathf.RoundToInt(center.x-75), Mathf.RoundToInt(center.y)).a, Is.LessThan(.1f), "Ordinary scene cameras must not draw the pooled skeleton a second time.");
                // Scene-view inspection can see the actual enabled components on their image plane.
                inspectionCamera = new GameObject("SceneView inspection camera", typeof(Camera)).GetComponent<Camera>(); inspectionCamera.CopyFrom(camera);
                inspectionCamera.cameraType = CameraType.SceneView; inspectionCamera.cullingMask = camera.cullingMask;
                inspectionCamera.transform.SetPositionAndRotation(camera.transform.position, camera.transform.rotation); inspectionCamera.targetTexture = sceneTarget;
                Assert.That((bool)Call(owner, "EnsureComposition"), Is.True, "Scene-view cameras are intentionally allowed to inspect real pooled geometry.");
                Assert.That(inspectionCamera.commandBufferCount, Is.Zero); inspectionCamera.Render();
                read.ReadPixels(new Rect(0,0,target.width,target.height),0,0); read.Apply();
                Assert.That(read.GetPixel(Mathf.RoundToInt(center.x-75), Mathf.RoundToInt(center.y)).a, Is.GreaterThan(.9f), "A regular SceneView camera must draw the real enabled point without manual DrawRenderer calls.");
                RenderTexture.active = previousTarget;
                owner.enabled = false;
                // MonoBehaviour lifecycle callbacks do not execute automatically in
                // EditMode; exercise the production handler directly in this fixture.
                Call(owner, "OnDisable");
                Assert.That(Field<RenderTexture>(owner, "_target"), Is.Null); Assert.That(Field<Camera>(owner, "_renderCamera"), Is.Null);
                Assert.That(Field<RawImage>(owner, "_composition"), Is.Null);
                Assert.That(sceneCamera.cullingMask, Is.EqualTo(originalMask), "Disable restores the explicitly assigned camera mask.");
            } finally {
                RenderTexture.active = previousTarget;
                if (sceneCamera != null) Object.DestroyImmediate(sceneCamera.gameObject);
                if (inspectionCamera != null) Object.DestroyImmediate(inspectionCamera.gameObject);
                if (sceneTarget != null) { sceneTarget.Release(); Object.DestroyImmediate(sceneTarget); }
                if (read != null) Object.DestroyImmediate(read);
                Object.DestroyImmediate(root); Object.DestroyImmediate(canvas);
            }
        }
    }
}
