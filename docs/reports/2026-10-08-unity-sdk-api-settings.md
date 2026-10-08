# Unity SDK API/settings update — 2026-10-08

## Scope and authority

User-authorized one-component SDK API, KinectManager-informed queries, editable
UGUI settings Demo and Unity 2021/2022 compatibility. The later user instruction
authorizes GitHub/main and a prerelease after actual test-project verification.
The branch merges origin/main (771e296) before the update; unrelated Android
worktrees and user media remain outside release assets. Native V1 ABI, all 17
shipping DLL/SO files and the model/index/license payload retain preview.4 bytes.
Existing SDK managed files/meta remain byte-for-byte origin/main; additions are
SDK facade/query/UI/sample/docs/tests. Input version is 0.1.0-preview.3; SDK is
0.4.0-preview.5. Evaluation/distribution and device/FPS markers are unchanged.

## Delivered behavior

`HumanVisionSdk` supports coroutine initialization/shutdown, runtime capacity 1–8,
combined count/region configuration, sparse stable slots and long track IDs,
unknown/empty/occupied queries, 32-point validity/confidence/age-aware skeleton
queries and caller-owned copies, image/screen/world-plane positions, planar
angles/directions, image/stats and identity/occupancy/result/lifecycle events.
World coordinates are a configured Unity XY plane, not measured 3D depth. Real
Hand/Handtip/Thumb are returned only when the model actually supplies them.

GameObject/HumanVision/Create SDK creates one controller with Undo. HumanVision/
Create SDK settings demo assets creates a saved ordinary Prefab/scene without
replacing existing scenes. UGUI layout follows the referenced Setting scene
(1600x900, 72% preview/28% settings): three inputs, people, actual supported
quality, capture/mirror/drawing controls, draggable regions, advanced controls,
apply/save/reload/backup, status, safe stop and optional return. Save is atomic
with backup; applying/saving input commits only after actual Streaming. JSON is
configuration-only, outside frame processing; pose logs are optional/throttled.

Texture compatibility avoids direct Texture2D.isDataSRGB references, binds an
optional Texture property once and provides a legacy fallback. Font selection
uses LegacyRuntime.ttf in Unity 2022.2+ and Arial.ttf in older engines.

## Verification evidence

- Task1 RED missing query API; GREEN 28/28 query/config cases.
- Task2/compatibility RED missing facade/helper plus a real menu Undo failure;
  repaired facade/lifetime/compatibility focused GREEN 32/32.
- Task3 RED missing settings types and untitled-scene generation; GREEN 7/7.
- Final review RED 0/4 exposed native/Unity clock mismatch, exact two-argument
  ABI removal, premature active-options commit and lost Inspector defaults.
  All repaired; new/affected EditMode **41/41**, 0 failures/skips:
  `20261008-112935-794-results.xml`, packaged `20261008-120616-668-results.xml`
  and fresh packaged `20261008-120753-724-results.xml`.
- Packaged native PlayMode lifecycle **8/8**, 0 failures/skips:
  `20261008-113030-266-results.xml` and final packaged `20261008-120936-735-results.xml` (ready init, count/regions, invalid apply,
  repeated shutdown, disable, destroy, cancel, clock and failed-input retention).
- Fresh native Release CTest **376/376**, no skips, 30.52s:
  `tools/test/run_native_tests.ps1 -Fresh`, `out/native-tests.log`.
  Actual ignored models/golden fixtures were restored before the passing run;
  that test binary does not replace any shipping native.
- Unity 2021.3.45f1 Editor/Input/Runtime/Demo and Android-conditional C# compile
  PASS. Unity 2022.3.61t4 compatibility **3/3** PASS. This is the installed
  Tuanjie-derived editor, not a claim to have run stock 2022 or 2021.3.18.
  Unity 2021.3.18 was unavailable; its absent property is avoided by source.
- `Human-Vision-SDK-Test`: resolved new local UPM packages, Console 0, generated
  SDK Settings scene, actual CPU video through ApplySave. Five distinct results
  produced 215 successful joint/screen/world queries and 20 known slot states;
  actual stable ID/index mapping passed. 4→2 capacity + two regions produced five
  new results with count <=2. Stop clears observations; destruction while video
  streaming leaves no Runtime Host. `actual-project-probe.txt`, actual video/
  boxes/skeleton screenshot `actual-settings-video.png`. Test media is excluded.
  Saved scene is left open; final Editor recovery/compilation check is Console 0.
- Real sequential offline Input-only import + compilation, SDK import, all 20
  installed resource hashes, three old demos/new settings Prefab bindings and
  actual Windows CPU model/native init PASS (`out/release-preview5-offline`).
- Fresh final-package Windows player build PASS, `player-build-retry.log`.
  First build hit Windows 1455 virtual-memory pressure; limiting job workers
  and rerunning succeeded. Earlier failed build log is retained.
- Architecture/public surface, component catalog, autocrlf=true Git package
  bytes, package closure/GUID/native dependency checks PASS. Snapshot packaging
  regression **15/15** PASS. Candidate has 541 files, 297 GUIDs, 8 Android SO,
  9 Windows DLL and 20 indexed runtime resources.

## Broad regression limits (not an all-green claim)

The initial non-hardware broad package run was **226 PASS / 2 FAIL / 32 SKIP**,
`20261008-113908-226-results.xml`. MissingInputPluginIsActionable requires the
native plugin absent; a separate genuine native-absent installation passes that
case **1/1** (`input-absent/result.xml`). UnsupportedPortraitPreviewContinuesBut-
InferenceFails received an unlocated cross-fixture Native Collection leak log;
its cause is not claimed resolved. Thirty skips require approved Android model
fixtures; two require a dedicated quality-q2 scratch project. Later broad/old
renderer runs encountered Unity Camera.Render hangs and Null-renderer SIGSEGV;
logs remain and no passing coverage is fabricated. The new UGUI actual video
path and all new/affected cases passed. These unrelated legacy/environment
checks do not establish complete renderer/platform regression acceptance.

Independent fresh-context review confirms the four fixes and unchanged native/
model/old SDK closure, and finds no new-code release blocker. Its bounded verdict
is recorded in `2026-10-08-unity-sdk-api-review.md`. Physical-device acceptance,
real hand observations and 30 fresh complete FPS per person remain pending.

## Reproduction and publication

Run `tools/test/verify_unity_sdk.ps1` with the four new EditMode class filters and
`-Package -Graphics`; run HumanVisionSdkRuntimeTests with PlayMode. Use the
project probe with a local private video configuration for actual API/UI checks.
Build immutable archives with `package_release_snapshot.py --authority tools/
package/release-preview5-authority.json --output <new directory>`. The release
contains two tgz, two separate unitypackage, README, indexes/provenance and SHA256.
Input imports first. Tag the verified source commit, verify remote Git import,
upload a draft, download all eight assets into another directory and compare
hashes before publishing. Network publication receipts live in out; final tag
and Release URL identify the published source. No user media/screenshots are
included in public artifacts.

## Preview 6 user-requested follow-up

Windows uses the published fixed model, so its model-level dropdown now explains
why it cannot select a level. Android NCNN/Vulkan capabilities prepare and inspect
the real indexed quality catalog before input/native initialization; admitted
low/medium/high choices show actual dimensions. Missing or invalid catalogs show
an actionable message rather than advertising unsupported choices.

Diagnostics record startup, OS/device/Unity/SDK, active runtime/profile/model and
analysis dimensions, configuration/application elapsed time, input/native errors,
statistics, region states, stable user events, stop and destruction. Optional
joint records include confidence/provenance/time and coordinates. Unity warnings
and exceptions are captured; log rotation retains headers; loaded retention is
honored; I/O failures appear in the panel. RTSP credentials and compound tokens
are redacted. Default intervals are bounded; frame processing remains asynchronous.

The HumanVision product menu has four groups: Create SDK, Create SDK settings demo
assets, Examples and Install Packaged Models. Existing legacy builders remain under
Tools/Human Vision/Legacy Examples. Private acceptance scripts in the test project
were moved by menu annotations to Tools/Human Vision/Development.

Fresh verification receipts under out/sdk-api-verification:

- Initial diagnostics RED **0/5**, 20261008-123615-837-results.xml.
- Reviewer edge-case RED **0/4**, 20261008-125802-554-results.xml.
- Source settings GREEN **18/18**, 20261008-130058-040-results.xml.
- Final packaged affected EditMode GREEN **50/50**, no skips,
  20261008-130558-306-results.xml.
- Final packaged native PlayMode GREEN **9/9**, no skips,
  20261008-130302-411-results.xml. Includes failed native input logging before
  the first successful Apply; failed input does not commit active/saved options.
- Fresh packaged Windows player build **exit 0**, player-preview6-build.log.
- Snapshot-packaging regression **15/15**, architecture, public-surface and
  source/package synchronization PASS.
- Actual Human-Vision-SDK-Test Unity 2021.3.45f1 video probe COMPLETE PASS:
  five distinct observations, valid screen/world/joint queries, known regions,
  4-to-2 capacity/region updates, stop -> unknown occupancy, and streaming destroy
  -> no Runtime Host. Diagnostic assertions cover session/device/init/apply,
  statistics/joints/confidence/user entered/stop/controller destroy/session end.
  Screenshot sdk-settings-diagnostics-preview6.png shows real video/boxes/bones
  and readable fixed-model explanation. Four main groups confirmed by reflecting
  actual loaded MenuItem attributes. User project left stopped in Settings scene.

Stopping Play Mode produced one UnitySkills HTTP response ThreadAbort error,
recorded in actual-console-after-stop-preview6.json; it is not an SDK exception.
After retaining that evidence and clearing the Console, final idle errors are 0
(actual-console-final-preview6.json). This does not erase the historical error.

The visible editor was restored after an earlier verification launch had hidden
its window; no unrelated Unity process was terminated. Preview 5 was already
tagged but its draft was never published. These follow-up changes ship as SDK
0.4.0-preview.6 / Input 0.1.0-preview.4; preview 5 is not retagged. The new immutable
authority is tools/package/release-preview6-authority.json. Final network/import/
download receipts are written to out after tagging the verified source commit.
