# Camera demo and renderer

Purpose: camera/settings scenes and batched skeleton visualization. Consumes stable
Unity API and preview rectangle; produces one canonical UI mesh for all people.
Allowed: UGUI and HumanVision.Runtime. Forbidden: model files, tensor decoders and
native provider classes. Primary files: Live/HumanVisionSkeletonGraphic.cs,
HumanVisionSkeletonOverlayer.cs, HumanVisionCameraManager.cs and SceneControls.cs.
Set explicit joint/line prefabs only when the old per-object renderer is required.
Default null prefabs select batching; old component type/GUID stays stable.
Focused checks: managed compilation, overlay geometry/rendering tests, public surface.
Symptoms: clipping, point/line size, orientation, touch obstruction. Change drawing
here without modifying recognition. Raw and sampled rates are separate in the HUD.
The native bottleneck block refreshes at 4 Hz and includes provider identity,
pipeline timing, adaptive sample state and separate drop counters. It does not log
per frame.

The camera/settings drawer exposes the three no-hands Android benchmark profiles.
Changing the selection takes effect on Apply/Start and recreates the native runtime;
an empty `runtimeProfileOverride` preserves the existing auto/forceCpu behavior.
