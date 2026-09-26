# Android Vulkan/ncnn — Revision 4 written design awaiting review (2026-09-26)

The user confirmed drafting the recovery revision after the Revision 3 failure.
The proposed design is
`docs/superpowers/specs/2026-09-26-android-gpu-input-camera-provenance-revision-4-design.md`.
It sequences deterministic same-frame GPU parity, Camera2 same-image sensor
provenance, then unchanged integrated TopDown physical acceptance. This is a
documentation-only proposal, not implementation approval or a device PASS.
Milestone C remains blocked on written-spec review and subsequent plan approval;
Milestone D, main merge and Release remain gated. Historical evidence follows.

# Android Vulkan/ncnn — Revision 3 Task 10 integrated device gate FAIL (2026-09-26)

The integrated Development/IL2CPP/ARM64/API-26/Vulkan Camera Demo was built
with interval 2/capacity 1 and a separately hashed local model profile. The
one-person, front-camera 60-second window produced 1,137 distinct complete
native observations (18.95/s), zero native Bodies and zero facade body slots while
the user confirmed their upper body visible. A corrected GPU letterbox and
rank-2 output decoder passed native regressions but did not restore live
recognition. A hash-bound supplemental video run in the same Demo with a
verified `VideoPlayer` GPU texture also produced zero candidates, while pinned
ONNX recognizes that video. Its sparse normalized GPU input has an unexpected
black content row; GPU copy/import/preprocess versus ncnn parity needs further
isolation. `WebCamTexture` still lacks the same-image sensor timestamp, so
verified sensor age is unavailable. The 30 fresh/s, body correctness and age
gates therefore fail independently. No 2-person or 15-minute thermal window
was run, no interval was selected, and RTMO/Hand/release work remains gated.
See `docs/validation/ANDROID_NCNN_TOPDOWN_GATE.md` and ignored raw evidence.
Post-review RED→GREEN fixtures closed analyzer false-pass paths for rolling
10-second exit boundaries, 1/2-person cardinality, runtime telemetry/fatal
lines, live overlay presentation and thermal sample validity. The analyzer
suite is 10/10 PASS; full native CTest remains 302/302 PASS, architecture checks
PASS, and the isolated Unity project imported the final evaluation-only C#
teardown edit without errors. The measured device APK was not rebuilt after
that teardown edit. Facade slots do not prove rendered skeleton geometry, so
renderer evidence is now unavailable and fail-closed. The controlled video
bypasses live-source recency; its native detector trace is diagnostic only.
Thermal evidence must bind body correctness and source to the same
APK/device/PID/log timeline, and none exists for this failed candidate. The
currently open `E:\UnityProject\Human-Vision-SDK-Test` Editor imported the
reviewed Task 10 Unity sources and four narrowly required older-runtime
dependencies after every overwritten file was backed up and hash checked. Its
CameraDemo and CameraSettings scenes were created, and Unity's Build Settings
API reads both enabled exactly once. Both scenes entered bounded Play with zero
compile/Console errors; the Demo overlayer retained the requested 24/72 visual
defaults. Actual Editor inference is blocked: `HumanVisionCameraManager.Status`
reports `Runtime data index missing: HTTP/1.1 404 Not Found` because
`Assets/StreamingAssets/HumanVision/Runtime/index.json` is absent from both the
repository Unity Demo and open project. `IsReady=false`, `ResultSequence=0`;
this Editor run does not demonstrate a visible skeleton or change device FAIL.
The open project remains on Android build target and its user assets/settings
and eight 24/72 prepared scene overrides were preserved. Exact hashes and
Play snapshots are under ignored `out/android-topdown-eval/open-project-validation/`.

# Android Vulkan/ncnn — Revision 3 Task 9 diagnostics complete (2026-09-26)

Task 9 adds a separate size/version-prefixed `HV_RuntimeStatsV2` query and
managed interop. V1 stats layout, `HV_RuntimeCopy` and the public skeleton API
are unchanged. The GPU worker now assigns Regions, validates current joint
timestamps and native capture clock, and counts each accepted complete
observation at native publication. A Unity poll may sample the same result many
times without increasing the fresh counter. The paired native source-observation
anchor and first publication clock drive fixed-capacity rolling age
quantiles; pose-only and scheduled-detector-frame age are separate, as are per-body
pose timings. Empty observations do not enter the per-body timing quantiles.
Fresh, capture and sampled-output rates use bounded 10-second windows and
decay to zero after a stall. Source arrivals/rate limiting, capture attempts,
bridge and pose drops, detector cadence/outcomes, copy/import errors, selected
copy path and actual backend have separate fields. Successful GPU copy counters
and timestamps reset across bridge generations. The camera scene and Demo
HUD use the same labelled V2 presentation.

**Capture-clock limitation:** the Android camera scene uses Unity
`WebCamTexture.didUpdateThisFrame`. `HumanVisionLiveSource` records the timestamp
in `Update`, after the camera produced the buffer; Unity's 2021.3 public
WebCamTexture API does not expose that image's original sensor timestamp.
Accordingly V2 marks `capture_provenance=UNITY_OBSERVED`, labels Age P50/P95
as a Unity-observed-to-publication **lower bound**, and leaves verified sensor
capture age unavailable (`NaN`). The Task 10 end-to-end age gate must reject
this provenance and cannot pass on the lower bound. An actual same-image
timestamp would require a native camera image/metadata route (for example
Android Camera2 `SENSOR_TIMESTAMP`) or another verified camera integration;
that is a Task 10 architecture blocker, not evidence of physical acceptance.
The current Task 9 contract does not permit callers to claim sensor-verified
provenance through its setter.

RED: `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter StatsV2`
failed compilation when the new `StatsCollectorV2` contract was absent.
Regression coverage now includes 1/4/8-body frame counting, sample/clone
rejection, invalid clocks and stale valid joints before public publication,
sparse Unity polling, deterministic P50/P95, separate pose/keyframe age,
per-person timing, empty-frame exclusion, stalled FPS decay, unknown/observed
unavailable sensor age, exact sensor-verified fixture quantiles, public
verified-provenance spoof rejection, bridge restart telemetry reset and nonfinite
pose timing rejection before publication. The new Unity tests cover the V1/V2 managed
layouts, required provenance/HUD labels and actual camera scene connection.

Verification after the final diagnostics changes:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter StatsV2`: **10/10 PASS**.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **299/299 PASS**.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Windows`: **PASS** (build only).
- `pwsh -NoProfile -File tools/test/run_unity040_tests.ps1`: **85/87 passed, 2 intentionally skipped, 0 failed**. The existing serialized Android settings test now uses a complete deterministic fixture because Unity's temporary EditMode project omits empty Android mappings; the test runner reports ignored tests without treating them as failures.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`: **PASS** (build only).
- `.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`: **PASS**, ELF64 AArch64/API 26, 1813 strong dynamic imports resolved; packaged `libhumanvision.so` SHA-256 `a5d0fbd95977b766bba1a2171a278ecd2c840df4484528979a39fb5f2a73e340`.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**.
- `git diff --check`: **PASS**.

Independent Task 9 SPEC and QUALITY re-reviews passed after the provenance,
bridge-restart and prepublication validation repairs. Task 9 is ready for its
separate verified commit. No physical-device FPS or camera acceptance is
claimed.

# Android Vulkan/ncnn — Revision 3 Task 8 Region assignment complete (2026-09-26)

Task 8 assigns current GPU pose observations to configured Regions in runtime
composition before common tracking and snapshot publication. Pelvis containment
uses an explicit valid pelvis or the midpoint of valid hip joints; box center is
used only without a valid pelvis. Bodies outside all Regions are discarded. One
body occupies each Region. Existing assigned crop identity wins a valid
collision, followed by pose confidence, detector confidence, and source order.
For one body eligible for overlapping Regions, box coverage breaks the tie,
then lowest Region index. The no-Region path preserves pipeline body order.
Independent review found that two prior Region anchors could claim the same
current candidate. Fixed-storage one-to-one matching now maximizes retained
assignments, then overlap. It also found that a changed crop ID could disappear
when `MaxPeople` was full; a complete GPU observation now reclaims an old
unsupported public slot for the current crop. The legacy CPU hold and
association path is unchanged.

The GPU pipeline passes detector score and crop track ID through a guarded V3
observation extent. V1 observation/body layouts and reserved fields remain
unchanged. The common tracker maps that crop identity to its own public
`track_id`, allowing a person to cross Region slots without swapping public
identity merely because the Region index changed. GPU composition does not
call `MaskRegions`; the legacy CPU worker retains its existing mask behavior.
Old-revision results are rejected before common tracking, and the session test
holds a fake GPU result across a Region edit to verify that no stale body or
source frame is published.
The GPU host and TopDown pipeline reject an incompatible observation
`api_version`; V1 copies advertise their exact copied prefix size.

RED: `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter RegionGpu`
failed compilation because `composition/region_assignment.h` was absent.
The initial GREEN run exposed a collision bug: two overlapping candidates
both inherited priority from one anchor. The final one-to-one matching fixes
this and the later two-anchor contention. A V1 copy regression verifies that
its advertised `struct_size` describes only the copied V1 prefix. The final no-Region order regression was
also included in the full verification below.

Review repair RED: two `RegionGpu` cases failed for competing anchors and a
capacity-full changed crop ID. After those fixes, the incompatible output
version case failed before its host/pipeline guard. All three are GREEN now.

Verification after the integration change:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter RegionGpu`: **12/12 PASS**.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **288/288 PASS**.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`: **PASS**; tests were not run by the build script.
- `.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`: **PASS**, ELF64 AArch64/API 26, 1813 strong dynamic imports resolved; packaged `libhumanvision.so` SHA-256 `491dc5115f03dd29e87215a3b7be87872f8b0e0d0ed261b7c0accf146a66f9ad`.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**.
- `git diff --check`: **PASS**.

Independent Task 8 SPEC and QUALITY re-reviews passed after the one-to-one
anchor, full-capacity reacquisition, and ABI-version repairs. Task 8 is ready
for its separate verified commit. No Android device performance or physical
camera result is claimed.

# Android Vulkan/ncnn — Revision 3 Task 7 TopDown GPU pipeline complete (2026-09-26)

Task 7 now selects a production V3 `pipeline.topdown` for the strict Android
ncnn route. Accepted source frames yield one complete current-frame observation:
0 bodies while reacquiring, or only bodies whose current image completed
Body26 pose. The detector takes prepared 320×320 keyframes on a bounded
2–6 accepted-frame/200 ms schedule, runs one detached job at a time, and may
supersede only an unstarted keyframe. A current pose rejection immediately
removes that body; detector output updates future crops only. Source generation,
dimensions and Region revision reset crop association; an old detector result
cannot seed a new revision. The V3 Region frame extension preserves the exact
56-byte V1 frame prefix and is read only after its `struct_size` guard. The
producer also carries a paired native monotonic capture clock in a 56-byte
extension of the frozen 48-byte Android submission. The Unity timestamp is
unchanged for public results; detector age now includes source queue time.
Task 8
still owns post-inference Region assignment.

RED: `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter TopDownGpu`
first failed compilation on the absent pipeline header. Further focused RED
cases caught the capture-clock epoch mismatch, repeated busy-deadline count,
stale profile hash, and Region revision result leak. The latter produced 1
failed focused case before the revision reset and guarded V3 frame extension.
Independent first SPEC/QUALITY reviews then found five blockers: public GPU
snapshots could inherit a previous valid hand joint; detector age omitted
capture-to-prepare queue time; failed `prepare_image` wedged cadence; a queued
detector token might never wake after a pose or role-completion error; and the
V3 host accepted a truncated or mismatched callback table. New regressions
cover each path. GPU snapshots now use current raw observations without
previous-hand carry-forward or render hold while legacy/ORT samples retain
their existing behavior. A prepare failure cancels its cadence reservation
and enters terminal NCNN Vulkan error. A successfully queued detector wakes
after current pose/role completion; an exit guard wakes it exactly once on an
early error. The host validates the V3 callback prefix. A second QUALITY
review caught the earlier wake violating pose priority and the V3 pipeline
`Create` accepting a short host-services prefix. Both new focused tests failed
before repair and passed after. The V3 factory advertises the full V3 host
extent while retaining the historical V1 prefix API version and leaving
ServicesV1/ServicesV2 unchanged. A host-to-pipeline clock/Region propagation
test also covers the frame extension.

GREEN after the final Region change:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter 'TopDownGpu.*'`: **19/19 PASS**, including 180 ms queued capture plus 40 ms detector rejection, pose and role-error token wakeup, terminal prepare failure, V1 ABI prefix, host-services prefix and detector admission after current pose. The queue-delay test initially crashed from test-fixture teardown of an active fake worker; waiting for the second detached fake job corrected only the fixture.
- Public `RuntimeSession::Copy` no-old-joint/no-held-body, host callback validation, and host-to-pipeline clock/Region extension tests passed in the final full native suite.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **276/276 PASS**. One prior 276-test run had 274 passes and two failures: an existing V3 ABI fixture expected its V1 API version, and the pose-priority fixture polled detector execution before its result was published. Keeping the V1 version with V3 extent and awaiting result publication fixed both; the final full rerun exited 0.
- `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_rtmpose_ncnn_golden.py -v`: **5/5 PASS**, including the four captured real Vulkan pose cases and hashed local schema-2 pack. `test_rtmdet_eval.py`: **5/5 PASS**. Local evaluation weights remain ignored and unpublished.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`: **PASS**. `.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`: **PASS**, ELF64 AArch64/API 26, 1813 strong dynamic imports resolved; packaged `libhumanvision.so` SHA-256 `3cab864a660f19a4530c41975ab8ac6d890dd350981dad03d5205ea713f7ff87`.
- Focused Unity 2021.3.45f1 `HumanVision.Tests.HumanVisionAndroidGpuRoutingTests`: **12/12 PASS** in `out/unity040-route-task7-final-repair.xml`, including managed V1/V2 submission layout. Earlier full Unity EditMode run was **80 passed, 1 failed, 2 skipped**; the failing generated ProjectSettings fixture is described in Task 5 below.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**; `git diff --check`: **PASS** with line-ending advisories only.

The pose-only fixture's warmed C++ hot path measured zero allocations. That
does not certify the full ncnn detector hot path: Task 4 identified bounded
`record_download` staging allocations on detector keyframes, and Task 7 has
not measured their device timing/allocation pressure. Captured model output
goldens pass, but an integrated Task 7 live source-to-pose device parity/FPS
run is not claimed; the Revision 3 plan assigns that APK measurement to Task
10. Independent Task 7 SPEC and QUALITY re-reviews both passed after the
final pose-priority and host-prefix repairs. Task 7 is ready for its separate
verified commit; Task 8 Region assignment remains next.

# Android Vulkan/ncnn — Revision 3 Task 6 GPU crop state complete (2026-09-26)

Task 6 adds a bounded, eight-slot GPU crop policy and detector identity
association for a later V3 TopDown pipeline. Current valid pose joints update
the next clipped ROI. A delayed detector result can refresh a detector-time
anchor without rewinding a newer pose crop or changing its published frame.
New identities require a detector result within 200 ms of capture; a result up
to 500 ms old may correct an existing identity. Two missed detector matches or
more than 500 ms since a matched detector expire that identity. A failed
current pose clears its publishable frame immediately. The GPU route never
uses the legacy tracker's three-second crop prediction to decide publication.

RED: `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter
GpuTrackCrops` failed compilation because the new crop header was absent. After
the initial implementation, the same focused suite showed two failing pose
cases; corrected one fixture's image-edge joints and made the failure fixture
select its current frame. A later boundary test failed because `ApplyPose`
accepted a track not selected for that frame; a selected-frame guard fixed it.
The separate aged-result test failed until known-track correction and new-track
discovery were given their distinct 500/200 ms limits. One more RED case
showed that a newer detector arriving during a selected pose frame incorrectly
cancelled that pose; the frame-selection timestamp now governs pose acceptance.
The discovery geometry/score gate also failed a RED test until low-score and
out-of-image boxes were rejected. Independent SPEC and QUALITY reviews then
found four blockers. New RED tests showed warmed detector matching still
allocated, delayed crossing could swap IDs, a future second detector miss
could cancel an already selected pose, and expiring one of two tracks could
erase the reacquisition request. The matcher now reuses reserved scratch,
caps the GPU identity path at eight tracks, scores delayed results only from
detector-time anchors, defers selected-frame expiry, and preserves the
reacquisition request. The final delayed-crossing fixture was also run against
the original cost rule and failed as intended before restoration of the fix.
A second SPEC/QUALITY review found the inverse ordering: a detector correction
captured after pose selection was overwritten when that older pose completed.
The RED regression checked both the next crop center and a following detector
association. The older pose now publishes its own frame while leaving the
newer detector crop/identity untouched. A warmed eight-track churn test also
confirmed no allocations at the maximum GPU capacity.

GREEN: focused `GpuTrackCrops` **18/18 PASS**; full
`pwsh -NoProfile -File tools/test/run_native_tests.ps1` **253/253 PASS**;
`.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`
**PASS**; `git diff --check` **PASS** (line-ending advisory only). This is a
policy component and does not claim Task 7 pipeline integration or device FPS.
Independent final SPEC and QUALITY reviews passed after the ordering repair;
the SPEC reviewer independently reran the focused **18/18** suite. Task 6 is
ready for its separate verified commit before Task 7 begins.

# Android Vulkan/ncnn — Revision 3 Task 5 GPU runtime composition complete (2026-09-26)

Task 5 adds a V3 GPU runtime worker that claims the existing AHB bridge's
generation-bound slots only during an owned source lease, invokes one V3 pipeline per current source frame, drains
or quarantines failed leases, and atomically publishes the full observation with
its original frame ID/time. The `android-ncnn-vulkan` profile now selects only
a schema-2 ModelPack and a strict, single V3 ncnn backend/pipeline route. ORT
profiles still use their V1 CPU route. The production V3 TopDown pipeline is
scheduled for Task 7; until it exists, NCNN runtime creation reports
`V3 GPU pipeline unavailable` and does not fall back to ORT. No integrated
skeleton/FPS or device acceptance is claimed here.

RED: `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter
RuntimeGpuComposition` first failed compilation for the missing
`host/gpu_runtime_host.h`. After adding only its declaration, the same command
reached the link step and reported four missing `GpuRuntimeHost` symbols
(`LNK2019`/`LNK1120`) in `out/native-tests.log`. That wrapper continued into
CTest and returned exit 0 despite the failed build, so the logged linker
failure, rather than the wrapper exit code, is the RED evidence.

GREEN after implementation and lifecycle tests:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter RuntimeGpuComposition`: **4/4 PASS**. The fixture exercises real `AhbSlotRing` reservation, ready publication, claim, producer-fence transfer, retirement, stop/restart, and stale-generation drain with a fake V3 pipeline.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter 'RuntimeGpuComposition|AndroidNcnnSelectsOnlyV3'`: **3/3 PASS** when first run; the strict profile test also passed in the final full suite.
- Independent quality review found global source-lease ownership and partial-create exception gaps. The added RED coordinator test linked with four missing methods (`LNK2019`/`LNK1120`); partial-create cleanup is covered by a new regression case. A first full run after the ownership fix was **232/233** because an existing adapter fixture began the producer directly but ended it through a runtime handle. The fixture now begins through that handle. A later SPEC review found a blocking ownership check after successful frame preparation; the RED signature change failed compilation until dimension recording was moved inside the coordinator's nonblocking Prepare path. The focused `RuntimeGpuComposition` suite is now **8/8 PASS**, including a concurrent End/Prepare test that confirms Prepare returns Busy promptly while source teardown holds the lease lock. `ForeignRuntimeCannotEndOrPrepareOwnedSourceLease` and the adapted teardown case also pass. Runtime destruction stops its GPU worker before ending only its own source lease; foreign End/Prepare are rejected. Stale-token tests quarantine instead of retiring through a substituted valid token.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **235/235 PASS**, including V1/ORT and V2/V3 ABI regressions.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Windows`: **PASS**.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`: **PASS** with `HV_ANDROID_GPU_GATE=OFF`.
- `.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`: **PASS**, ELF64 AArch64/API26, 1813 resolved strong imports; `libhumanvision.so` SHA-256 `ce02484d9f81c26bf3d6ab007dacffc8224d3d650247df9984de6637728378b7`. NDK `llvm-nm --defined-only` found zero `HV_AndroidGpuGate*` definitions in the final gate-OFF artifact.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`, `git diff --check`, and source search for `AsyncGPUReadback`, `AHardwareBuffer_lock`, or CPU `HV_RuntimeSubmit` in the Task 5 GPU host/Android entry point: **PASS**.
- Unity 2021.3.45f1 focused `HumanVision.Tests.HumanVisionAndroidGpuRoutingTests`: **12/12 PASS** (exit 0, `out/unity040-route-tests.xml`). Two full `run_unity040_tests.ps1` attempts completed **80 passed, 1 failed, 2 skipped**. The sole failure was existing `SerializedAndroidSettingsSnapshotDetectsPackageIdentityChange`: the test runner's generated `ProjectSettings.asset` had no nested Android `scriptingDefineSymbols` entry (and initially no `applicationId`), while the unchanged fixture requires both. The checked-in Unity project has no `ProjectSettings.asset`; the runner copies only `ProjectVersion.txt` and `EditorBuildSettings.asset`, then Unity generates defaults. All 12 GPU route tests passed inside the full run as well.

Independent full Task 5 SPEC and QUALITY reviews passed after both repair
rounds. The final nonblocking frame-submission fix was re-reviewed separately;
its concurrent End/Prepare case passed. Task 5 is ready for its separate
verified commit before Task 6 begins.

## Prior Task 4 device gate

Task 4's detached detector input gate passed in two independent cold starts on
OnePlus 9 Pro LE2120 (Snapdragon 888, Adreno 660, Android API 34; ADB serial
`e7c07019`). The installed development APK is
`E:/UnityProject/Human-Vision-SDK-Test/Builds/humanvision-prepared-gate.apk`,
SHA-256 `d744b2d6f596be56909123666757f87fc66336e3f4bf3b1d6da51a10361989b3`.
Its native `libhumanvision.so` is
SHA-256 `d9ab94631537e27605c46b9c139930d867490b8b678b1544706db681c28340b1`.
The first and repeat cold-run PID-specific logs are retained locally in ignored
`out/rev3-task4-evidence/device-extractor-reset-{logcat,cold-repeat-logcat}.txt`.
After a reviewed active-RenderTexture teardown cleanup, the current APK was
rebuilt, verified against all seven pinned ARM64 libraries, and installed
with `adb install -r`; its final repeat log is
`out/rev3-task4-evidence/device-active-texture-fix-logcat.txt` (PID 21141).
The final run again reported the same three-generation PASS sequence and
exact tensor hash, with no gate failure, fatal signal or active-RenderTexture
release warning.
Both runs reported generation 1 `PASS`, generation 2
`RESTART_DRAIN_PASS` while a prepared token was outstanding, and generation 3
`PASS` after restart. The exact 320×320×3 FP16 tensor hash was
`9e598ea5d37ebe348bd8235c412d404d62ceb78018dfdfb942a88a26ad118d19`
in each generation. The detector gate also checked source retirement before
prepared inference, direct/prepared output parity, and exactly two bounded
detector-output downloads per prepared job (`cls` 8,400 bytes and `bbox`
33,600 bytes). No source-frame CPU pixel readback was used.

The device reported AHB format 1 (Vulkan format 37), AHB usage `0x100`, Vulkan
format features `0xFFD83`, and selected the supported GPU blit path after
feature probing; producer and consumer AHB imports returned `VK_SUCCESS`.
Unity and ncnn reported the same physical-device and driver UUIDs on separate
logical devices. The observed `noSlot` counters (34/107 for the two complete
generations in the repeat run) are latest/drop behavior during this deliberately
slow diagnostic gate. This gate has a synthetic fixture and does not produce
camera skeletons or establish the required 30 fresh full observation frames/s.
Task 4's device gate is complete. Task 5 host composition and later integrated
performance acceptance remain outstanding.

Verification after the extractor lifecycle fix: `pwsh -NoProfile -File
tools/test/run_native_tests.ps1` passed **225/225**; Python reference tests
passed **56/56**; architecture tests passed **19/19**; focused Unity EditMode
gate tests passed **9/9** and `debug_get_errors` returned **0**. The normal
`pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android
-AndroidApiLevel 26` build passed; `tools/test/verify_android_native.py`
confirmed ELF64 ARM64, API 26, and 1,810 resolved native imports for
`build/android-live/bin/Release/libhumanvision.so` (SHA-256
`b7509141e314c92ad83df8e29c47380433516df5e6f2196cc45064e50bede8ac`).
The gate-only diagnostic string is absent from the normal binary;
`tools/maintenance/check_architecture_boundaries.py` and `git diff --check`
passed. The temporary verification command first pointed at the wrong
`build/android-live/runtime/` path; rerunning against the actual
`build/android-live/bin/Release/` artifact passed.
Independent full-diff SPEC and code-quality reviews passed. The latter
identified the active-RenderTexture teardown warning; the focused cleanup
was reviewed, rebuilt, and passed the final device rerun above.

## Task 4 development chronology (earlier failures below were resolved)

The detector-only V3 `prepare_image` now records an AHB import and GPU
preprocess/packing into one persistent 320×320 RGB FP16 pack1 ncnn `VkMat`.
The GPU command completes before the detector yields its AHB role; the current
observation can retain the AHB for pose and release the final role separately.
The generation-bound one-shot token then allows `run_prepared` to infer from
the detached tensor after AHB retirement. Busy/stale tokens and unproved GPU
completion fail closed; no ORT fallback, source-pixel CPU readback, or
per-frame AHB/import-pipeline creation was added. This is **not yet a Task 4
pass**: device-side source-to-prepared tensor parity and AHB lifecycle
evidence remain outstanding.

The fake-bridge test was run RED as four failing runtime assertions after an
initial compile RED, then GREEN (4/4) after implementation. The observable
test records GPU-copy proof, detector role handoff, final pose role/AHB
retirement, and a detached extractor-input read in order. V3 registration
tests passed 3/3; `pwsh -NoProfile -File tools/test/run_native_tests.ps1`
passed 220/220 before independent review. The review found that a direct
destructor could release Vulkan/AHB resources despite unproved GPU completion.
The new lifetime-policy tests were first 2/5 failing runtime assertions with
a temporary false implementation, then 5/5 passing; logs are preserved in
ignored `out/rev3-task4-evidence/destructor-{assertion-red,green}.log`.
The Android session now deliberately retains all dependent ncnn/Vulkan/AHB
allocations and its GPU-instance lease for process lifetime if completion is
uncertain. The destructor also rechecks after its final-role release, which
can itself fail; a source-level Android regression was RED then GREEN with
logs under the same ignored evidence directory. After combined repairs, full
native passed **224/224**, reference tests **55/55**, architecture tests
**19/19**, architecture guard and `git diff --check` passed. The gate-only
Unity source-lifetime regression was RED against HEAD and GREEN on the
terminal-aware C# script; ignored logs are under `out/rev3-task4-evidence/`.
Physical device validation is still pending.
Android ARM64/API26 `humanvision` with the development GPU
gate enabled built; `.venv-reference/Scripts/python.exe
tools/test/verify_android_native.py` passed ELF/API and 1813 dependency
imports (latest combined gate binary SHA-256
`16b6a489fae28b2c6c05fcd36b3c1060433be704d266d1cbe974293d79bfd098`).
The architecture guard and `git diff --check` passed.
The ordinary (non-gate) Android ARM64/API26 build also passed ELF/API audit
(1810 imports, latest SHA-256
`b7509141e314c92ad83df8e29c47380433516df5e6f2196cc45064e50bede8ac`);
the gate-only tensor download and gate entry point are absent from this
binary. Task 5 has not composed the V3 ncnn session into the runtime host;
the development gate is required to exercise Task 4 on device. The ordinary
binary includes the reviewed bridge terminal-state correction, so its bytes
no longer match the Task 3 baseline.

The isolated Prepared Gate uses a 320×320 four-quadrant RGBA fixture pinned to
SHA-256 `3e1edfd04bd3abd1b260cc67b07b781e365a85559365032185cd8ec72545ce74`.
It performs a **development-build-only** download of the small detached
320×320×3 tensor and requires its FP16 channel-major SHA-256 to match the
independently calculated
`9e598ea5d37ebe348bd8235c412d404d62ceb78018dfdfb942a88a26ad118d19`;
the production binary omits this diagnostic. The gate requires a real V3
prepare, source role yield, same-source second role, final AHB retirement,
then `run_prepared`; it snapshots and compares direct/prepared model outputs.
It logs fixture, manifest and detector-file hashes, actual AHB format/usage/
features, copy path, and generation. These are **unexecuted device checks**.
`tools/test/generate_prepared_gate_golden.py` independently reproduces the
reference from the tracked detector contract, validates local ModelPack
param/bin bytes when present, and writes the checked
`docs/validation/PREPARED_DETECTOR_INPUT_GOLDEN.json`; its focused Python
test passed. This reference is separate from the ncnn output comparison.

Independent review found two further fault/lifetime issues: bridge
quarantine already retains its owned AHB/Vulkan resources, but an Initialize
waiting on an active lease could reopen a new generation after quarantine;
also Unity could destroy its source texture after the native gate terminates
with unknown GPU completion. The bridge now keeps terminal state sticky,
rejects new generation admission, and exposes a gate-only source-retention
query. C# checks it before and after worker join and retains Unity textures
and the command buffer for process lifetime on terminal failure. The focused
bridge/ring/adapter suite passed **78/78** after RED tests. The independent
Task 4 **SPEC and QUALITY code-level reviews passed**; device proof remains
pending, so Task 4 is not complete. ncnn's small detector-output
`record_download` also creates bounded temporary allocations per keyframe.
Source restart while an active prepared job is pending has not yet been
demonstrated on-device.
The development gate now scripts three generations: full V3 parity; a second
generation with an admitted prepared token and retired source AHB, where Unity
requests restart and the worker drains the detector job before rebuilding;
then full V3 parity after rejecting the old generation's token. This validates
restart request overlapping an outstanding prepared job, **not** a Vulkan
graph hot-switch during execution. The gate-only `End` waits for the worker;
the production Unity main/render thread must never acquire this synchronous
behavior. None of this three-generation evidence exists until a fresh APK
runs on the device.
The readback scope is exactly two `record_download` calls per admitted
detector keyframe (`cls` at most 8,400 logical bytes, `bbox` at most 33,600),
never source pixels or a per-pose-frame transfer. ncnn internally allocates
temporary staging for each call; cached vector capacity does not remove those
allocations. A gate-only counter now asserts/logs two calls per prepared job.
This bounded ncnn output work is a Task 7 allocation-pressure/timing risk,
not a Task 4 input-cache hard blocker. The detector hot path is **not**
claimed allocation-free.

`pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -Prepared
-Unity 'D:/Developer/2021.3.45f1/Editor/Unity-verified.exe'` staged the native
closure but could not build an APK. Unity batch exited 1 before project load:
its Licensing Client reported signature validation error Code 10, an invalid
ULF digital signature and no cached entitlement (`No valid Unity Editor license
found`). The default Unity.exe was also independently found to have a
HashMismatch signature and cannot launch normally. No licensing/security
settings were changed, and no old APK is being treated as Task 4 evidence.
Once a valid Unity editor/license is available, rebuild this gate, inspect
the current device's AHB format/usage/features and sync-fd ownership, and
require all three scripted generations and tensor/model parity before final
device signoff and a separate Task 4 commit. Task 5 is not
started.

The user authorized the already-open Unity test project at
`E:/UnityProject/Human-Vision-SDK-Test` for interactive validation. Its
Unity 2021.3.45f1 Editor was available through UnitySkills, so the SDK
assets, pinned prepared-gate detector/model contract, and Android ARM64
native dependency closure were imported there. The focused EditMode class
`HumanVision.Tests.HumanVisionAndroidGpuGateBuildTests` passed **8/8** via
`test_run_by_name`; `debug_get_errors` returned **0** after the final build.
The interactive menu
`HumanVision/Android/Build PREPARED Detector Gate (Development APK)` built
`E:/UnityProject/Human-Vision-SDK-Test/Builds/humanvision-prepared-gate.apk`
(57,786,149 bytes, SHA-256
`cc5c58cf68ee7edf4ca60c14063f29775871fc1bc19a9f9edb6439cda3bc7031`).
`aapt dump badging` confirms its separate package
`com.blazetc.humanvision.preparedgate`, API 26, and ARM64. `pwsh -NoProfile
-File tools/test/verify_android_gpu_bridge_gate_libs.ps1 -Mode Verify
-ApkPath E:/UnityProject/Human-Vision-SDK-Test/Builds/humanvision-prepared-gate.apk`
verified all seven required ARM64 native libraries. The original
`SampleScene` remained active and clean. Interactive settings before/after
matched in Editor logs; the serialized ProjectSettings file also retained
the user's original product name, Android application ID, and API level
after the build. This required adding `File/Save Project` and a disk-field
assertion to the gate-only restoration path after a live first-build
regression exposed Unity's deferred ProjectSettings serialization.
Independent static review passed the repair, while its failure path has
not been exercised in Unity. The APK has **not** been installed or run on
the Snapdragon 888. This build is a fixed-input detector/AHB gate and
intentionally does not emit a skeleton. The physical parity, AHB feature,
sync-fd, and three-generation restart checks in Task 4 remain open; Task 4
is not complete and Task 5 must not begin.

The user then authorized direct ADB installation on OnePlus 9 Pro LE2120
(Snapdragon 888, Android API 34, serial `e7c07019`). The independent package
`com.blazetc.humanvision.preparedgate` installed and launched. The first
device run reached Vulkan/AHB import: the target reported AHB Vulkan format
37, image usage 151, format features 1047939, and a supported blit path;
Unity and ncnn physical-device UUIDs matched. The gate failed its first
full-tensor hash (`e5475a...` versus the initial `fd1e68...` reference).
The device's six samples isolated texture-edge mixing and one-ULP FP16
conversion differences. The gate fixture was changed to Point/Clamp
sampling; its focused Unity tests passed **9/9** and independent review
passed. The next device run produced exactly the independently calculated
full-tensor RTZ hash `9e598ea5...`, separating the texture problem from the
offline reference's nearest-even conversion. The reference generator now
models the observed Snapdragon/ncnn FP16 conversion without weakening the
exact whole-tensor comparison; reference tests passed **56/56**, native
**224/224**, architecture **19/19**, and independent review passed. This
rounding result is device evidence, not a claim about every Android GPU.

After rebuilding Android ARM64/API26 native (`libhumanvision.so` SHA-256
`fc286d8eb06d7ee7ed474c2f5309694d430b69db03117d9f3979e8ec200837de`,
ELF/dependency audit passed 1813 imports), staging all seven native libraries,
and building the current gate APK (SHA-256
`8facd05f7c5f0fc7d536c8c6629b7ce1d457847df6d4b1025dc00c920e577c87`),
the next Snapdragon run passed the strict tensor-hash stage and reached the
same-AHB detector role. It then failed with `ncnn model rejected explicit
input tensor`. Inspection of pinned ncnn found the cause: `DeliverInput()`
calls `Extractor::clear()` before input; ncnn's `clear()` empties its blob
vectors, and `input(index, VkMat)` rejects the now-empty extractor. A reusable
pristine-extractor reset with a RED regression is being implemented. Raw
per-run logcat and device evidence are ignored under
`out/rev3-task4-evidence/`. No source-to-detector output parity or
three-generation restart PASS has yet been observed. Task 4 remains open.

# Android Vulkan/ncnn — Revision 3 Task 3 V3 prepared-input ABI complete (2026-09-26)

Independent Task 3 SPEC/QUALITY review requested four fixes. A V3 prepared
output with only a 16-byte declared extent or wrong version is now rejected
before any write/callback; `HV_GpuBackendApiV2` declares 48 bytes and
registration checks that extent before reading `prepared`; a failed prepare
advances the token watermark even when discard succeeds; and all V3 callbacks
on one lease are serialized. The four new regression scenarios and the host
table assertion were observed RED as **5/12 failed** with temporary old
behavior. The source was restored byte-identically after this reproducible
RED replay (SHA-256 `396eab8b828c6ce713f50936a1b1fd228dc309f7650a6fc46baf46a63a912ca0`).
The revised focused command passed **12/12**; full native passed **215/215**;
V1/V2 `PluginAbi` passed **10/10**. The ARM64/API26 `humanvision` build,
`.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`
(1810 resolved imports; SHA-256
`f53be3799bb16f522f7493eb7c63bbc29ee6355421587146325e804097dbef24`),
architecture guard and `git diff --check` passed. Ignored evidence is under
`out/rev3-task3-evidence/`: `review-red-native-tests.log`,
`review-green-focused-native-tests.log`, `review-green-full-native-tests.log`,
`replay_review_red.py` and matching before/after source SHA files. Independent
Independent SPEC and QUALITY re-review passed after the fixes. This dedicated
Task 3 commit records the additive ABI; Task 4 has not started.

Task 3 adds a separate V3 query, immutable V1/V2 prefix tables, explicit V3
backend registration/selection, V3-only host services, and a generation-bound,
one-shot prepared-token wrapper with strictly increasing token/generation
ordering. It does not yet create a detached ncnn tensor;
that is Task 4. Existing V1/V2 callbacks and layouts were not edited.

The test-first RED command `pwsh -NoProfile -File
tools/test/run_native_tests.ps1 -Filter GpuAbiV3` failed at C1083 because the
new `humanvision_plugin_v3.h` interface did not exist. This was a compile RED,
not a failing runtime assertion. After implementation, the same focused command
passed **5/5**. Review then found that an A→B→A token sequence could reuse
an old token and a failed discard could lose its recovery handle. Two new
runtime tests failed first (**2 failed of 7**) and passed after the fixes
(**7/7**). A further test caught failed prepare followed by failed discard:
one of two new cases was RED before a poisoned lease retained the token for
destruction and rejected any new prepare. The final focused suite is **9/9**.
The initial full native suite passed **208/208**; after the four additional
boundary tests, `pwsh -NoProfile -File tools/test/run_native_tests.ps1`
passed **212/212**; `pwsh -NoProfile -File tools/test/run_native_tests.ps1
-Filter PluginAbi` independently passed **10/10** existing V1/V2 ABI
regressions. The command
`& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
--build build/android-live --target humanvision --parallel 8` passed for
ARM64/API26. `.venv-reference/Scripts/python.exe
tools/test/verify_android_native.py` passed ELF64 AArch64/API26, ncnn/AHB/Vulkan
symbols and 1810 strong dynamic imports; `libhumanvision.so` SHA-256 is
`042fc1346103a1419700fe5ddc79ecc671f625502cd029fc3463fd67f9beabd6`.
`.venv-reference/Scripts/python.exe
tools/maintenance/check_architecture_boundaries.py` and `git diff --check`
passed. Device performance and the 30 fresh observation FPS gate remain
unmeasured. The 208/208 and 212/212 runs above preceded the review repair;
the final 215/215 and independent review results are recorded at the top of
this section. Task 4 begins only after this separate Task 3 commit.

# Android Vulkan/ncnn — Revision 3 Task 2 model eligibility complete (2026-09-26)

The first ScaleNorm `ReduceL2` conversion now passes all four strict Body26
Snapdragon 888 golden cases: full body, clipped person, mirrored and rotated.
Each case ran twice with an explicit GPU `VkMat` FP16 pack4 input, FP32
arithmetic and subgroup disabled; this result is byte-identical to the
earlier host `Mat` golden. The formal ModelPack gate rejects Mat-only evidence
and verifies all eight Vulkan audit logs, input hashes and small FP32 output
hashes. Both the Mat-only and rehashed audit/coverage/output tamper tests were
observed RED then GREEN after the gate change. The strict route record SHA-256
is `5bed3e57394d9848e8e4479eeca6c7eb8e7e8967f1bff5247a8e04dd6eed73c7`;
the local-only ModelPack manifest SHA-256 is
`0d0d096f9f6ac98eecf41a034795051a0916688cbf96a1a520f20ce28f6c44aa`.
All valid-joint masks match; normalized distance P95 is at most 0.002781413
(required ≤0.01), distance max at most 0.003933497 (required ≤0.03), and
confidence P95 at most 0.006584597 (required ≤0.02). All eight pose runs use
166/166 Vulkan-supported layers with FP16 pack4 input/storage and FP32
arithmetic; repeated SimCC output is byte-identical. The detector's four-image,
eight-output Vulkan regression remains byte-identical. The official export,
conversion and formal Android runner reproduced the accepted pose golden index
SHA-256 `71738a5d47b7a8a30e371dcd941a7ce8eedd86f1036be377a6a86926230b4312`.

The ignored local schema-2 `precision-t-26-ncnn-fp16` ModelPack resolves with
both roles and rejects altered hashes, paths and backend options. The
`android-ncnn-vulkan` Profile bytes are unchanged. The production Android
backend applies role-specific ncnn options before loading each model. The
audited 0002 subgroup-option patch was prepared twice from the pinned archive;
both ARM64/API26 runs succeeded. An Android `humanvision` build and ELF audit
passed; built `libhumanvision.so` SHA-256 is
`405555238bc27e931900611aed86b11d49f5dcf55adbbcd3d4e81203da1953b0`.

Verification commands/results: `.venv-reference/Scripts/python.exe -m unittest
discover -s tests/reference -v` **54/54 PASS**;
`pwsh -NoProfile -File tools/test/run_native_tests.ps1` **203/203 PASS**;
`.venv-reference/Scripts/python.exe -m unittest discover -s tests/architecture -v`
**17/17 PASS**;
`.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`
**PASS**; `pwsh -NoProfile -File tools/setup/prepare_ncnn_android.ps1 -Abi
arm64-v8a -ApiLevel 26 -Jobs 8` **PASS twice**;
`& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
--build build/android-live --target humanvision --parallel 8` **PASS**;
`.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`
**PASS** (ELF64 AArch64 API26, ncnn/AHB/Vulkan symbols and 1810 imports resolved);
`git diff --check` **PASS**. Exact model inputs/outputs, RED/GREEN evidence,
hashes and reproduction commands are in
`docs/validation/RTMPOSE_NCNN_CONVERSION_GATE.md` and ignored
`out/c3-local-runtime/first-norm-reducel2/` (conversion and RED/GREEN) plus
`out/c3-local-runtime/strict-vkmat/` (accepted GPU-input replay and pack).

Independent Task 2 SPEC and QUALITY reviews passed after the strict VkMat
four-case replay and pack-evidence repair. This dedicated Task 2 commit records
model eligibility; Task 3 has not started. Model eligibility is not integrated 30 fresh
observation FPS or physical user acceptance. The pack is local-evaluation-only;
trained-weight redistribution is not approved. No Release, main merge or push.

# Android Vulkan/ncnn — Revision 3 Task 2 non-subgroup gate still blocked (2026-09-25)

The single audited option-aware subgroup-macro candidate fixes the isolated
Reduction (26/26 exact) and Gemm (676/676 exact) tests; real Gemm replay also
passes FP16 tolerance. Full Body26 golden passes 3/4 cases. All four valid
masks and coordinate-distance gates pass, but mirrored confidence P95 error
**0.041543197632 exceeds 0.02**. Eight runs audited 169/169 Vulkan layers;
repeat tensors match byte-for-byte. Four-image detector regression passes
all eight pinned output hashes with 316/316 Vulkan layers. These are
correctness diagnostics, not integrated 30-FPS or hardware acceptance.

Task 2 remains **BLOCKED**; no ModelPack promotion or Task 3. The bounded
attempt stopped without changing shaders, weights or thresholds. Per-joint
mirrored confidence/top-peak evidence confirms identical crop bytes and
inverse-affine transforms but does not identify the remaining cause.
Candidate sources, binaries, libraries and raw results are preserved under
ignored `out/c3-local-runtime/non-subgroup/`; experimental tracked changes
were removed. Baseline ncnn source/provenance and audited AHB0001 were restored,
and `pwsh -NoProfile -File tools/setup/prepare_ncnn_android.ps1` passed.

Fresh `.venv-reference/Scripts/python.exe out/c3-local-runtime/non-subgroup/verify_evidence.py`
verified 139 retained artifacts plus 12 external hashes and expected gate
outcomes. `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v`
passed **46/46**; architecture boundaries and `git diff --check` passed.
Exact RED/GREEN, all-four-case results, timing limits, hashes and replay/build
commands are in [`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).
This is documentation-only failure evidence, not Task 2 completion.

---

# Android Vulkan/ncnn - Revision 3 Task 2 Reduction workaround fails full golden (2026-09-25)

One bounded audited shader workaround selected ncnn Reduction's existing
shared-memory tree. Actual Snapdragon 888 width256 SUM was RED on all 26 rows
(`64+r` instead of `960+4r`), then GREEN with exact results; real layer128
replay also passed FP16 tolerance. The unchanged padded Body26 graph audited
169/169 Vulkan-supported layers, but all four strict pose cases failed valid
masks (reference/device 25/19, 20/16, 25/18, 25/11). Full-body normalized joint
distance P95 is 0.413909 against 0.01. Detector regression passed: all eight
four-image outputs match the pinned golden byte-for-byte. A reused 191-blob
sweep localizes the next catastrophic relative discrepancy to layer154
`Gemm /gau/MatMul_output_0` (P95 absolute 0.228347, correlation 0.435991);
its cause remains unproven. No second patch was attempted.

Task 2 remains **BLOCKED**; no ModelPack promotion or Task 3 work. Experimental
patch/provenance/runner sources and raw outputs remain ignored under
`out/c3-local-runtime/reduction-workaround/`, after restoration of the tracked
implementation. Original ncnn source/provenance were restored and baseline
`pwsh -NoProfile -File tools/setup/prepare_ncnn_android.ps1` passed. This is
failure evidence only. Fresh evidence verification passed 484 artifact hashes;
`.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v`
passed 46/46; architecture boundaries and `git diff --check` passed.
Exact commands, all four numerical/timing results,
source/build hashes and replay mapping are in
[`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).

---

# Android Vulkan/ncnn — Revision 3 Task 2 layer localization (2026-09-25)

The approved follow-up's pinned padded graph and crop received one diagnostic
CPU/Adreno 660 intermediate sweep, with FP16 pack4 Vulkan input/storage and
FP32 arithmetic. All 191 blobs matched by logical shape after FP32 pack1
extraction. The first catastrophic relative and semantic mismatch is layer 129
`/gau/ln/ReduceSum_output_0`: CPU/Vulkan P95 absolute error `0.796001`,
correlation `-0.240461`; layer 128's P95 was `0.00007758`. On all 26 rows,
the GPU output matches the first 64 of 256 input elements, indicating lost
subgroup contributions in ncnn's Vulkan Reduction shader path. The exact
driver/compiler cause and a fix remain unverified. Task 2 stays **BLOCKED**;
no ModelPack promotion or Task 3 work occurred. Exact SHA-256 and replay
steps are in `docs/validation/RTMPOSE_NCNN_CONVERSION_GATE.md`.

---

# Android Vulkan/ncnn — Revision 3 Task 2 combined precision gate blocked (2026-09-25)

The one approved follow-up used the pinned 4-channel zero-padded Body26 first
Conv with FP16 pack4 input and packed/storage, while setting
`use_fp16_arithmetic=false`. On the OnePlus 9 Pro / Snapdragon 888, the runner
audited **169/169 Vulkan-supported layers** and logged the requested option
tuple `1/1/1/0` plus 16-bit pack4 input. The full-body crop's SimCC X/Y are
finite, but versus PyTorch their P95 absolute errors are `33.3674/44.8435`;
argmax agreement is `1/26` and `0/26`. Exact inverse-affine Body26 comparison
has a valid-mask mismatch (`25` reference joints versus `9` candidate), so
the first of the required four cases fails. The remaining cases were not run.
The formal ONNX-based harness additionally cannot load MMDeploy's custom
`AdaptiveAvgPool2d`; a direct device run and same-crop PyTorch comparison
established the failure. Commands, raw hashes and exact ignored-source restore
mapping are in [`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).
Task 2 remains **BLOCKED**; no ModelPack promotion or Task 2 implementation
was made. Task 3 remains closed.

---

# Android Vulkan/ncnn — Revision 3 Task 2 first-Conv padding gate blocked (2026-09-25)

One hash-pinned official ONNX transform padded the first Body26 convolution
from 12×3×3×3 to 12×4×3×3 with an exactly zero fourth input plane. The pinned
MMDeploy converter and ncnn optimizer accepted it; the shape-only Vulkan
finalizer retained 169/169 Vulkan-supported layers. On the same crop, original
versus padded ncnn CPU final SimCC differs by at most 0.0000051. The real
Snapdragon 888 FP16 pack4 first Conv now matches the original CPU result with
P95 absolute error 0.00605, but final SimCC X and Y are each entirely NaN.
The first full-body case therefore fails and the required four-case golden
cannot pass. Task 2 remains **BLOCKED**; no schema-2 ModelPack was promoted.
Exact commands, hashes and raw log paths are in
[`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).

---

# Android Vulkan/ncnn — Revision 3 Task 2 official-path failure evidence (2026-09-25)

The pinned MMDeploy ncnn FP16 static RTMPose-t Body26 preset and its modified
converter produced a 169-layer graph. After each of its seven `ExpandDims` and
five `Squeeze` boundaries was proven shape- and value-equivalent on this fixed
graph, a hash-pinned finalizer replaced those CPU-only operators with Vulkan
`Reshape`. The Snapdragon 888 runtime audit then passed **169/169 Vulkan
layers**. Pinned ncnn CPU final SimCC matched PyTorch closely (X/Y P95 absolute
error `0.001402/0.001529`), but the real first GPU golden case failed: explicit
GPU FP16 pack4 input yielded X/Y P95 error `17.4789/19.9902` and 0/26 argmax
agreement on each axis. FP32 pack1 input matched CPU at the first Conv (P95
`0.00605`) yet ended in all-NaN SimCC. GPU-uploaded FP16 pack4 reproduced the
manual pack4 route byte-for-byte, diverging at the first Conv (P95 `6.9510`).
The pack4 Conv mismatch's exact cause is unresolved. The next architecture
decision must provide a GPU FP16 pack4 path with first-Conv and final SimCC
parity before repeating the four-case golden. Task 2 remains **BLOCKED**; no
ModelPack is eligible, no Task 2 completion is claimed, and Task 3 is closed.
Commands, hashes and preserved diagnostics are in
[`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).

---

# Android Vulkan/ncnn — Revision 3 Task 2 blocked pose conversion (2026-09-25)

Task 2 remains **BLOCKED**. The official RTMPose-t Body26 static ncnn graph
failed the Snapdragon 888 pose golden in three bounded conversion/precision
attempts. Its ExpandDims/Squeeze layers also violate the no-CPU-fallback gate.
The experimental sources and raw outputs remain ignored under
`out/c3-local-runtime/`; no eligible ModelPack or Task 2 completion commit
exists. This status records failure evidence only. Commands, model/tool hashes,
device results and the narrower next gate are in
[`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).
Task 3 remains closed.

---

# Android Vulkan/ncnn — Revision 3 Task 1 local RTMDet eligibility (2026-09-25)

Revision 3 Task 1 reproduces the RTMDet Nano detector as a strict **local
evaluation** candidate under `out/c2-local-detector/`; no production detector
or integrated performance pass is claimed. The approved Revision 3 cadence
design permits this eligibility gate before RTMPose packaging. The prior
every-frame complete-detector P95 values remain failures. Task 2 and later
work wait for Task 1 review and commit; no main merge or Release is authorized.

Tests started RED: the new reference test failed with
`ModuleNotFoundError: tools.models.ncnn.finalize_rtmdet_eval` before the
finalizer existed. The first native contract-only filter passed because
generic `InputContract` already admitted FP16 pack1. Review then required an
extractor-delivery regression: with a forwarding-only handoff, the focused
test failed because pack4 and FP32 tensors reached the fake extractor; after
the shared handoff checked `c=3`, `elempack=1`, `elembits=16`, it passed. The
Android session calls that same handoff for both first and repeated roles.

Fresh commands/results:

- `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_rtmdet_eval.py -v`: **5/5 PASS**.
- `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v`: **46/46 PASS**.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter NcnnDetectorPack1Input.ExtractorReceivesOnlyThreeChannelFp16Pack1`: **RED 1/1 failed** before validation, **GREEN 1/1 passed** after validation.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **198/198 PASS** after review fix.
- `.venv-reference/Scripts/python.exe -m tools.models.ncnn.diagnose_c2_failure -v`: **4/4 PASS**, including four-image real golden.
- `.venv-reference/Scripts/python.exe -m tools.models.ncnn.audit_ncnn_graph --manifest out/c2-local-detector/model.json --param out/c2-local-detector/model.param --onnx out/c2-detector/rtmdet-nano.onnx --checkpoint out/c1-source-cache/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth --bin out/c2-local-detector/model.bin --fixture out/c2-detector/golden/official/image.png --onnx2ncnn out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe --ncnnoptimize out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe`: **PASS**, 316-layer static graph.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`: **PASS**.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**.
- Snapdragon 888 four-image rerun: **316/316 Vulkan-supported layers**, FP16 pack1 input, all eight `cls`/`bbox` tensors byte-identical to C2 golden; `out/` only.

Exact reproduction commands and asset hashes are in
`docs/validation/RTMDET_NCNN_CONVERSION_GATE.md`. Task 1 implementation is
complete for local eligibility; independent task review controls the next
task transition.

---

# Android Vulkan/ncnn — Milestone C Task C2 bounded detector search exhausted (2026-09-25)

The user approved Revision 3 on 2026-09-25. It proposes bounded detector
keyframes with current-frame pose inference while retaining the 30 fresh
complete observation frames/s hard target. The revised Milestone C plan is
`docs/superpowers/plans/2026-09-25-android-ncnn-topdown-cadence-revision-3.md`.
The revised plan was subsequently approved for ordered Task 1 execution.
The failed every-frame C2 result below remains historical evidence rather
than a passed detector. Task 2 awaits independent Task 1 review.

The user-approved C2 extension has evaluated its maximum of two additional
detector candidates after RTMDet Nano and NanoDet-Plus-m 320 failed. PP-PicoDet-XS
320 COCO's person-score and DFL graph plus necessary output downloads measured
**42.0377/42.8649/43.2690 ms P95** across three warmed 100-frame runs.
Original MobileNet-SSD VOC 300's optimized graph plus necessary output downloads
measured **51.8276/36.1623/46.6351 ms P95** across three warmed 100-frame runs.
These are **lower bounds** on complete detector latency: they exclude host
decode and NMS. Every run exceeds the **33.33 ms** TopDown frame period before
pose, bridge, and tracking. Both candidates stopped at the early performance
exit; neither passed four-image golden parity or the strict runtime no-CPU-
fallback gate. Source, hashes, build recipes, raw logs, and limitations are in
`docs/validation/RTMDET_NCNN_CONVERSION_GATE.md`.

The finite C2 search is exhausted. No production detector ModelPack asset was
selected. **C3 remains unauthorized**; further work requires a new user design
decision. The earlier C2 authorization and prescribed-path results remain below
as historical evidence.

---

# Android Vulkan/ncnn — Milestone C Task C2 prescribed paths failed (2026-09-25)

Task C2 exhausted the two approved detector paths on the authorized OnePlus 9
Pro / Snapdragon 888. RTMDet Nano passed its real four-image ONNX/ncnn Vulkan
golden gate, but complete warmed detector P95 (pre-uploaded FP16 input through
graph, required FP32 output downloads, and host decode/NMS) was
**38.7561/39.6981/39.3521 ms** across three 100-frame runs. The approved
official NanoDet-Plus-m 320 substitution also failed: after fixed person/DFL
GPU output crop, its complete P95 was **48.2666/49.5960/49.0909 ms**.
Both exceed the **33.33 ms** TopDown frame period. The selected production
detector ModelPack is empty; no detector is accepted. See
`docs/validation/RTMDET_NCNN_CONVERSION_GATE.md` for source, hashes, golden,
raw logs, and exit evidence. This result triggered the bounded candidate
search authorized above. **C3 is not authorized.** No merge or release is
authorized.
After the failure diagnostic was moved out of default test discovery,
`python -m unittest discover -s tests/reference -q` passed **41/41**;
`python -m tools.models.ncnn.diagnose_c2_failure -q` passed **4/4** against
the ignored local evidence, including raw-to-JSON binding, both models'
hashed failure logs, and converter executable hashes. Controlled missing
evidence and forged converter runs failed as required.
Architecture boundaries and `git diff --check` passed. These are evidence
checks for the impasse, not a C2 detector acceptance.

---

# Android Vulkan/ncnn — Milestone C Task C1 model-conversion gates complete (2026-09-25)

Task C1 adds pinned checkpoint/source/ONNX/ncnn-tool hashes, fixed RGB and
role-specific FP16 packing contracts, static ONNX/ncnn graph audits, and model-bound detector
and Body26 SimCC golden comparisons. The Android NCNN profile now declares a
person score threshold of `0.35`, matching the existing TopDown runtime and
reference exporter; detector golden comparisons cannot override it. Tests
were added before implementation and failed as expected, including adversarial
fixtures for forged manifests, changed model assets, disconnected outputs,
external ONNX weights, invalid pose crops, and missed detections.

Fresh C1 verification: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_ncnn_model_contract.py -v` **30/30 PASS**; the full
`tests/reference` suite passed **41/41**;
`.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`
**PASS**; `git diff --check` **PASS**. PowerShell converter parsing and Python
compilation checks passed. Independent task review found **SPEC APPROVED** and
**QUALITY APPROVED** after the threshold binding fix. The Revision 2 design
requires that profile binding, so C1 adds one profile field beyond the plan's
initial file list; C3 must use that field in the GPU pipeline and regenerate
golden provenance after its final profile/model-pack update.

No RTMDet Nano or RTMPose ncnn graph has yet passed conversion, golden parity,
or Snapdragon 888 inference. Task C2 (time-boxed RTMDet Nano conversion) is
next; Milestone C and the 30 fresh complete observation frames/s acceptance
remain open. Main merge and Release remain prohibited until final physical
acceptance.

---

# Android Vulkan/ncnn — B7 Snapdragon 888 gate accepted; Milestone B complete (2026-09-25)

Authority: Revision 2 design and the ordered implementation plan. The user
enabled USB debugging and authorized direct device collection. B7 ran the
existing `HumanVisionCameraDemo` GPU gate on a OnePlus 9 Pro `LE2120`
(Android 14, SM8350 Snapdragon 888, Adreno 660 Vulkan 1.1.0
`[512.530.0]`). Tested code commit:
`147ed1d5d0f1c9eab3e18145a0b8077e80e405f6`. The Development/IL2CPP
ARM64 API 26/Vulkan gate APK is
`out/android-gpu-gate-runtime/humanvision-gpu-bridge-gate.apk`, SHA-256
`42de1691606e76e99aec8db75ece8ca7ec20c3ead1c54cea6ac068610fef88ce`.

The accepted collection command was:

```powershell
pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -Serial e7c07019
```

`out/device-gates/milestone-b/report.json` reports
`PASS_CANDIDATE_REQUIRES_USER_REVIEW`: 23/23 checks passed over 10.003 minutes.
Raw `logcat.txt` inspection confirmed 2,136 gate statuses, zero nonempty
status errors or native/Unity fatals, six complete source-generation probe
blocks, maximum status gap 7.678 s, and final
submitted/imported/converted `11992/11991/11991`. Final `noSlot=6` and
`generationDrops=14` are recorded, not hidden. Portrait, left landscape
(Unity `Landscape` alias), right landscape, pause/resume, focus loss/return,
and camera restart all appeared with recovery and renewed GPU progress.
Actual AHB format `1`, usage `0x100`, features `0xFFD83` supported selected
blit path `1`; ncnn imported a sampled/read-only image (usage `4`). Unity and
ncnn device/driver UUIDs matched exactly. The color-attachment candidate was
not allocated after blit passed, per the approved probe strategy.

The first ten-minute run is retained at `out/device-gates/failed-slot-busy/`.
It failed two analyzer checks after transient ring slot-publication contention
caused a path-zero status with `Unity Vulkan slot transition/publication is
pending recovery`. Commit `147ed1d` repaired the race; the rerun passed.

Fresh Milestone B host verification commands and results:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **196/196 PASS**.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`: **75/75 EditMode PASS**.
- `pwsh -NoProfile -File tools/test/test_android_gpu_bridge_gate_analysis.ps1`: **37/37 PASS**.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo`: **PASS**; APK and recursive seven-library closure verified, with the SHA-256 above.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`: **PASS** before physical collection.

The B7/Milestone B GPU Bridge engineering gate is accepted; evidence and
limitations are recorded in
[ANDROID_NCNN_VULKAN_AHB_GATE.md](validation/ANDROID_NCNN_VULKAN_AHB_GATE.md).
This test-only gate has no detector, pose model, skeleton output, or production
ModelPack. Milestone C (RTMDet Nano then RTMPose TopDown) is next and has not
started. RTMO remains Milestone D. The production 30 fresh complete
observation frames/s and final user physical acceptance remain open. Do not
merge main or publish a Release before that final acceptance.

---

# Android Vulkan/ncnn — B7 probe evidence bound to each source generation (2026-09-25)

B7 review found a second false pass: after camera restart, Unity reset its
last-probe cache while the native producer retained the previous generation's
diagnostic. A pending status could therefore log stale probe lines, and the
collector accepted one global candidate block for the entire ten-minute run.
The old analyzer passed a full-duration fixture with no probe after restart
(expected RED).

Gate probe publication is now tied to the current native source lease token.
Begin/End revoke the old token; only successful measurement/configuration
publishes the new token. Unity labels each rebuild, probe line and status with
the same source generation. The collector requires a complete candidate,
producer, consumer and external-memory probe for every configured generation,
with the matching generation label and timestamps between its rebuild marker
and first configured status. It rejects a missing, stale, early or duplicate
selected-candidate probe. Rejected blit evidence may precede a successful
color-attachment candidate in the same generation.

Fresh host verification:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter 'GateProbeBelongsOnlyToCurrentSourceLease'`: **1/1 PASS**; an old callback token remained invisible after End/Begin, and the new token became visible.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **194/194 PASS**.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`: **75/75 PASS**.
- `pwsh -NoProfile -File tools/test/test_android_gpu_bridge_gate_analysis.ps1`: **35/35 PASS**.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo`: **PASS**; Android ARM64 API 26 native, Unity Development IL2CPP/Vulkan APK, and seven-library dependency closure. APK SHA-256: `3727263c1e5e87e76a015d95607420b282081ce126e8942fa0ec2e5cd377e70f`.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`: **PASS**; same APK/hash, no ADB executed.

B7 and Milestone B remain open for the complete physical-device gate and user
acceptance. C/D remain unopened.

---

# Android Vulkan/ncnn — B7 device smoke reached GPU path; status contract repaired (2026-09-25)

The first Snapdragon 888 smoke after APK dependency repair reached the bridge:
the sampled-only blit path selected actual AHB format 1 and usage `0x100`,
Unity/ncnn device and driver UUIDs matched, and imported/converted each rose
from 0 to 1261. It was a short smoke, not the ten-minute lifecycle gate.
Its status lines incorrectly placed successful B2 AHB probe measurements in
`error=`, so the strict collector reported `no_status_error=False` even while
conversion progressed. Replaying `out/device-gates/smoke-preload/logcat.txt`
through the analyzer reproduced that expected failure before the fix.

The development gate now queries producer faults separately from AHB probe
evidence. Every measured candidate line is logged once per source generation
as `HV_GPU_GATE probe ...`; routine status uses `error=<none>`. Native
producer/configuration/bridge and ncnn consumer faults still surface as errors.
The collector accepts transient `result=1/path=0` with zero AHB fields and
UUIDs while initial measurement is pending. A later pending status requires
an explicit source-rebuild marker and recovery to path 1/2 within 30 seconds;
an unmarked fallback or non-pending path zero fails. It validates the selected
nonzero path's actual AHB contract and exact UUIDs. A rejected blit candidate remains in the probe log
when the measured color-attachment candidate succeeds.

Fresh host verification:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **193/193 PASS**.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`: **75/75 PASS**.
- `pwsh -NoProfile -File tools/test/test_android_gpu_bridge_gate_analysis.ps1`: **32/32 PASS**, including a real Snapdragon 888 smoke excerpt, initial and post-rebuild pending path, unmarked path-zero regression, separate probe, rejected-blit color fallback and erroneous status probe.
- `pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo`: **PASS**; Android ARM64 API 26 native and Unity Development IL2CPP/Vulkan APK, seven-library dependency closure verified. APK SHA-256: `fb888c1f68a709fe31c042344b05d988b13cdfe26b0588417693e2eceda7f7c2`.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`: **PASS**, same APK/hash; no ADB command executed.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py` and `git diff --check`: **PASS**.

B7 and Milestone B remain open for the full Snapdragon 888 lifecycle run and
user acceptance. C/D remain unopened.

---

# Android Vulkan/ncnn — B7 APK dependency repair; device acceptance open (2026-09-25)

The first Snapdragon 888 launch reached Android's dynamic loader but failed
before the GPU gate started: `libhumanvision.so` needed `libavformat.so`, which
was absent from the gate APK. A recursive `DT_NEEDED` audit of the Android
ARM64 libraries found seven packaged libraries in the complete closure:
`libhumanvision.so`, `libonnxruntime.so`, `libavformat.so`, `libavcodec.so`,
`libavutil.so`, `libswscale.so`, and `libswresample.so`. All other dependencies
resolve in the Android API 26 NDK sysroot. The old APK failed the new audit
with `Gate APK missing required ARM64 library: libavcodec.so` (expected RED).

The gate build now stages only the closure from the native build and pinned
`out/live-deps` inputs, then verifies each APK entry against its source SHA-256.
Fresh verification: `pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1
-ProjectPath unity/HumanVisionDemo` **PASS**; the seven-library APK audit **PASS**.
Rebuilt APK SHA-256:
`5efead83bc8a136e912f73e5d68f29f3a2223e198631d709b5a69346f73691f1`.
This repairs packaging only; B7 and Milestone B remain open pending renewed
physical-device collection and user acceptance. C/D remain unopened.

---

# Android Vulkan/ncnn — B7 collector review round 3; device acceptance open (2026-09-25)

Independent rereview found a collector false pass: an otherwise valid log
with `E Unity: InvalidOperationException: Gate source lease failed` still
returned `PASS_CANDIDATE_REQUIRES_USER_REVIEW` under the prior analyzer. The
submit-exception fixture already returned overall `FAIL` because its `failed:`
text tripped the separate path-contract check, but the fatal-error check
incorrectly passed. New fixtures exposed both gaps. The analyzer now rejects
error-severity Unity lines and explicit gate exception lines even if tagged as
informational; ordinary Unity warning lines remain acceptable. It covers gate
source-lease, submit and startup errors.

- `pwsh -NoProfile -File tools/test/test_android_gpu_bridge_gate_analysis.ps1`: **24/24 PASS** after the fix, including the expected Unity exception/error rejections and harmless-warning pass.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`: **PASS**, verifies APK SHA-256 `965e95d5d7b1b480b25684191b73255c9deea9a57833d9effa428c5b71aba735`; no ADB command executed.
- Analyzer PowerShell parse and `git diff --check`: **PASS**. This round changes only the analysis script, its fixture and documentation. The round-2 Unity/APK/native build evidence below remains the binary baseline.

B7/Milestone B remain open for user device acceptance; C/D remain unopened.

---

# Android Vulkan/ncnn — B7 collector review round 2; device acceptance open (2026-09-25)

Independent rereview found that the filtered logcat omitted native crash
records, and a ten-minute sleep with only three early status lines could still
produce a pass candidate. The previous `error=` check also accepted a
multiline diagnostic whose first line was empty, and lifecycle recovery did
not require later GPU progress. A new sparse-status fixture first **failed**
against the prior analyzer: it incorrectly returned a pass candidate.

The collector now captures unfiltered `main`, `system` and `crash` buffers.
The gate emits `error=<none>` for clean status or a single-line escaped error.
The analyzer requires timestamped statuses spanning at least 570 seconds,
starting and ending within 30 seconds of the capture bounds, with no gap over
60 seconds. Import and conversion must progress in the final 90 seconds and
after both pause and restart recovery. All checks remain a screen for user
device acceptance, not a device pass.

Fresh verification on this worktree:

- `pwsh -NoProfile -File tools/test/test_android_gpu_bridge_gate_analysis.ps1`: **19/19 PASS**; sparse, missing-tail, long-gap, late-stall, crash-dump, multiline-error and post-recovery-stall fixtures reject.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`: **75/75 EditMode PASS**.
- `pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo`: **PASS**, Development IL2CPP ARM64 APK; SHA-256 `965e95d5d7b1b480b25684191b73255c9deea9a57833d9effa428c5b71aba735`.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`: **PASS**, same APK/hash, no ADB executed.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py` and `.venv-reference/Scripts/python.exe tools/maintenance/generate_component_catalog.py --check`: **PASS**.
- Collector PowerShell parser: **PASS**. Native code was unchanged in this review round; the previous **193/193** native suite and gate-OFF export audit remain the baseline.

---

# Android Vulkan/ncnn — B7 review fixes verified; device acceptance open (2026-09-25)

B7 independent review found three host blockers after `c2181bf186da11d5ed74b452ff4925fdba4dc5ab`:
the configuration render event reused one mutable payload address across source
leases; the B2 ncnn probe created a GPU instance outside the backend's owner
count; and the device collector could report a pass candidate despite later
errors, stalled imports or incomplete lifecycle evidence. The added
`QueuedOldConfigurationCannotAliasNewLeaseRequest` test first failed when an
old queued event arrived after End/Begin/new Prepare on the same texture.
Configuration events now carry the 64-bit lease token as opaque callback data,
with no payload pointer to reuse or free. B2 probe and backend sessions share
one counted ncnn instance lease; the producer releases its lease after bridge
teardown. The collector requires every status to have an empty error, rising
imported and converted counters, selected-path/actual-usage agreement, no
native fatal, all orientations, ordered pause/focus/restart and recovery logs.
The gate component emits explicit focus and post-recovery markers.

Review-fix verification on this worktree:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter 'QueuedOldConfigurationCannotAliasNewLeaseRequest'`: expected RED, 1/1 failed before the event fix.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **193/193 PASS**, CTest 14.48 s after the final native change.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter 'QueuedOldConfigurationCannotAliasNewLeaseRequest|StaleConfigurationEventCannotMeasureReusedTexture'`: **2/2 PASS** after the final native change.
- `pwsh -NoProfile -File tools/test/test_android_gpu_bridge_gate_analysis.ps1`: **12/12 PASS** including error, stall, fatal, path and missing lifecycle rejection fixtures.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`: **PASS**; the gate build repeats the Android API 26 native compile.
- The same gate-OFF Android build was repeated after the final source change; NDK `llvm-nm --defined-only build/android-live/bin/Release/libhumanvision.so` found **zero** `HV_AndroidGpuGate*` definitions.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`: **75/75 EditMode PASS**.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`: **PASS**.
- `.venv-reference/Scripts/python.exe tools/maintenance/generate_component_catalog.py --check`: **PASS**.
- `pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo`: **PASS**, Development IL2CPP ARM64 APK; SHA-256 `a269174f9e95d6262772c1ab6891fd560b1a44d10cccf169e4f7828f6a1185eb`.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`: **PASS**, verified the same APK/hash; no ADB command executed.

This is host verification only. B7 and Milestone B remain open until the user
returns a physical-device report and accepts it; C/D remain unopened.

---

# Android Vulkan/ncnn — B7 host gate built; device acceptance open (2026-09-25)

Authority: Revision 2 design and the B7 plan. B6 at
`cdf5651285f1755ede75596c29c9947e645eb472` is the approved base. B7
adds a render-event measurement of the actual active Unity Vulkan camera
image, exact ncnn device/driver UUID selection, B2 AHB capability probing,
and control-worker configuration of the production three-slot producer.
The render callback only copies source facts; allocation/import work is on
the control worker. First frames pending configuration count as generation
drops. A development-only gate consumer reuses the B5 ncnn cached import,
GPU preprocessing and explicit FP16 pack4 conversion with a generated input
contract, with no model inference or skeleton. Production NCNN model assets
remain absent until Milestone C and the ordinary validator remains strict.

Fresh host verification on this worktree:

- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`: **192/192 PASS**,
  CTest 15.20 s. Includes the render-event and stale lease/configuration
  regressions.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -NoProfile -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`:
  **75/75 EditMode PASS**.
- `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26`:
  **PASS**. `llvm-nm --defined-only` confirms the default native build has no
  `HV_AndroidGpuGate*` exports.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`:
  **PASS**. `.venv-reference/Scripts/python.exe tools/maintenance/generate_component_catalog.py --check`:
  **PASS**.
- `pwsh -NoProfile -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo`:
  **PASS**, Unity 2021.3.45f1 Development IL2CPP ARM64 APK, API 26 minimum,
  Vulkan first, `android-ncnn-vulkan` manifest mode/profile, CAMERA permission,
  `libhumanvision.so` and `libonnxruntime.so` packaged. APK SHA-256:
  `95854922748cccdc533bee98266ac4cd99acc079627bc1baf0d445becbfbb986`.
- `pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -DryRun`:
  **PASS**; checked the exact APK/hash/output path without issuing ADB commands.

The APK is a **host-built gate artifact, not a device pass**. On the user’s
Snapdragon 888, `tools/test/collect_android_gpu_bridge_gate.ps1` must record
the ten-minute camera, AHB format/usage/features, actual copy path, exact
UUID pair, cached import/conversion progress, drops, orientation and lifecycle
evidence. See [B7 device gate](validation/ANDROID_NCNN_VULKAN_AHB_GATE.md).
Until the user returns and accepts that report, B7 and Milestone B remain
open; Milestones C/D must not start.

---

# Android Vulkan/ncnn — Milestone A closed; hardware gates pending (2026-09-14)

Authority: Revision 2 of
`docs/superpowers/specs/2026-09-13-android-vulkan-ncnn-production-runtime-design.md`
and the implementation plan of the same name. Milestone A closes only the
explicit Android mode registry/bake, strict staged profiles and ModelPack
contracts, and additive GPU plugin/Android submission ABI. Milestone B has not
started in this commit.

All Milestone A automated gates are green; Milestone B is the sole authorized
next milestone. Physical-device acceptance remains pending and is not part of
this transition.

The Android player bakes one user-selected mode: `android-ncnn-vulkan`,
`android-ort-xnnpack`, or `android-ort-cpu`. There is no automatic mode or
fallback. Empty/`auto` resolves to the baked profile and a conflicting explicit
profile fails with both IDs. Each profile has one pipeline and one backend with
`allow_fallback: false`; missing NCNN capability/library/model requirements do
not route into ORT. See maintenance decision
[0002](maintenance/DECISIONS/0002-android-runtime-mode-and-gpu-abi.md).

## Fresh Milestone A verification

Base/commit evidence: A5 started at A4 commit
`32066b565da85877f16931ab2532a749d846af1c` (parent
`adb2829add9bb0260b983a44b93d123721c58cb1`) on
`codex/android-ncnn-vulkan-implementation`. The A4 diff leaves
`humanvision_c.h`, `humanvision_types.h`, `humanvision_plugin.h`, and
`humanvision_v2.h` unchanged; the full native suite includes their layout and
runtime contract snapshots. Unity public skeleton APIs remain semantic and do
not own model/provider types. V2 adds opaque GPU-frame/pipeline/backend/host
extension tables: backend owns GPU/tensor work, pipelines own model semantics
and decoding, common services own region/canonical/tracking/temporal/snapshots,
and Unity submits/renders semantic data only.

- `.venv-reference/Scripts/python.exe --version`: Python 3.10.21;
  `pwsh --version`: 7.6.5; CMake: 4.3.1-msvc1; Git: 2.49.0.windows.1;
  Unity: 2021.3.45f1 at `D:/Developer/2021.3.45f1/Editor/Unity.exe`.
- `pwsh -File tools/test/run_native_tests.ps1 -Fresh`: PASS, 95/95, 0 failed,
  CTest real time 12.54 s (MSVC v143/Ninja Multi-Config).
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -File tools/test/run_unity040_tests.ps1 -Unity 'D:/Developer/2021.3.45f1/Editor/Unity.exe'`:
  PASS, 62/62 EditMode tests, 0 failed.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`:
  PASS (public-surface contract and architecture/documentation boundaries).
- `.venv-reference/Scripts/python.exe tools/maintenance/generate_component_catalog.py --check`:
  PASS.
- `pwsh -File tools/package/build_live_native.ps1`: PASS, Windows x64 and
  Android ARM64 native libraries built (the package input build; tests were not
  run by this script). `.venv-reference/Scripts/python.exe
  tools/package/package_live_sdk.py`, then `tools/package/package_upm.py`, then
  `tools/package/verify_package_isolation.py`: PASS, 107 Unity assets, 112 UPM
  files, 137 GUIDs. The dry run regenerated `upm/`; those generated files were
  restored to A4 before staging and are not part of this documentation commit.

Expected staged validation: `HumanVisionAndroidRuntimeBuildValidatorTests` ran
inside the 62/62 EditMode suite. Its complete-NCNN fixture, with only each model
field removed in turn, reports `HasNcnnModelPackAssets` and
`HasNcnnModelPackSha256Index` as the required errors, including “Milestone C has
not installed them”; it does not substitute an ORT mode. This expected rejection
exists because the schema-2 `precision-t-26-ncnn-fp16` model pack and SHA-256
index are deliberately absent until Milestone C. It is not a production NCNN
build, device, or performance result.

`git diff --check` and `git diff --cached --check` are required again before the
A5 commit; generated UPM files and ignored SDD artifacts are excluded from its
staging set.
No hardware behavior is accepted: Vulkan/AHB allocation, ncnn linking/import,
camera copy, model conversion/inference, Android APK execution, device FPS,
latency, accuracy, thermal behavior, and 1–8-person skeleton quality remain
future/user acceptance gates.

---

# 0.4.0-preview.3 — Android provider baseline and presentation continuity (2026-09-13)

Authority: `HumanVisionSDK_Android_0403_Codex_Optimization.md` and the frozen
[diagnostic plan](diagnostics/ANDROID_0403_PLAN.md). User recordings from preview.2
showed RTMO 3.9–5.8 raw body FPS, 299–397 ms result age and periodic
`SampledBodies=0` on OnePlus 9 Pro / Snapdragon 888. The fixed 200 ms sample expiry
was a confirmed cause of visible flicker; it was not the cause of slow inference.

Implemented:

- Motion prediction remains bounded to 25 ms. Presentation now holds the last
  filtered body for an observation-period-based 300–800 ms window, while track loss
  remains independently bounded at 800 ms and hand endpoints at 200 ms.
- Added forced `android-cpu-nohands`, `android-xnnpack-nohands` and
  `android-nnapi-nohands` profiles. Each disables hands and requests exactly one
  provider with fallback disabled.
- Added an Android XNNPACK session with four intra-op threads; all Android ORT
  sessions use sequential execution, one inter-op thread and disabled spinning.
- Added 4 Hz diagnostics for profile/pipeline/provider identity, raw/tracked/sampled
  counts, body stage timings, result/sample age, observation EWMA, adaptive hold,
  input/body drops and pipeline-specific detector/pose metrics.
- Camera/settings Demo can select the three benchmark profiles. Apply/Start
  recreates the native Runtime; the HUD shows the active profile.
- Packaged the six-run device matrix and official ORT Mobile model-usability reports.
  The checker did not recommend NNAPI for the current RTMO, RTMDet, Body26 or Hand21
  models. This is graph inspection, not phone performance proof.
- QNN remains deferred. The documented next route is QAIRT plus custom ARM64 ORT
  with `--use_qnn static_lib`; start with FP32/HTP and optional HTP FP16 precision.

## Automatically verified

- `pwsh -File tools/test/run_native_tests.ps1`: 72/72 PASS, 9.91 s.
- `pwsh -File tools/package/build_live_native.ps1`: Windows x64 and Android ARM64
  Release PASS.
- `pwsh -File tools/package/compile_managed.ps1`: Runtime/Demo/Editor and Android
  conditional compilation PASS; existing serialized JsonUtility CS0649 warnings only.
- `$env:__COMPAT_LAYER='RunAsInvoker'; pwsh -File tools/test/run_unity040_tests.ps1`:
  Unity 2021.3 EditMode 44/44 PASS, including the benchmark-profile contract.
- `generate_component_catalog.py --check`, `check_architecture_boundaries.py`,
  public-surface checks and package-only autocrlf regression: PASS.
- `verify_package_isolation.py`: PASS, 102 Unity assets, 107 UPM files, 131 GUIDs.
- Local preview.3 tgz import in Unity 2021.3: PASS; packaged Runtime initialized,
  all RuntimeData hashes/GUID isolation verified, camera/settings scenes generated.
- Remote Git UPM import at immutable commit
  `052b81263e99b6124335ce6da029cd863e74f022`: PASS; packages-lock reports the
  exact Git hash and the packaged Runtime/hash/GUID/scene checks passed again.
- All seven GitHub Release assets were downloaded to an independent directory and
  match the local artifacts byte-for-byte by SHA-256.

## User manual acceptance pending

Run only the six combinations in
[ANDROID_0403_DEVICE_BENCHMARK](diagnostics/ANDROID_0403_DEVICE_BENCHMARK.md) on the
same device/camera/scene for at least 30 seconds each. Record raw body FPS, result
age, stage timing, requested/actual backend, both drop counters, sampled bodies and
flicker. Select the fastest provider separately for TopDown and RTMO. If best RTMO
remains below 15 raw body FPS or above 180 ms result age, enter QNN HTP next.

No automated result certifies Android camera FPS, latency, accuracy, thermal
behavior, 1–8-person acceptance or hand quality.

Published [v0.4.0-preview.3](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.3)
as a GitHub prerelease on 2026-09-13T02:28:25Z. Annotated tag and release code point
to `052b81263e99b6124335ce6da029cd863e74f022`; remote main includes this publication
record.

---

# 0.4.0-preview.2 — Git import newline repair (2026-09-12)

User screenshot: `HumanVision runtime hash mismatch: profiles/auto.json`, followed
by missing Runtime/index.json and HTTP404. Actual user PackageCache file is445bytes
with12CRLFs; published payload is433bytes withLF. SHA256 of CRLF-converted source
exactly matches user's file. Root repository .gitattributes did not protect the
UPM package-only checkout. The preview.1 tgz/import and committed-blob tests did
not cover that Git materialization step.

Fix: package-local .gitattributes disables payload newline rewriting. Installer
also recovers JSON/Markdown newline changes only when restored bytes match the
original SHA256; modified content and ONNX weights never bypass byte verification.
Cache files are not edited. Runtime index is still published only after all entries
validate. No inference, skeleton or camera behavior changed in this patch.

Regression first: package-only Git checkout with autocrlf=true failed on auto.json;
UPM import regression failed on missing text repair. Final verification:
- Package-only Git checkout with autocrlf=true: PASS.
- Installer newline recovery, text tamper rejection and binary exclusion: PASS.
- Actual remote Git URL at de518d023bba9a428fea2a2d8ba090fbe5949a0a: PASS in
  isolated Tuanjie2022.3; packages-lock source=git and exact commit verified.
  Installed auto.json is433bytes and matches the published SHA256 exactly.
- Native initialization, full RuntimeData/index installation, GUID isolation and
  generated camera/settings scenes: PASS.
- Archive isolation92Unityassets/97UPMfiles/119GUIDs and architecture/public API: PASS.
Published [v0.4.0-preview.2](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.2)
on2026-09-12T13:38:53Z (not draft). All7remote assets downloaded and SHA256 matched.
Release tag337eed82a0dc53bf9cf10be62f329e29a6226070; main includes publication record.
User project/cache files were inspected read-only; update its Git dependency from
preview.1 to preview.2 and restart Play Mode. No cache deletion is required.

Previous native63/63 and Unity43/43 are unchanged-core baseline, not newly executed
patch results. Physical device acceptance remains pending as described below.

---

# 0.4 v2 — published; physical acceptance pending (2026-09-12)

Authority: [master v2](plans/040-v2/2026-09-10-humanvision-040-master-v2.md)
and its plugin architecture addendum. Maintenance starts at
[START_HERE](maintenance/START_HERE.md). Earlier entries below are history.

Tasks 1–6 are committed: frozen V1 contracts; C Plugin ABI/Host; registry,
ModelPacks/Profiles; legacy adapter; backend factories and optional providers;
real RTMO, TopDown Body26 and independent Hand21 pipelines.
Tasks 7–9 implementation is complete: common Hungarian/velocity tracking, region
masking/revision isolation, canonical derivation, asynchronous fair hand scheduling,
bounded adaptive temporal samples, additive V2 C ABI/semantic Unity integration,
batched UGUI skeleton mesh and actual-provider diagnostics.
Task 10 maintenance documentation and generated component metadata are implemented.
Tasks 11–12 complete: architecture/documentation/archive/import gates PASS;
main and annotated tag pushed; GitHub prerelease published2026-09-12 08:43:48 UTC.
Release code commit:45a833a1627d719befaa66cec2216f53fc5d55ed.
[Download v0.4.0-preview.1](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.1)

All seven remote release assets were downloaded and their SHA-256 hashes exactly
match local artifacts. All96 committed Git UPM payload hashes also match the tested
archive. The release is not a draft. Current next gate is USER physical acceptance;
there is no unfinished mandatory non-hardware implementation milestone in this plan.

## Fresh verification

- `pwsh -File tools/test/run_native_tests.ps1`: 63/63 PASS, 11.94s (final diagnostic-label regression included).
- `-Filter CommonServices`: 12/12 PASS, including stationary-jitter/reversal,
  region-locked identity/old-frame, sixteen-hand fairness and per-hand cadence.
- `pwsh -File tools/package/build_live_native.ps1`: Windows x64 and Android ARM64 PASS.
  Android uses NDK23, API24; NDK21 filesystem linkage failed and was replaced.
- `pwsh -File tools/package/compile_managed.ps1`: Runtime/Demo/Editor and Android
  conditional compilation PASS. JsonUtility/serialized-field CS0649 warnings remain.
- `pwsh -File tools/test/run_unity040_tests.ps1`: 43/43 EditMode PASS in isolated
  Unity2021.3 project, also rerun PASS with Tuanjie2022.3.61t4: actual packaged native P/Invoke, canonical/legacy projection,
  ABI layouts, RTSP clock age conversion, eight-body mesh/coordinate/thickness checks.
- `pwsh -File tools/test/run_upm040_import.ps1`: isolated tgz import PASS on Unity2021.3 and Tuanjie2022.3;
  native runtime initialization, generated camera/settings scenes, all installed
  data hashes and independent StreamingAssets/UPM GUIDs verified. Final regenerated
  tgz reimport on Tuanjie2022.3 PASS, using a SHA-addressed input path to prevent
  stale same-version UPM cache reuse.
- Hand21 golden uses independent OpenCV/ORT preprocessing and inverse ROI transform;
  actual palm/index-tip/thumb match within1.5px. No fabricated production joints.

Regression-first fixes: crossing identity, fast-hand priority, all16-hand fairness,
per-hand cadence (not a global15-job ceiling), expired hands, C import spelling,
NDK filesystem linking and RTSP/Unity clock-origin mismatch. Source timestamps
remain original; rendered samples do not increment raw result sequence.

- Final `verify_package_isolation.py`: PASS (92 Unity assets,96 UPM files,118 GUIDs).
- Catalog `--check`, architecture/documentation and public-surface guards: PASS.
- Final release hashes: out/releases/0.4.0-preview.1/SHA256SUMS.txt.

## Explicit acceptance limits

Physical Windows/Android camera/RTSP, 1/2/4/6/8 people, fresh complete skeleton FPS,
latency, accuracy, hand visibility and thermal behavior remain USER ACCEPTANCE.
The Demo requests60Hz display/sampling independently of inference.
No automated pass establishes 8-person30FPS or phone latency. Hand updates are
independent, bounded by actual inference capacity, and expire separately.
Optional QNN defaults OFF. Its source/syntax path was checked; enabled linking,
redistribution dependencies and device execution remain UNVERIFIED without QAIRT
and compatible custom ORT. It is not advertised as an enabled backend in this package.
No RKNN backend is shipped. Existing V1 ABI/public methods/GUIDs remain compatible.
Caches, user archives/media and the user's Unity project are preserved. The prior
preview.5 GitHub Release remains an unpublished, superseded draft.

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

Task 6 final focused SimCC check 1/1 PASS; final default native builds PASS. Current work advances to Task 7 common services. Model-pack generation assets are retained; source archives remain cached.
