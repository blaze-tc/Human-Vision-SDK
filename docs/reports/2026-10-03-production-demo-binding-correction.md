# Formal Demo binding correction — 2026-10-03

The unified Demo forced Windows CPU execution with independent hand inference,
whereas the previously accepted PC Demo used DirectML with hands disabled.
The private Android acceptance fixture also selected TopDown instead of the
previously accepted integrated YOLO640 model. Both were composition regressions.

The four managed input/configuration components now restore explicit Windows
DirectML/CPU selection, retain Android's baked Runtime Mode, and accept exactly
one `manifest.json` or `modelpack.json`. Missing/ambiguous manifests fail.
After failed initialization, Apply schedules reinitialization even when no
contract has been loaded. Native ABI, model decoding and inference are unchanged.

Independent spec and quality review of the frozen 16-file v2 candidate PASS.
Actual production ApplyShared controlflow with declared boundary doubles:
RED reproduced false success, GREEN3; these are not native execution tests.
Real Unity loader checks passed8/8. Composition helper rejects modified Android
model/index input before writing output. The actual combined data closure is
11 exact indexed files; current Android SDK/Input binaries are retained, because
the earlier accepted Android native library lacks the current retirement APIs.
Android YOLO bytes remain a local evaluation composition, not a published pack.

Root imported immutable `out/input/production-correction/user-packages-v10`
into the already open `E:/UnityProject/Human-Vision-SDK-Test`. All419 package
files and231 distinct GUIDs verified; both native libraries and the complete
Input package are unchanged. Installed runtime index and11 file hashes match.
Scene, capacity4, user regions and caches are preserved. A temporary probe was
initially compiled against old assemblies; the import-order failure is retained,
packages were explicitly resolved, and the new package/probe compiled cleanly.

Fresh current-project EditMode results: profile selection3/3, renderer objects6/6,
plane mapping8/8. Real JsonUtility/settings-store persistence passes. Actual
shared UI toggled running DirectML→CPU→DirectML; diagnostics confirm requested
and actual backend match and hands remain disabled. Actual failed-first-native
initialization recovery has not been injected in the user's running scene.

Two15-second formal VideoDemo diagnostic samples (120 records total) show native
FPS median24.8535, local age median87.3025ms/P95 117.323ms, and active Mesh points
plus enabled composition in120/120 records. Actual screenshot shows upright
video and matched skeleton objects. These are sampled local diagnostics using a
25FPS video, not calibrated physical complete-observation throughput acceptance.
They cannot qualify the Android30FPS gate or seven-person coverage: existing
user capacity/regions intentionally remain4.

The first reviewed renderer uses explicit command-buffer rendering of disabled
Renderer components. The user subsequently clarified ordinary enabled scene
objects are required; that separate bounded simplification is active. Do not
claim its implementation from the above v2 renderer evidence.

Evidence remains under `.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/`:
`model-binding-correction/{candidate-source-v2,independent-review-v2.md}` and
`production-correction-user-integration/`. Windows/Android player packaging,
formal Android Camera/Video/RTSP follow and30FPS physical acceptance remain open.
No Release or main merge is authorized before final user acceptance.
