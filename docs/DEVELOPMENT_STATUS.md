# 0.4 v2 implementation (2026-09-10) — ACTIVE

User approved `docs/plans/040-v2/2026-09-10-humanvision-040-master-v2.md`.
Architecture authority: SDK_040_PLUGIN_ARCHITECTURE_ADDENDUM.md in that directory.
Completed foundation: V1 ABI/Unity contract snapshots; versioned C plugin ABI and
generic Runtime Host/registry. Native regression: 36/36 passed via
`tools/test/run_native_tests.ps1`; Windows/Android builds passed via
`tools/package/build_live_native.ps1`. Public-surface checker intentionally retains
three existing model-path findings until semantic Unity migration.
Task 3 complete (2026-09-11): capability resolution, validated ModelPacks and Profiles.
`tools/test/run_native_tests.ps1 -Fresh`: 40/40 PASS, 7.88 seconds.
`tools/package/build_live_native.ps1 -Fresh`: Windows x64 and Android ARM64 PASS.
Regression first exposed automatic selection accepting a pipeline as a backend;
selection now checks plugin type as well as capability, and retains fallback reasons.
Fresh builds also exposed MSVC localized include-prefix encoding preventing Ninja
header dependencies from being recorded. UTF-8 command code page fixes this;
`ninja -t deps` now lists humanvision_plugin.h in both Windows build directories.
Task 4 complete (2026-09-11): existing detector/pose adapter registered as
`pipeline.legacy`; real image/model output reaches RuntimeHost with semantic joints
and original timestamps. Nose coordinates match the locked reference within 1.5 px.
`tools/test/run_native_tests.ps1`: 41/41 PASS, 9.02 seconds.
After final metadata/ROI validation edits, `-Filter LegacyPlugin`: 1/1 PASS.
`tools/package/build_live_native.ps1`: Windows x64 / Android ARM64 PASS.
The adapter has no identity/region ownership, does not synthesize missing hands,
and uses the legacy CPU backend pending backend-plugin migration.
Current milestone: implement/refactor Backend Plugins and session diagnostics.
Latest increment: BackendFactory provides ordered creation fallback, failure
diagnostics and independent module/session lifetime. Legacy detector and pose now
request injected backend sessions through HostServices instead of constructing ORT.
`tools/test/run_native_tests.ps1`: 46/46 PASS (10.39 seconds), including real-model
golden integration and creation-fallback lifetime tests.
`tools/package/build_live_native.ps1`: Windows x64 / Android ARM64 PASS.
Optional QNN and final Task 5 review remain; generic run-time provider retry is not
implemented (NNAPI retains its own recovery). No device performance claim.
Latest Task 5 increment: compiled-platform DirectML/NNAPI plugin query added.
Session diagnostics now preserve NNAPI registration, session-creation and run-time
fallback errors, and report the current configured provider. Accelerator flag is
provider configuration only, not measured graph coverage or device acceleration.
Verification: full native regression 44/44 PASS; subsequent capability-query test
and final focused backend/diagnostic suite 4/4 PASS. Final Windows x64 and Android
ARM64 builds PASS via `tools/package/build_live_native.ps1`.
Remaining Task 5: optional QNN integration and final review;
physical accelerator execution remains user acceptance.
Task 5 in progress: `backend.ort.cpu` now exposes real single-input float32 tensor
inference through the C plugin ABI, with shape/overflow validation and CPU diagnostics.
Unsupported accelerator requests fail explicitly. BackendPlugin fixture tests added.
Latest verification: `tools/test/run_native_tests.ps1` 43/43 PASS (10.37 seconds);
`tools/package/build_live_native.ps1` Windows x64 / Android ARM64 PASS.
Task 5 is not complete and this change does not establish phone performance.
New production pipelines and Unity V2 integration are not yet implemented.
Later runtime/model/service/Unity milestones must pass their preceding automated gates.
Maintenance documentation and architecture guards are release requirements.
Automated non-hardware tests are now authorized and required by the new plan.
USER MANUAL ACCEPTANCE PENDING: Android camera, FPS, latency, accuracy, thermal,
1/2/4/6/8 people, RTSP. No physical-device performance claims.
Cleanup is limited to audited regenerable artifacts; user data and dependencies stay.
User instruction: preserve caches and continue development; cleanup is deferred.

Historical preview.5: source/tag e8f16eb pushed; Release remains draft as work moved
to 0.4. This does not constitute 0.4 implementation or acceptance.

---

# 0.3.0-preview.5 Android pose continuity (2026-09-10)

Active user request: analyze the supplied Android recordings and fix flicker/stalls.
Evidence: detector 535-892 ms, intermittent zero-body output with people visible.
Root cause in preview.4: detector age expiry reset all tracks; pending detector
requests kept CPU inference busy. Pose crop refresh was absent.
Implemented fresh-pose crop feedback, preservation against delayed detector updates,
pose validity rejection, idle-only detector submission with 500/1000 ms search pauses,
and explicit empty-result HUD. No joint interpolation or stale joint republication.

Acceptance for user device retest: sustained current-image skeletons while stationary
and moving; disappear after leaving; reacquire on return; preserve region IDs and hands;
compare valid Pose FPS and result age, including two people and orientation changes.
30 fresh complete skeleton FPS/person for eight people remains NOT accepted.

Builds: tools/package/build_live_native.ps1 (Windows x64 and Android ARM64) PASS;
tools/package/compile_managed.ps1 PASS (existing CS0649 warning only).
Runtime/unit/integration/phone tests omitted per user instruction.
Archive check: out/inspect_release035.py PASS: 59 Unity assets, 141 UPM files,
77 unique GUIDs; manifests, models and archive bytes match.
Publication target: main and v0.3.0-preview.5.

---

# 0.3.0-preview.4 Android detector stall correction (2026-09-09)

Active user request: improve persistent skeleton stutter. User screenshots show
1073–1086ms detection,60–86ms pose,0.9FPS and1811–2311ms source age.
Implemented Android CPU detector on a separate bounded latest-request worker;
pose remains on current frames with AUTO acceleration. Detector requests/results
carry region revision, dimensions and timestamp;1200ms maximum crop source age,
250ms bounded non-mutating crop extrapolation, no lost-track resurrection.
Windows retains sequential inference. HUD reports recent throughput and parallel stages.

Commands: tools/package/build_live_native.ps1 PASSED (Windows/Android, no tests).
tools/package/compile_managed.ps1 PASSED (existing CS0649 warning only).
Runtime/unit/integration/phone tests not run per user instruction. This is not
an eight-person30FPS acceptance result; actual device improvement awaits user testing.
Archive verification: out/inspect_release034.py PASSED:59 Unity assets,141 UPM files,
77 unique GUIDs; model, manifest and archive byte hashes match.
Publication target: main / v0.3.0-preview.4.

---

# 0.3.0-preview.3 mobile latency and drawer (2026-09-09)

Active user request: fix landscape GUI clipping/occlusion with a retractable panel,
reduce actual Android skeleton lag, and carry forward model GUID correction.

User evidence: camera/render30FPS, pose2.2FPS, result age599–731ms.
Changes: one scrollable animated drawer containing all GUI; NNAPI AUTO with CPU
fallback, full CPU graph optimization, Android detector interval2 with500ms crop
reuse bound, latest-frame refresh while inference runs, stage timing display,
rotation tracking reset, independent UPM model GUIDs.

Commands: tools/package/build_live_native.ps1 succeeded (Windows x64 and Android
ARM64; BUILD_TESTING=OFF). tools/package/compile_managed.ps1 succeeded (Runtime,
Demo, Editor, UNITY_ANDROID; existing CS0649 warning). No runtime tests executed.
Phone acceleration coverage, latency and fresh pose FPS remain user acceptance items.
Archive verification: out/inspect_release033.py passed (59 Unity assets,141 UPM files,
77 unique GUIDs; manifest, model, gzip header and archive bytes match). Both UPM
model GUIDs differ from the actual Unity project StreamingAssets GUIDs.
Publication target: main and v0.3.0-preview.3.

---

# 0.3.0-preview.2 mobile follow-up (2026-09-09)

Active user-authorized work: responsive GUI in both scenes, dynamic phone rotation,
inverted camera correction, missing live skeleton diagnosis, and readable raised-hand example.
Implemented clockwise display / inverse UV rotation, old-orientation invalidation,
one-time live GPU row-order calibration, configurable 3000 ms overlay source-age cap,
and a separate 1500 ms gesture age cap. GUI uses safe-area units and keeps region editing
in physical screen coordinates. Existing camera scenes add the gesture component automatically.

Validation: `tools/package/compile_managed.ps1` succeeded for Runtime, Demo, Editor
and UNITY_ANDROID conditional Demo. Existing CS0649 warning only. No native changes;
native binaries reuse the previous build. No runtime/unit/integration/camera tests run,
per user instruction. Device orientation, visibility and performance await user testing.
Archive verification: out/inspect_release032.py passed: 59 Unity assets, 141 UPM files,
77 unique GUIDs, all manifest/model hashes and gzip header verified. UPM payload
is byte-identical to the local tgz. Publication target: main and v0.3.0-preview.2.

---

# DEVELOPMENT_STATUS.md

# HumanVisionSDK Development Status

## 0.3.0-preview.1 delivery checkpoint - 2026-09-09

Implementation and Windows/Android native + managed compilation completed.
- Six hand endpoints are decoded from the 133-landmark model and published with
  the same snapshot/sequence as COCO-17 bodies; the legacy ABI remains unchanged.
  Palm is derived from five actual hand-model landmarks and marked as derived.
- Independent sphere/LineRenderer overlayer with line/joint size controls; two
  generated scenes (camera and settings) with navigation. Original region move/
  resize semantics remain unchanged; camera-space mapping accommodates the new canvas.
- Android latest-camera preview is decoupled from inference, no history texture
  ring, one useful readback at a time, 640x640 analysis cap preserving aspect,
  ORT pools limited to two threads without spinning. Camera-only comparison and
  device details are user-provided; new phone FPS is NOT measured.
- UPM directory and tgz include scripts, platform-selected native plugins, model
  files and an Editor/build installer for StreamingAssets. Git/release target is
  the user-confirmed Human-Vision-SDK repository, which is currently private.

Validation: `tools/package/build_live_native.ps1`, `compile_managed.ps1`,
`package_live_sdk.py`, `package_upm.py`; all compilation succeeded, including
Android conditional C# and the UPM model installer. Static archive inspection:
57 Unity assets; 137 UPM files; 75 unique GUIDs; content/model hashes matched;
Unity-compatible gzip inner filename preserved. A new isolated Unity launch
was canceled at OS startup and its REST endpoint was unavailable, so no new
Unity import/scene/runtime pass is claimed. No unit/integration/camera tests ran.
Evidence: `out/build-native030.log`, `out/build-managed030.log`,
`out/inspect_release030.py`, `out/releases/0.3.0-preview.1/SHA256SUMS.txt`.

## User priority update - 2026-09-07

**Current stage:** Independent SDK skeleton upgrade and validation.
**Current milestone:** 0.3.0-preview.1: genuine hand endpoints, independent skeleton rendering/settings scene, Android live-preview performance, and UPM/GitHub delivery (user update 2026-09-09).
The user confirmed Editor camera operation and corrected the region request: existing
rectangle movement/resizing works and must be preserved. Phone validation is now
OnePlus 9 Pro LE2120, Snapdragon 888, Android 14, 12 GB RAM (user screenshot).
Camera-only APK is smooth; SDK analysis APK is choppy. Preserve previous no-runtime-test
instruction; compile/package verification is agent-owned. User explicitly authorizes
main and Release publication to blaze-tc/Human-Vision-SDK (confirmed destination).
See SDK_030_PLAN.md.
The user confirmed following on `4859224-uhd_3840_2160_25fps` and explicitly
requested code delivery without running tests; hardware/runtime acceptance is
now user-owned. Builds needed to produce native libraries/packages are allowed;
do not run unit, integration, Play Mode or camera tests for this new work.
See `SDK_030_PLAN.md`. Hand integration is implemented for this preview; real-device quality/performance acceptance remains pending.

**Historical delivery checkpoint:** 0.2.0-preview source implementation complete; Windows
x64 and Android ARM64 native builds completed with BUILD_TESTING=OFF. Managed
Runtime/Demo/Editor and Android-conditional Demo compilation completed using
Unity2021.3.45f1 references. No tests, camera connections, Play Mode, APK build,
or new-package import were executed after the user's no-test instruction.
User acceptance is pending. Deliverables live under `out/releases/0.2.0-preview/`;
usage: `SDK_LIVE_CAMERA_GUIDE.md`. The existing imported demo was not overwritten.

2026-09-08 import follow-up: the user reports an "already imported" dialog.
The active Unity project has none of the 54 delivered asset paths. Archive
inspection found zero explicit GUID directory entries in our generated package,
whereas Unity's bundled TMP package has them. The packager now writes explicit
GUID directories using USTAR format, preserving asset GUIDs, and delivers
`out/releases/0.2.0-preview-importfix/`. Archive content/metadata comparison
was the verification scope; the user subsequently confirmed that this first
fix still returned "Nothing to import". The directory-only diagnosis was incomplete.

2026-09-08 second import repair: isolated Unity 2021.3.45f1 native
`PackageUtility.ExtractAndPrepareAssetList` inspection reproduced zero entries
for importfix. Changing only gzip FNAME to `archtemp.tar` (or omitting FNAME)
made the same one-asset payload return one entry; tar format, timestamp and
mode variants alone did not. Python tarfile had written the outer unitypackage
filename into gzip FNAME. The packager now uses an explicit GzipFile wrapper.
The full importfix2 package returns 54 entries, all `exists=False`, while the
old full package returns zero in the same isolated project. Commands used
Unity `-batchmode -nographics -quit -job-worker-count 2 -projectPath
"E:/Project/Human Vision SDK/out/package-inspection" -executeMethod
PackageInspection.Inspect`; evidence: `out/package-gzip.log` and
`out/package-final-inspection.log`. No SDK runtime/Play Mode/camera tests ran.
Deliverable: `out/releases/0.2.0-preview-importfix2/`. Interactive import in the
user project remains user-owned; archive parsing is now verified by Unity itself.

Implemented: WebCamTexture capture and Android permission flow, native FFmpeg
RTSP TCP/UDP decoding/reconnect, common oriented frame bridge, native region
masking/one-body-per-region selection, versioned region assignments, draggable
settings/save-load and KinectManager-inspired image-space getters. Region
overlap is explicitly rejected. Android uses ONNX CPU; RKNN, real hand endpoints
and eight-person30FPS remain unimplemented/unaccepted.

Build commands: `tools/setup/prepare_directml_runtime.py`,
`tools/setup/prepare_live_dependencies.py`, `tools/setup/create_ffmpeg_imports.ps1`,
`tools/package/build_live_native.ps1`, `tools/package/compile_managed.ps1`,
`tools/package/package_live_sdk.py`. Build logs are `out/build-live-windows.log`,
`out/build-android-live.log`, `out/build-managed-live.log`. Android NDK21.3's
missing filesystem status symbol was resolved using stat on Android; both
platforms link successfully. This is build evidence, not runtime acceptance.

The user requested visible, aligned, smooth Unity skeleton following before
continuing SDK expansion. This authorizes focused display fixes and measured
Windows acceleration experiments now. Preserve the D0 CPU regression path;
do not claim zero latency or fresh 30 FPS from interpolation. S1's wholebody
models remain offline candidates until S2 resumes.
**Active plan:** `docs/SDK_SKELETON_EXECUTION_PLAN.md`.

The user has requested independent SDK skeleton completion and acceptance
before AzureKinectExamples integration. The confirmed expanded target is
Windows + Android, 1-8 concurrent people, 30 actual complete skeleton updates
per second per person, and both hands' Kinect-compatible hand/handtip/thumb
outputs based on real inference. See `SDK_SKELETON_REQUIREMENTS.md` for the
requirements, measurement rules and unresolved hardware/test conditions.

Android hardware baseline is now user-confirmed as RK3588, based on the supplied
`RK3588 Brief Datasheet.pdf`: four Cortex-A76 plus four Cortex-A55 cores and an
INT8 NPU rated at 6 TOPS. This records the target hardware, not a performance
pass. Exact board/RAM/firmware and sustained skeleton throughput remain to be
verified on hardware.

The D1.0 schedule below is the previous implementation plan, pending
reconciliation with this priority. S0/S1 evidence is recorded below; SDK hand,
Android and performance acceptance remains pending. Historical D0 verification is
preserved below. AzureKinectExamples integration is deferred until SDK acceptance.

## S0 - Baseline and model feasibility - complete (2026-09-07)

- Exact commands/results: `docs/validation/S0_BASELINE_REPORT.md`.
- Native Debug and Release incremental builds passed; CTest 29/29 each,
  12.04 and 8.07 seconds respectively, including real ONNX golden tests.
- New scene-media contract test initially passed 1/1 (job `9a77360d`): the
  imported startup path was already correct. No RED or production path fix is
  claimed. Full Unity EditMode job `363c338e`: 38/38 passed in 19 seconds.
- Windows x64 build succeeded, 438,964,244 bytes / 9.28 seconds.
- Dynamic-video playback and native results observed, errors/readback errors
  zero. Measured inference about 4-5 FPS. Captured frames without overlays are
  recorded as unresolved following-quality evidence, not a visual pass.
- Real two-person sequential means: MaxBodies 1/2/8 gave 192.348/209.285/202.313
  ms and 1/2/2 observed bodies. No eight-person throughput claim.
- Model assessment: `docs/validation/S0_MODEL_FEASIBILITY.md`; selected next
  experiment is official RTMPose-s/m 133-point wholebody, with real hand/foot
  outputs, retained ONNX baseline, and RKNN conversion still unverified.
- Next: S1 reference/golden experiment; production native model/schema changes
  wait for S2. Azure integration and segmentation remain deferred.

## S1 - Wholebody reference - complete (2026-09-07)

- Exact reproduction and limitations: `docs/validation/S1_MODEL_REFERENCE_REPORT.md`.
- New contract tests started with expected missing-module RED; final reference
  suite 9/9 passed in 0.029 seconds.
- Official small/medium FP32 ONNX graphs checked and hashes pinned in
  `models/wholebody/candidates.json`; real-image golden records saved in
  `tests/golden/s1/`. Assets are evaluation-only.
- PyTorch/ONNX comparisons passed with ORT CPU threads 1 and 4 for both models:
  source-coordinate error zero, raw errors below 0.0000061.
- Actual dynamic batches 2/4/8 executed and matched single-input results.
- Small/four-thread serial eight-ROI mean 89.512 ms, medium 187.943 ms, excluding
  detector and other stages. This is repeated-ROI compute cost, not eight-person
  recognition or 30 FPS acceptance.
- Real model hand endpoints present; palms explicitly derived from hand-model
  root/MCP landmarks. Gloved reference image does not prove finger accuracy.
- Selected small FP32 for S2 native adapter. Production runtime unchanged by S1;
  rich native results, following quality, platform acceleration and field
  acceptance remain pending. Azure integration remains deferred.

## Previously verified development checkpoint

**Status date:** 2026-09-05  
**Historical stage:** D1 - RTSP IPC Integration  
**Historical next milestone:** D1.0 RTSP IPC Input (deferred)  
**Historical implementation state:** D0.4 Unity local-video vertical slice completed and verified in the imported AzureKinectExamples project: asynchronous frame submission, real RTMDet/RTMPose inference, stable tracking, video/box/ID overlay with a Kinect-style display skeleton derived from COCO-17, performance HUD, and a Windows x64 standalone build.

## Immediate user-visible target

The next meaningful checkpoint is **D1.0 RTSP IPC Input**:

```text
RTSP IPC -> decoded RGB frame -> HV_SubmitFrame -> RTMDet -> Tracker -> RTMPose
    -> Unity video + BBox + TrackId + COCO17 skeleton + performance HUD
```

The verified local MP4 path remains the regression baseline while RTSP reconnect/error behavior is added.

## Milestone state

- [x] D0.0 Repository & Build Bootstrap
- [x] D0.1 Python/OpenMMLab Reference + ONNX Contract
- [x] D0.2 Native ONNX Runtime + RTMDet
- [x] D0.3 RTMPose + Tracker + Native Video Benchmark
- [x] D0.4 Unity Local Video Demo
- [ ] D1.0 RTSP IPC Input
- [ ] D1.1 Real 1~4 Person Field Validation
- [ ] D1.2 Demo Stabilization & Decision Report

## Scope lock

Until D1.2 is accepted:

- no Android
- no RKNN
- no segmentation/matting
- no TensorRT/CUDA optimization
- no action recognition

## Latest verification

### D0.4 Unity Local Video Demo - PASS

- Date: 2026-09-03
- Implementation commit: `058d06258cd1db25f2291ea6930542636d39033e`
- Unity host: Windows x64, Unity 2021.3.45f1, project `E:\UnityProject\Human-Vision-SDK-Test`
- Version decision: the user explicitly approved keeping the active Unity version. Unity 2021.3.45f1 is therefore the accepted D0.4 host exception to the original Unity 2022.3 LTS plan; no editor upgrade was performed.
- Integration context: AzureKinectExamples remains imported in the target project. HumanVision uses a privately named ONNX Runtime DLL so its 1.29 runtime can coexist with the example package's public ONNX Runtime binaries.

Expected RED verification:

- Scene contract job `61db16fe`: 0/1 passed before the D0.4 scene existed.
- Plugin isolation job `502281c0`: 0/2 passed while HumanVision and AzureKinectExamples both exposed compatible public `onnxruntime.dll` names.
- Native-result error propagation job `272b2c74`: 0/1 passed before a failed `HV_GetLatestResultMeta` call was surfaced to managed callers.

Focused GREEN verification:

- Scene contract job `26746a14`: 1/1 passed after creating `Assets/Scenes/HumanVisionD04Demo.unity`.
- Plugin isolation job `b7570548`: 2/2 passed after linking HumanVision against `humanvision_onnxruntime.dll`.
- Native-result error propagation job `0d9e2c31`: 1/1 passed after `HumanVisionSession.PollLatestResult()` began throwing actionable native metadata failures while retaining `NoNewResult` as a non-error.

Fresh native regression:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-release
```

- Debug CTest: PASS, 29/29 tests, 0 failures, 11.20 seconds.
- Release CTest: PASS, 29/29 tests, 0 failures, 7.38 seconds.
- `humanvision_native_dependency_isolation` verifies the private runtime dependency and rejects a public ONNX Runtime dependency from `humanvision.dll`.

Fresh Unity verification after the final DLL copy and editor restart:

- UnitySkills instance: `HumanVisionSDKTest_F988EAA7`, Unity 2021.3.45f1.
- Scene: `Assets/Scenes/HumanVisionD04Demo.unity`, not dirty, exactly three roots (`HumanVision Pipeline`, `HumanVision Canvas`, and `EventSystem`).
- Full EditMode job `01dee637`: PASS, 28/28 tests, 0 failures, 7 seconds.
- Source-to-target comparison: PASS, 65/65 committed HumanVision source/scene/project files matched the imported Unity project by SHA-256.
- Play-mode observation: initialization succeeded; the real two-person clip played at 436x346 and 5 fps; `BodyCount=2`; runtime `MaxBodies=4`; `ResultSequence` advanced from 52 to 113; manager and video-source error strings remained empty.
- GPU readback counters remained 0 drops / 0 errors. Unity Console contained 0 errors and 0 warnings during this run.
- The runtime overlay visibly rendered two tracked boxes, IDs, and COCO-17 skeletons. The HUD reported input/inference FPS, detector/pose/total timing, submitted/processed/dropped frames, and readback counters.
- Captured evidence: `E:\UnityProject\Human-Vision-SDK-Test\Assets\Screenshots\humanvision_d04_runtime.png`.

Post-acceptance visual skeleton correction:

- Date: 2026-09-03
- Fix commit: `ad4011254d604e4d9f0f9ef57d6fe9a2539319f2`
- User-visible symptom: the original bone lines formed large head/shoulder and shoulder/hip triangles, so the overlay did not read as a human skeleton even though the COCO-17 joint indices and coordinates were correct.
- Root cause: COCO-17 has no explicit neck, spine, or pelvis-center joints. The first display topology connected nose directly to both shoulders and each shoulder directly to its corresponding hip.
- Fix: derive a neck anchor from the shoulder midpoint and a pelvis anchor from the hip midpoint, then render a central `head -> neck -> pelvis` spine with anatomically branched shoulders, arms, hips, and legs. Model output, native ABI, tracking, and coordinate mapping remain unchanged.
- Expected RED job `6497ca37`: 3/5 passed; the new topology and derived-anchor midpoint tests were the two expected failures.
- Focused GREEN job `ea7a68a2`: 5/5 passed.
- Final full EditMode job `21968242`: PASS, 30/30 tests, 0 failures, 6 seconds; Unity Console contained 0 errors.
- Visual runtime evidence: `E:\UnityProject\Human-Vision-SDK-Test\Assets\Screenshots\humanvision_d04_skeleton_fix.png`; scene remained clean with the same three root objects.
- Windows x64 Demo rebuild: PASS, 431,123,235 output bytes, 8.51 seconds.

Kinect-style skeleton hierarchy refinement:

- Date: 2026-09-03
- User references: `joint-hierarchy.png` (963x1036, SHA-256 `FF5C02424C7CCFB910F6FF79526F9C5B2D4A0A1441E7606E8E1BE651E0DA7B89`) and `Kinect.png` (1057x893, SHA-256 `632CABC733C1B210B15077E2CE075C5AA45F49324D6BE9B315C97ACCB4ED3059`).
- Remaining symptom: the neck/pelvis-only correction still lacked the Kinect spine-navel, spine-chest, head, and clavicle hierarchy, so its silhouette remained a COCO keypoint overlay rather than a Kinect-style skeleton.
- Root cause: the display topology exposed only two derived anchors and rendered markers for the 17 raw COCO joints. It could not express Kinect's central spine or shoulder-belt parent chain even though the source joint coordinates were correct.
- Fix: retain the native/public COCO-17 result unchanged and derive seven display-only anchors: pelvis, spine navel, spine chest, neck, head, and left/right clavicles. The overlay now renders the supported Kinect hierarchy as 24 anchors and 23 parent-child bones, with circular markers for both measured and derived joints.
- Unsupported Kinect endpoints are intentionally omitted: COCO-17 contains no hand, hand-tip, thumb, or foot-tip observations, so the display does not invent them or claim depth/Z tracking.
- Expected RED job `06d3ef04`: 2/6 passed; four new hierarchy, anchor-placement, dependency-suppression, and valid-range assertions failed against the previous topology.
- Focused GREEN job `ade54dd2`: PASS, 6/6. Post-refactor focused job `ed43236b`: PASS, 6/6.
- Full EditMode job `63bf091a` and final pre-commit job `cfe83129`: PASS, 31/31 tests each, 0 failures, 11 seconds each. Unity compilation completed with 0 errors.
- Play-mode verification: the real two-person clip ran at 436x346 and 5 fps with `BodyCount=2`, runtime `MaxBodies=4`, and `ResultSequence=317`; manager/frame-source errors, GPU readback drops, and GPU readback errors were all zero. Unity Console reported 0 warnings and 0 errors.
- Visual evidence: `E:\UnityProject\Human-Vision-SDK-Test\Assets\Screenshots\humanvision_d04_kinect_style.png`, 1920x1080, SHA-256 `4B52E431F9EF941D4583EA5BE109F135B8A9EF751457A307EED2A4F08EE9B634`.
- Source-to-imported-project comparison: PASS; the modified geometry, overlay, and EditMode test files match by SHA-256.
- Windows x64 Demo rebuild: PASS, 162 files / 431,126,373 bytes. The rebuilt `HumanVision.Demo.dll` is 24,064 bytes with SHA-256 `F411DD05E223F98C5AA7301609F9D5A422DCD9FD53DEF13423FD2EB0B75C1709`.
- Standalone smoke run: the rebuilt player remained responsive for 12 seconds and loaded `humanvision.dll` plus the private `humanvision_onnxruntime.dll`. Its log contained no exception or crash and only the two already documented VideoPlayer timestamp/color-standard warnings.

Unity GPU readback orientation and skeleton readability correction:

- Date: 2026-09-03
- Implementation commit: `b80ac63` (`fix: normalize Unity video readback orientation`).
- User-visible symptoms: the display skeleton's five-point face chain appeared near the person's feet, while leg-related chains appeared near the upper body; the 2 px bones and 4 px-radius joints were also too thin to read clearly.
- Root cause: on the active Windows DX11 path, `AsyncGPUReadback` returned the VideoPlayer RenderTexture rows in the opposite vertical order from the top-left image layout required by the native detector and pose pipeline. The RawImage itself used the normal `(0,0,1,1)` UV rectangle, so changing overlay coordinates or swapping left/right COCO joints would only hide the input error.
- Expected RED jobs: `be8dcd24` failed 0/1 before a reusable bottom-up-to-top-down row normalizer existed; `e5634859` failed 0/1 while the Demo scene still serialized 2 px bones and 4 px-radius joints.
- Fix: each active GPU readback slot now owns a preallocated top-left-order buffer. On platforms whose graphics UV origin starts at the top, the callback copies rows in reverse order with `UnsafeUtility.MemCpy` before `HV_SubmitFrame`. The display texture remains unchanged, and no per-frame managed or native buffer allocation was added. Demo bone thickness is now 4 px and joint radius is 7 px; the 3 px body box is unchanged.
- Focused GREEN jobs: row normalization `0bf77b3e` 1/1; visual scene contract `1b86df41` 1/1; post-refactor row normalization `9f4635d1` 1/1.
- Full EditMode job `9b820641`: PASS, 33/33 tests, 0 failures, 2 seconds; Unity compilation and Console reported 0 errors.
- Final post-dynamic-video EditMode job `5622cd29`: PASS, 33/33 tests, 0 failures; Unity Console reported 0 errors.
- Two-person runtime: 436x346 at 5 fps, `BodyCount=2`, `MaxBodies=4`, `ResultSequence=19`, manager/frame-source errors empty, and GPU readback drops/errors 0/0. Evidence: `E:\UnityProject\Human-Vision-SDK-Test\Assets\Screenshots\humanvision_d04_orientation_fixed_two_people.png`, 1920x1080, SHA-256 `445F937569B36047DA4A0DF70F75ABB82D5DC2EED0FB76F01C0912BC9A15A575`.
- One-person reproduction regression: 218x346 at 5 fps, `BodyCount=1`, `MaxBodies=4`, `ResultSequence=21`, manager/frame-source errors empty, and GPU readback drops/errors 0/0. The face chain is now on the eyes/nose and both leg chains follow the legs. Evidence: `E:\UnityProject\Human-Vision-SDK-Test\Assets\Screenshots\humanvision_d04_orientation_fixed_one_person.png`, 1920x1080, SHA-256 `55CD8751D1F973AEE46E72E617E35036F66F2AAFE52527B9FD189E2DB69F24EE`.
- User-provided dynamic-video regression: `Assets\4859224-uhd_3840_2160_25fps.mp4` (H.264/yuv420p, 3840x2160 at 25 fps, 2.88 seconds, 7,834,825 bytes) ran with `BodyCount=2`, `MaxBodies=4`, `ResultSequence=86`, `SourceFrameId=28`, manager/frame-source errors empty, and GPU readback drops/errors 0/0. Evidence: `E:\UnityProject\Human-Vision-SDK-Test\Assets\Screenshots\humanvision_d04_dynamic_4k_validation.png`, 1920x1080, SHA-256 `352DA3B3F279F3329B80A66375DBAA3DAC8EC3D96031B22351F79ADB95FA190D`.
- Dynamic-motion latency diagnosis: with the 4K/25 fps clip, a completed two-person frame took about 193-251 ms (`RTMDet` about 159-204 ms, two-person `RTMPose` about 34-47 ms). Runtime frame/result snapshots were typically separated by 6-10 source frames (about 240-400 ms). Submitted-frame replacement remained active, so the visible lag is detector-bound processing latency rather than an unbounded stale-frame queue.
- Hand-data boundary confirmed by the dynamic clip: the current result schema ends at the COCO-17 wrists. Palm, hand-tip, thumb, and finger points are not present in the model output, so adding them requires a real hand-keypoint model/schema extension rather than an overlay-only drawing change.
- Pixel-space regression check against the committed native one-person benchmark: correct top-left joint locations scored 2745 cyan pixels within the joint neighborhoods; the vertically flipped alternative scored 129.
- Windows x64 Demo rebuild: PASS, Unity BuildPipeline reported 431,124,771 bytes in 8.54 seconds; output inventory is 163 files / 431,128,710 bytes. Rebuilt `HumanVision.Demo.dll` is 25,088 bytes with SHA-256 `273FEC4127E9570156875E1530A230174536777A977E24DFBF4878555BAF3C9C`.
- Standalone smoke run: the player remained responsive for 12 seconds, loaded `humanvision.dll` and private `humanvision_onnxruntime.dll`, and did not load public `onnxruntime.dll`. Its log contained no exception or crash and only the two known VideoPlayer timestamp/color-standard warnings.
- Requested follow-up: after this correction is accepted, add computer USB-camera recognition through a Unity `WebCamTexture` frame source that reuses the asynchronous `HV_SubmitFrame` pipeline. This follow-up is not implemented by the orientation correction and must be scheduled explicitly against the current D1.0 RTSP milestone.

RenderTexture real-time presentation synchronization follow-up:

- Date: 2026-09-05
- User-visible symptom: the VideoPlayer RenderTexture displayed the newest decoded frame while the asynchronous CPU result described an older source frame. Fast runners could therefore leave their boxes and skeletons behind even though the pose coordinates were correct for the frame that was analyzed.
- Root cause: the 4K source was read back and submitted at its full 3840x2160 size, model results were composited over the live texture without a source-frame presentation policy, and VideoPlayer loop frame indices were reused after wraparound. The native latest-frame queue bounded backlog but could not make an old pose geometrically match a newer displayed image.
- Fix: preserve source-frame-driven submission (`VideoPlayer.frameReady`, not Unity render-frame-driven inference), render 4K input into a 1280x720 analysis RenderTexture, and keep a preallocated GPU presentation ring. Presentation begins with a six-frame delay, advances at source-frame cadence, and re-anchors to the result source frame when video/pose skew exceeds four frames. Results more than ten presented frames old are suppressed immediately. Submission IDs are now monotonic across VideoPlayer loops, and loop-boundary readbacks/results are invalidated before they can be presented.
- Web-camera architecture decision: a future `WebCamTexture` source must submit only when `didUpdateThisFrame` is true and reuse the same timestamped latest-frame-wins path. Unity `Update()` may render/interpolate the overlay every frame, but it must not run duplicate inference for the same camera image.
- Expected RED evidence: Unity compilation first failed with 8 missing geometry/presentation-policy references, then 3 missing delayed-frame selector references, then 5 missing adaptive synchronization selector references.
- Focused GREEN jobs: `e0763876` PASS 9/9; `3f5c23a9` PASS 10/10; `8c0008d1` PASS 11/11. Full EditMode jobs `dac971a2` and final `7450b839`: PASS 37/37 each, 0 failures (final run 10 seconds); Unity compilation and Console reported 0 errors.
- Runtime validation input: `Assets\StreamingAssets\HumanVision\Media\4859224-uhd_3840_2160_25fps.mp4`, temporarily configured with `MaxBodies=8`. The source rendered for analysis at 1280x720 and 25 fps; sampled inference was 4.6-5.0 fps, Editor rendering remained above 43 fps, and GPU readback drops/errors remained 0/0.
- Runtime visual evidence: `humanvision_adaptive_t09.png` and `humanvision_adaptive_t14.png` contain no person and no stale skeleton; `humanvision_adaptive_t19.png` contains two people with both Kinect-style skeletons on their bodies. The latter reported a four-frame video delay and an eight-frame result age. SHA-256 values are `B9C0029B22022A519F2CE8C6A74683905D08F36A832BDA83EA09E5AF94807BA7`, `11DF7FC9C6615B21020B55D0BF6237ECABAA3E78CDE5D2F62523CE0F4E8C85F8`, and `D0D54B452E92D43999621F4098BCAD08D5F23031E7CA8E87D0242C787FB7B4F8` respectively.
- The imported scene was restored after validation; its SHA-256 is again `3091403CCBE2AB06B3472A8B5840B64CBCF13CACD9C89073446B32853B58CFBF`. The four modified source/test files match the imported Unity project by SHA-256.
- Windows x64 Demo rebuild: PASS, BuildPipeline reported 438,964,228 bytes in 8.69 seconds; output inventory is 164 files / 438,968,167 bytes. Rebuilt `HumanVision.Demo.dll` is 29,696 bytes with SHA-256 `F921B36BA5B6564CF61C6C2C943169579D2A658BE5A7C67249368C70B311A542`.
- Acceptance boundary: this synchronization change does not make CPU RTMDet + tracker + RTMPose inference run at 30 fps. The 4K clip still measured only 4.6-5.0 inference fps, and it contains two people, so the separate 30 fps / eight-person acceptance target remains unverified.

Windows standalone verification:

- Build command: Unity menu `HumanVision/Build Windows x64 Demo`; the builder includes only the D0.4 Demo scene.
- Artifact: `E:\UnityProject\Human-Vision-SDK-Test\Builds\HumanVisionD04\HumanVisionD04.exe`, SHA-256 `6755298a7f8b5e2ee9ea2b3b09b7166825c5f1933b20713edf5c192dfe17baac`.
- The launched player remained responsive after 12 seconds and loaded `humanvision.dll` plus `humanvision_onnxruntime.dll` 1.29.0. It did not load the AzureKinectExamples public `onnxruntime.dll` or provider DLL for this scene.
- Final Release `humanvision.dll`: 114,688 bytes, SHA-256 `3c81a5b2774ea5cb2944c4dda65b3cb640423e9372465af1c54abcd3d9f178d1`.
- Private `humanvision_onnxruntime.dll`: SHA-256 `69d8e6d3879a3b4001cdc74c8ed9ccc7e7f799a5b847059738323404519ec471`.
- `dumpbin /dependents` confirms `humanvision.dll` depends on `humanvision_onnxruntime.dll`, not `onnxruntime.dll`.
- Standalone model hashes match the locked detector and pose ONNX files; both committed one/two-person MP4 hashes match their D0.3 fixtures.
- Player log contained no exception or crash. It reported two known Windows VideoPlayer warnings for the small H.264 regression clip: skewed timestamp correction and unknown color standard fallback.

Behavior and architecture checks:

- Unity submits frames asynchronously and polls immutable complete snapshots; it never waits for detector and pose work on the main thread.
- Latest-frame-wins behavior and dropped-frame statistics remain owned by the native pipeline.
- Managed body/result buffers, native frame staging, render texture, and GPU readback slots are reused after warm-up; no per-frame JSON is used.
- Unity objects and overlay graphics are updated only on the Unity main thread.
- `MaxBodies` remains runtime configurable and defaults to 4; only the COCO-17 joint schema has fixed per-body capacity.
- The public managed API contains no RTMDet, RTMPose, ONNX Runtime, FFmpeg, or Azure Kinect model types.

Known issues / environment notes:

- The imported AzureKinectExamples package still carries its own public ONNX Runtime 1.10-era binaries. They are intentionally preserved for its features; HumanVision's private import library and renamed runtime prevent same-name loader collisions.
- The D0.4 scene uses Screen Space Overlay UI and a VideoPlayer render texture, so no MainCamera or scene Light is required.
- Model ONNX files and native runtime binaries remain Git-ignored and are copied into the working Unity project/build as external artifacts.
- The two player warnings are media-container compatibility notices from Unity's Windows VideoPlayer. They do not prevent playback or HumanVision inference, but production/field media should use normalized timestamps and explicit color metadata.
- CPU-only 30 fps inference with eight simultaneous people is not yet demonstrated. The RenderTexture presentation ring bounds visual pose/video skew and preserves a responsive render loop, but it does not remove the RTMDet/RTMPose compute bottleneck.

Next milestone: D1.0 RTSP IPC Input.

### D0.3 RTMPose + Tracker + Native Video Benchmark - PASS

- Date: 2026-09-02
- Implementation commit: `68b01ae24d305755f6f1eada4772af2aca179555`
- Host: Windows x64, ONNX Runtime 1.29.0 CPU execution provider
- Generator/compiler: Ninja Multi-Config, MSVC 19.44.35228.0 (v143)
- Regression media reader: FFmpeg 8.1.1, invoked only by the benchmark executable and not linked into HumanVisionCore

Expected RED verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
```

- Configure result: exit 0.
- Build result before implementation: exit 1; the acceptance target first reported missing production headers `models/rtmpose/simcc_decoder.h`, `models/rtmpose/pose_affine.h`, and `models/rtmpose/rtmpose_model.h`. The same test target also declared the absent tracker and stats contracts.
- This confirmed that official ROI affine/SimCC behavior, real pose inference, stable IDs, and stage statistics could not pass through detector-only D0.2 code.

Fresh Debug verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
ctest --preset windows-debug
```

- Configure: PASS, exit 0.
- Build: PASS, 35/35 build steps, exit 0.
- CTest: PASS, 28/28 tests, 0 failures, 10.53 seconds.

Fresh Release verification:

```powershell
cmake --preset windows-release --fresh
cmake --build --preset windows-release --clean-first
ctest --preset windows-release
```

- Configure: PASS, exit 0.
- Build: PASS, 35/35 build steps, exit 0.
- CTest: PASS, 28/28 tests, 0 failures, 6.86 seconds.
- Final post-instrumentation regressions: Debug 28/28 in 10.19 seconds; Release 28/28 in 6.65 seconds.

Pose/tracker/integration results:

- MMPose-compatible bbox center/scale, 1.25 padding, 192x256 aspect correction, affine mapping, RGB normalization, and source-coordinate restoration pass committed golden tests.
- Synthetic SimCC peak tensors decode 17 joints with the documented `argmax / 2.0` coordinate rule and minimum-axis confidence rule; malformed output tensor contracts are rejected.
- Native RTMPose-s vs official PyTorch golden: 17/17 joints valid, maximum coordinate error `0.000030517578125 px`, maximum score error `0.0012398958206176758`, and ONNX inference time `10.435199737548828 ms` on the isolated official ROI.
- Native RTMDet regression remains within tolerance: maximum box error `0.075927734375 px`, IoU `0.99961433162530111`, native score `0.91604882478713989`.
- Tracker tests pass continuous motion, two missing detections, two-person crossing with reversed detection order, velocity prediction, and unique monotonic IDs.
- C ABI integration returns real boxes, positive unique track IDs, 17 real joints, and stage timings. `detection_interval=2` reports a zero detector stage on the intermediate frame while preserving the ID and rerunning pose on the predicted ROI.
- Runtime `MaxBodies=1/2/4` returns 1/2/2 bodies for the real two-person input, so pose work remains capped at the detector selection stage.
- Official Python reference unit tests pass 4/4; the PyTorch-to-ONNX detector and pose comparison summary remains PASS.
- Stress: all latest-frame and tracker tests passed 20 consecutive repetitions each; the asynchronous one-person, real two-person, and detector-interval C API tests passed 5 consecutive repetitions each.

Native video benchmark commands:

```powershell
build/windows-release/bin/Release/hv_video_benchmark.exe `
  --input tests/testdata/d0_3_one_person.mp4 --width 218 --height 346 `
  --fps 5 --frames 10 --max-bodies 1 `
  --detector-model models/detector/rtmdet_tiny_640.onnx `
  --pose-model models/pose/rtmpose_s_256x192.onnx `
  --output-prefix out/benchmark/d0_3_one_person_max1

build/windows-release/bin/Release/hv_video_benchmark.exe `
  --input tests/testdata/d0_3_two_people.mp4 --width 436 --height 346 `
  --fps 5 --frames 10 --max-bodies 1 `
  --detector-model models/detector/rtmdet_tiny_640.onnx `
  --pose-model models/pose/rtmpose_s_256x192.onnx `
  --output-prefix out/benchmark/d0_3_two_people_max1
```

- The two-person command was repeated with `--max-bodies 2` and `--max-bodies 4` and matching output prefixes.
- One person / MaxBodies 1: 10/10 frames, body count 1~1, 1 unique ID, 17 minimum valid joints, averages `158.061722 ms` detection, `16.725178 ms` pose, `0.003440 ms` tracking, `174.790833 ms` total.
- Two people / MaxBodies 1: 10/10 frames, body count 1~1, 1 unique ID, 17 minimum valid joints, averages `168.703171 ms` detection, `18.091358 ms` pose, `0.003850 ms` tracking, `186.798843 ms` total.
- Two people / MaxBodies 2: 10/10 frames, body count 2~2, 2 unique IDs, 17 minimum valid joints, averages `167.752930 ms` detection, `34.047710 ms` pose, `0.003460 ms` tracking, `201.804367 ms` total.
- Two people / MaxBodies 4: 10/10 frames, body count 2~2, 2 unique IDs, 17 minimum valid joints, averages `167.797073 ms` detection, `33.211170 ms` pose, `0.004400 ms` tracking, `201.013138 ms` total.
- Each run exported per-frame CSV and a JSON summary with first-frame real boxes, IDs, and all 17 joint coordinates under `out/benchmark/`.

Regression media/contracts:

- `d0_3_one_person.mp4`: 10 frames, 218x346 at 5 fps, SHA-256 `20806434a5620aca9e6198782d6882beb6fc53f1e5a0725e48abf128b46f2f94`.
- `d0_3_two_people.mp4`: 10 frames, 436x346 at 5 fps, SHA-256 `62cff448ff24d793b4e06d6776438eb64f5d24cb25ff3c928dcd8cb78f3f513b`.
- Both clips are H.264 encodes of the real locked official-image raw fixtures. Two forced regenerations produced identical hashes; `d0_3_video_manifest.json` records source hashes, settings, and FFmpeg version.
- D0.3 pose fixture regeneration is deterministic; D0.2 fixture hashes remain unchanged.

Artifact checks:

- Release `humanvision.dll`: 114,688 bytes, SHA-256 `18a05d17e6c55b1d3f7f1608fe652d801b54133c359f862f8ce51166553bdcdf`.
- Debug `humanvision.dll`: 916,992 bytes, SHA-256 `4223122db4e6095377409b33fa0c8caa2555103e816458d85eb852d7fb0553f9`.
- Release `hv_video_benchmark.exe`: 84,480 bytes, SHA-256 `c8196b5eae75f56fe084b2c3c944b594c18ed9098e65c7b7cf6a635f55693411`.
- `dumpbin /headers`: Release DLL is x64 PE32+.
- `dumpbin /exports`: public boundary remains exactly the same 10 C ABI exports recorded for D0.2; no model/backend/tracker types are exported.
- `dumpbin /dependents`: HumanVisionCore depends on `onnxruntime.dll` plus the MSVC/UCRT runtime and has no FFmpeg/OpenCV dependency.
- Copied `onnxruntime.dll` SHA-256 remains `69d8e6d3879a3b4001cdc74c8ed9ccc7e7f799a5b847059738323404519ec471`.
- Model ONNX files and ONNX Runtime binaries remain Git-ignored and were not committed.

Known issues / environment notes:

- The sequential CPU benchmark measures deterministic pipeline latency rather than real-time playback throughput. The two-person baseline is approximately `201.8 ms/frame`; D0.4 subsequently verified latest-frame dropping and Unity responsiveness under real playback.
- The committed MP4s are reproducible real-image-derived regression clips, not field-motion footage. Tracker crossing/occlusion behavior is covered deterministically at the native box level; real participant motion remains part of D0.4/D1.1 visual validation.
- The installed Visual Studio host remains Visual Studio 2026 with its v143 compiler rather than the documented Visual Studio 2022 host baseline.
- D0.4 was accepted on the active Unity 2021.3.45f1 project by explicit user decision; no editor upgrade was required.

Next milestone: D0.4 Unity Local Video Demo.

### D0.2 Native ONNX Runtime + RTMDet - PASS

- Date: 2026-09-02
- Implementation commit: `3d2375c5548bec1a54ee2360efc33cd1a9965427`
- Host: Windows x64, ONNX Runtime CPU execution provider
- Generator/compiler: Ninja Multi-Config, MSVC 19.44.35228.0 (v143)
- Native ONNX Runtime: 1.29.0 Windows x64
- ONNX Runtime release archive SHA-256: `c9b4b7086b529ad814f428c1bad028e20a25d7dc0699836775faace4ab5b78b2`
- ONNX Runtime package commit: `2e2543fbe9fae542f921d47a72d21d5a4ef0b710`
- ONNX Runtime DLL SHA-256: `69d8e6d3879a3b4001cdc74c8ed9ccc7e7f799a5b847059738323404519ec471`

Expected RED verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
```

- Configure result: exit 0.
- Build result before implementation: exit 1 because the acceptance tests required the absent production headers `core/latest_frame_slot.h`, `core/result_snapshot_store.h`, `backend/onnx/onnx_runtime_backend.h`, and RTMDet modules.
- This confirmed that asynchronous latest-frame behavior, immutable complete snapshots, generic ONNX execution, and RTMDet preprocessing/postprocessing could not pass through test-only stubs.

Dependency setup and developer environment:

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup\download_onnxruntime.ps1
```

```bat
call "D:\Microsoft Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
```

- The setup script verifies the existing package commit and runtime DLL hash, verifies the archive hash on download, and leaves ONNX Runtime binaries Git-ignored.

Fresh Debug verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
ctest --preset windows-debug
```

- Configure: PASS, exit 0.
- Build: PASS, 22/22 build steps, exit 0.
- CTest: PASS, 15/15 tests, 0 failures, 6.20 seconds.

Fresh Release verification:

```powershell
cmake --preset windows-release --fresh
cmake --build --preset windows-release --clean-first
ctest --preset windows-release
```

- Configure: PASS, exit 0.
- Build: PASS, 22/22 build steps, exit 0.
- CTest: PASS, 15/15 tests, 0 failures, 3.95 seconds.
- A final incremental Release regression after documentation/setup refinements also passed 15/15 tests in 3.60 seconds.

Acceptance and golden results:

- C ABI configuration tests cover invalid config/model errors, create/destroy cycles, unsupported pixel formats, caller-buffer ownership, and runtime `MaxBodies` values 1/2/4/6/8.
- Latest-frame tests use a deterministic slow worker and confirm that the newest frame wins while overwritten frames increment dropped-frame statistics.
- Snapshot capacity tests confirm that insufficient destination capacity reports the required count without a partial copy.
- The generic ONNX backend executes a mathematical add-one graph; detector acceptance never uses fixed boxes or fake joints.
- RTMDet preprocessing matches the committed official golden tensor samples for RGBA32, BGRA32, RGB24, and BGR24 input.
- Official-image native/Python comparison: one real detection, maximum box error `0.075927734375 px`, IoU `0.99961433162530111`, native score `0.91604882478713989`, and ONNX inference time `112.90849304199219 ms`.
- Real two-person composite reference produces two detections. The native result returns one body at `MaxBodies=1` and two at `MaxBodies=2`, proving that body capacity is selected at runtime rather than hard-coded.
- Concurrency stress: latest-frame tests passed 20 consecutive repetitions; asynchronous real-detector C API test passed 5 consecutive repetitions.
- D0.1 Python reference regression: PASS, 4/4 tests, 0 failures.

Artifact checks:

- Release: `build/windows-release/bin/Release/humanvision.dll`, 81,920 bytes, SHA-256 `5bb16b9c9c71d646ea753bbbe071c5617f0519a543c485116a363e4966ffb81c3`.
- Debug: `build/windows-debug/bin/Debug/humanvision.dll`, 366,592 bytes.
- `dumpbin /headers`: x64 PE32+ DLL.
- `dumpbin /exports`: exactly 10 public C ABI exports: `HV_Create`, `HV_Destroy`, `HV_GetBodies`, `HV_GetBodyCount`, `HV_GetLastError`, `HV_GetLatestResultMeta`, `HV_GetStats`, `HV_GetVersionString`, `HV_Reconfigure`, and `HV_SubmitFrame`.
- The Release output contains the required `onnxruntime.dll`; its copied hash matches the verified source DLL hash above. `onnxruntime_providers_shared.dll` is also copied.
- Model ONNX files and ONNX Runtime binaries are not staged or committed.
- Integration/golden test: the official-image and real two-person RTMDet comparisons above are the D0.2 golden tests.
- Milestone video benchmark: not applicable to D0.2; D0.3 owns the native video throughput/latency benchmark.

Known issues / environment notes:

- D0.2 intentionally publishes detector-only bodies with `track_id=-1` and invalid/zero joints. RTMPose, stable tracking, detector interval behavior, and video benchmarking belong to D0.3.
- The installed Visual Studio host remains Visual Studio 2026 with the v143 compiler, rather than the documented Visual Studio 2022 host baseline. Both Debug and Release native outputs are verified.
- D0.4 was accepted on Unity 2021.3.45f1 by explicit user decision; the historical Unity 2022.3 LTS baseline was not applied to this working project.

Next milestone: D0.3 RTMPose + Tracker + Native Video Benchmark.

### D0.1 Python/OpenMMLab Reference + ONNX Contract - PASS

- Date: 2026-09-01
- Implementation commit: `3990968cf0420b0bc0f6a53684642cdc34738144`
- Host: Windows x64, CPU reference inference
- Python: 3.10.21 (uv-managed CPython)
- PyTorch / TorchVision: 2.1.0+cpu / 0.16.0+cpu
- OpenMMLab: MMCV 2.1.0, MMDetection 3.2.0, MMPose 1.3.2, MMDeploy 1.3.1
- ONNX / ONNX Runtime Python: 1.15.0 / 1.23.2
- Reference media: official MMDeploy `demo/resources/human-pose.jpg`, SHA-256 `dd25fd8186e9ce27625520e24ac13ec6747316e51d20b124d3271c5764686d4e`

Locked official sources:

- MMDetection `v3.2.0`, commit `fe3f809a0a514189baf889aa358c498d51ee36cd`.
- MMPose `v1.3.2`, commit `5408bc76f5b848cf925a0d1857899011d8c5b497`.
- MMDeploy `v1.3.1`, commit `bc75c9d6c8940aa03d0e1e5b5962bd930478ba77`.
- RTMDet-tiny checkpoint SHA-256: `78e30dcce0c6f594eaff0d6977b84b4103688b4aff0ad1aa16008a8cc854a7fb`.
- RTMPose-s checkpoint SHA-256: `29dcacbb5c5f3ab2f03a67fedcb58cf7287f93b9a8a9d893f42416b63fc304ba`.

Expected RED verification:

```powershell
py -3.13 -m unittest discover -s tests\reference -v
```

- Result before implementing `tools.reference.contracts`: exit 1, 4/4 tests failed because the required D0.1 contract module was absent.
- This confirmed that model metadata completeness and model-file SHA validation could not pass without production contract code.

Final reference/export/comparison verification:

```powershell
.venv-reference\Scripts\python.exe -m unittest discover -s tests\reference -v
.venv-reference\Scripts\python.exe -m tools.reference.run_reference --device cpu
.venv-reference\Scripts\python.exe -m tools.reference.export_models --model all --device cpu
.venv-reference\Scripts\python.exe -m tools.reference.compare_onnx
```

- Contract unit tests: PASS, 4/4 tests, 0 failures.
- Official PyTorch reference: PASS, 1 person detection and 17 COCO joints.
- MMDeploy export: PASS for RTMDet-tiny and RTMPose-s; both graphs pass `onnx.checker` and load in ONNX Runtime CPU.
- Detector comparison: PASS; count 1/1, maximum source-coordinate box error `0.000033021 px`, IoU `0.999999682`, score error `1.19209e-7` (limits: `1.5 px`, `0.99`, `0.01`).
- Pose comparison: PASS; 17 joints, maximum restored coordinate error `0 px`, maximum score error `1.31130e-6`, maximum raw SimCC error `5.82635e-6` (limits: `0.5 px`, `0.005`, `0.002`).
- D0.0 native regression: PASS in Debug and Release, 1/1 CTest each, 0 failures.

Artifacts/contracts:

- Detector ONNX: `models/detector/rtmdet_tiny_640.onnx`, 22,289,445 bytes, SHA-256 `6d0d4e5da97772cbc8d25368f7796b1250fdf12c61861b228d31b321fc519e3d`.
- Pose ONNX: `models/pose/rtmpose_s_256x192.onnx`, 21,916,761 bytes, SHA-256 `9060b6cf176a49ba687feb993830b18293cc06b6675c5231c1e388d4e7a1c3ef`.
- ONNX/checkpoint binaries remain Git-ignored. Reproduction commands, exact environment snapshot, official source/checkpoint locations, model contracts, golden outputs, and numerical comparison results are committed.
- Integration/golden test: the official image PyTorch-to-ONNX comparisons above are the D0.1 golden tests.
- Benchmark media/settings/FPS/latency: not applicable to D0.1.

Known issues / environment notes:

- ONNX Runtime 1.29.0 does not publish a Windows `cp310` wheel, so the Python reference environment uses the last available official Windows `cp310` wheel, 1.23.2. The native D0.2 dependency remains the plan-locked ONNX Runtime 1.29.0 CPU x64 package.
- The MMDeploy `tools/deploy.py` CLI produced a valid detector graph, then failed only in its post-export MMDetection visualization because the exported label tensor is float. `export_models.py` uses the official `mmdeploy.apis.torch2onnx` API to omit that unrelated visualization step; the resulting graph passes structure and numerical verification.
- Pinned OpenMMLab emits registry/deprecation/tracer warnings during export on this Windows stack. The locked source/checkpoint checks, ONNX checker, model contract hashes, and numerical comparisons all pass.

Next milestone: D0.2 Native ONNX Runtime + RTMDet.

### D0.0 Repository & Build Bootstrap - PASS

- Date: 2026-09-01
- Implementation commit: `5114449e8d331485fdae4daaacc942cd44ccc093`
- Host: Windows x64
- Generator: Ninja Multi-Config
- Compiler: MSVC 19.44.35228.0 from VCTools 14.44.35207 (v143)
- CMake: 4.3.1-msvc1 (project minimum remains 3.24)
- GoogleTest: pinned commit `b514bdc898e2951020cbdca1304b75f5950d1f59` (`v1.15.2`)

Developer environment command used before configure/build:

```bat
call "D:\Microsoft Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
```

Expected RED verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug
```

- Configure result: exit 0.
- Build result before implementing `HV_GetVersionString`: exit 1 while linking the smoke test because the DLL had no exported implementation/import library (`LNK1104` for `native\Debug\humanvision.lib`).
- This confirmed that the smoke test could not pass without the production version function.

Fresh Debug verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
ctest --preset windows-debug
```

- Configure: PASS, exit 0.
- Build: PASS, 8/8 build steps, exit 0.
- CTest: PASS, 1/1 tests, 0 failures (`SdkVersion.IsNonEmpty`).

Fresh Release verification:

```powershell
cmake --preset windows-release --fresh
cmake --build --preset windows-release --clean-first
ctest --preset windows-release
```

- Configure: PASS, exit 0.
- Build: PASS, 8/8 build steps, exit 0.
- CTest: PASS, 1/1 tests, 0 failures (`SdkVersion.IsNonEmpty`).

Artifact checks:

- Debug: `build/windows-debug/bin/Debug/humanvision.dll` (52,224 bytes).
- Release: `build/windows-release/bin/Release/humanvision.dll` (9,728 bytes).
- `dumpbin /headers`: `8664 machine (x64)`, PE32+ DLL.
- `dumpbin /exports`: one D0.0 export, `HV_GetVersionString`.
- Model/runtime dependency: none.
- Integration/golden test: not applicable to D0.0.
- Benchmark media/settings/FPS/latency: not applicable to D0.0.

Known issues / environment notes:

- The installed Visual Studio host is Visual Studio 2026 18.9.1, while `TOOLCHAIN.md` names Visual Studio 2022 as the baseline. The verified build uses the installed v143 compiler through `VsDevCmd` plus Ninja because the VS 2026 MSBuild host does not include the v143 MSBuild platform targets.
- The UnitySkills project reports Unity 2021.3.45f1 and project `Human-Vision-SDK-Test`. D0.0 was native-only, and D0.4 later accepted this Unity version by explicit user decision.

## Advancement rule

Only mark a milestone complete when its acceptance criteria in `CODEX_DEMO_EXECUTION_PLAN.md` are met with real outputs. Then update `Current milestone` to the next item before implementing it.
