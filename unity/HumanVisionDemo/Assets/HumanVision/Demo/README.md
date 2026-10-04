# Camera demo and renderer

Purpose: camera/settings scenes and pooled skeleton visualization. Consumes stable
Unity API and the actual preview rectangle; creates MeshRenderer joint objects and
LineRenderer bones for the 32 semantic slots, with capacity from configured people.
Allowed: UGUI and HumanVision.Runtime. Forbidden: model files, tensor decoders and
native provider classes. Primary files: Live/HumanVisionSkeletonGraphic.cs,
HumanVisionSkeletonOverlayer.cs, HumanVisionCameraManager.cs and SceneControls.cs.
The formal Camera/Video/RTSP scenes share this object renderer. Null prefabs create
pooled spheres and lines; explicit compatible MeshRenderer/LineRenderer prefabs
remain supported. The existing component type, fields and GUID remain stable.
V1 CPU results use their existing COCO points and derived torso anchors. Missing
hand/foot points are hidden; available V1 hand landmarks remain visible. Runtime
results use the canonical buffer without synthesizing unavailable joints.

The renderer maps upright normalized image coordinates through the preview's
transformed RectTransform into an isolated camera image plane. This is display
geometry with no metric-depth meaning. A transparent display-sized target is
composed beneath the existing boxes/IDs and controls while the GUI stays in
ScreenSpaceOverlay. Source resolution does not determine the target dimensions.
Line width and point diameter are display reference units, multiplied by the
effective Canvas scale; each mode keeps its existing configurable 9/27 defaults.
Input orientation/mirroring is already applied upstream and is not repeated here.
Point MeshRenderers and bone LineRenderers stay enabled and use ordinary camera
culling/rendering; active objects control availability. No manual DrawRenderer or
CommandBuffer drawing is used. Scene View can inspect the real image-plane objects.
Reserve an unused user layer using `renderLayer` (default 30, valid 8-30; Unity
reserves 31 for Editor preview). Existing renderers on that layer cause an actionable
configuration error before pool allocation. The dedicated camera draws only this
layer. The explicitly assigned `foregroundCamera` temporarily excludes it and its
original layer bit is restored on disable/destroy or layer change, retaining other
mask edits made by the application. Other enabled game
cameras must exclude this layer themselves; otherwise rendering stops with an
actionable error rather than changing their masks. Camera changes are checked with
a reused buffer; the layer must remain exclusively reserved during rendering.
Disable hides the pool and retires the owned camera/target/composition; destroy also
releases pooled objects/materials. Warmed frames reuse geometry and buffers.
Focused checks: managed compilation, overlay geometry/rendering tests, public surface.
Symptoms: clipping, point/line size, orientation, touch obstruction. Change drawing
here without modifying recognition. Raw and sampled rates are separate in the HUD.
The native bottleneck block refreshes at 4 Hz and includes provider identity,
pipeline timing, adaptive sample state and separate drop counters. It does not log
per frame.

The camera/settings drawer exposes the three no-hands Android benchmark profiles.
Changing the selection takes effect on Apply/Start and recreates the native runtime;
an empty `runtimeProfileOverride` preserves the existing auto/forceCpu behavior.
