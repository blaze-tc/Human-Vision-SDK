# Android GPU Input and Camera Revision 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore correct Android GPU skeleton recognition and establish same-image sensor provenance before passing the unchanged Snapdragon 888 TopDown acceptance gate.

**Architecture:** First isolate exact-source differences through the existing Unity Vulkan/AHB/ncnn route. Then add Camera2 GPU acquisition with bounded image ownership, matched sensor metadata and reusable Unity RGBA producer textures feeding the existing three-slot bridge. Finally verify actual skeleton presentation and complete live-person performance.

**Tech Stack:** Unity 2021.3.45f1, C# and IL2CPP, Android API 26 ARM64, NDK Camera2/ImageReader, Vulkan/AHB/sync-fd, pinned ncnn FP16, C++17, Python reference and evidence tools.

**Spec:** `docs/superpowers/specs/2026-09-26-android-gpu-input-camera-provenance-revision-4-design.md`, approved by the user following commit `ee2d8c0`. Revision 2 and Revision 3 remain controlling outside R4's explicit amendments.

**Status:** Written plan awaiting user review. No task below is complete. Reuse `E:/Project/Human Vision SDK/.worktrees/android-ncnn-vulkan`, branch `codex/android-ncnn-vulkan-implementation`; do not create another checkout or merge main.

## Global Constraints

- Execute Tasks 1–3 (R4.1), then 4–7 (R4.2), then 8–10 (R4.3), in order. A failed stage blocks the next. Milestone C remains active; D stays blocked.
- Preserve the user's execution method: fresh Sol medium implementer per task, independent spec-compliance and code-quality reviews, fixes and re-review, separate verified commit per task. Astra escalation is permitted for unresolved high-risk work. No repeated task-level permission requests.
- V1 ABI and public Unity skeleton API stay byte/behavior compatible; additions use new size/version-prefixed internal bindings. Core does not depend on Unity.
- `NCNN Vulkan` fails fast; ORT modes remain explicitly selected. No WebCamTexture or backend fallback for a failed NCNN camera source.
- Same physical GPU/different VkDevice, UUID matching, measured copy-path selection, read-only ncnn AHB imports, sync-fd and queue ownership remain mandatory. No render-thread wait for inference or resource availability.
- No full source/preprocessed image CPU readback, CPU image conversion, per-frame AHB/import/pipeline creation or long joint prediction. GPU comparisons return bounded summaries only. Offline golden uploads and normal detector/SimCC output downloads are allowed.
- Three existing cached bridge slots; three reusable RGBA producer textures; ImageReader maxImages=6, at most four app-held images, acquire only while held<4; camera import cache at most eight entries.
- Pairing maps: at most eight images/eight capture records per session; unmatched image expires after 100 ms. Camera image holds also obey the stricter four-image limit. Exact timestamp/session/camera match only.
- Input normalized parity limits: max absolute error ≤0.02, mean absolute error ≤0.002. Nearest unorm8 copy ≤one quantization step; geometry/channel/packing must match exactly. Existing model golden tolerances are not relaxed.
- Detector interval 2–6 accepted pose frames, ≤200 ms source-time between capture attempts; new-track detection age ≤200 ms; pose-supported anchor expires at 500 ms or two missed matches. No rejected pose may reuse old joints.
- Target 30 complete observation frames/s: ≥29.0 every rolling 10 seconds, ≥29.5 average over 60 seconds; sensor-age P50≤75 ms, P95≤100 ms, ordinary maximum≤250 ms; combined bridge pressure drops≤1%; zero copy/import errors. Count a complete frame once, require current visible Bodies.
- No new detector, RTMO, Hand, QNN, MediaPipe, Windows/ORT optimization, main merge or Release. Keep 24px line/72px point defaults.
- Preserve caches/user files. Use ignored `out/android-r4/` for evidence; backup and hash files replaced in the authorized open Unity project. Raw private videos/images are not committed.

## Review Focus

1. A GPU parity test accidentally compares two copies of the same wrong tensor: Task 2 injects independent known corruptions at each boundary and checks golden provenance.
2. A paused VideoPlayer reports a frame index while its texture has advanced: Task 3 latches on the render timeline and verifies source identity before admitting evidence.
3. Metadata arrives after pause/resume or from another lens with the same timestamp: Task 5 tests exact camera/session keys and invalidates old epochs.
4. Buffer removal/device loss occurs during GPU sampling: Task 6 proves deferred retirement, single fd ownership and quarantine when completion is unknown.
5. Valid native Bodies exist while the overlay is culled, off-screen or stale: Task 8 separates geometry telemetry from independently captured visual evidence.

## File map and shared verification

New paths below are intentional deliverables, not assertions that those files exist.
Use `U=unity/HumanVisionDemo/Assets/HumanVision` and
`P=upm/com.blazetc.humanvision` as path notation only. Every changed `U/Runtime/*`
maps to `P/Runtime/*`, `U/Demo/*` to `P/Runtime/Demo/*`, and `U/Editor/*` to
`P/Editor/*`. Keep matching source bytes and stable existing `.meta` GUIDs; add
new metas through the normal Unity import process. Tests remain under U.

| Unit | New/primary files | Owner |
|---|---|---|
| Fixed source manifest | `tools/test/r4_fixture_manifest.py`, `tests/reference/test_r4_fixture_manifest.py` | Task 1 |
| Bounded GPU comparison | `runtime/gpu/android/gpu_parity_probe.{h,cpp}`, `tests/runtime/test_gpu_parity_probe.cpp` | Task 2 |
| Real video identity | `tools/test/TopDownEvalVideoSource.cs`, `tools/test/r4_parity_analysis.py` | Task 3 |
| Camera requirements | `runtime/gpu/android/camera_capabilities.{h,cpp}`, `tests/runtime/test_camera_capabilities.cpp` | Task 4 |
| Image metadata/clock | `runtime/gpu/android/camera_frame_contract.{h,cpp}`, `tests/runtime/test_camera_frame_contract.cpp` | Task 5 |
| Native camera lifecycle | `runtime/gpu/android/camera_source.{h,cpp}`, `camera_vulkan_import.{h,cpp}`, `native/include/humanvision/humanvision_android_camera.h`, `runtime/composition/android_camera_c.cpp` | Task 6 |
| Unity source adapter | `U/Runtime/Android/HumanVisionAndroidCameraSource.cs`, `U/Demo/Live/HumanVisionLiveSource.cs` | Task 7 |
| Presentation/evidence | `tools/test/TopDownEvalProbe.cs`, `tools/test/android_topdown_gate_analysis.py` | Tasks 8–10 |

For native tasks register sources in `runtime/CMakeLists.txt` and tests in the
existing test wiring in `tests/native/CMakeLists.txt`/root `CMakeLists.txt` as
appropriate; do not create a disconnected test-only library. Run:

```powershell
pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter <TaskFilter>
pwsh -NoProfile -File tools/test/run_native_tests.ps1
pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26
.venv-reference/Scripts/python.exe tools/test/verify_android_native.py
.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py
git diff --check
```

`<TaskFilter>` below names the test prefix to register. Verify nonzero discovered
tests: CTest's zero-tests exit status is not a pass. A RED must fail an assertion
or expected missing implementation, not an unrelated broken toolchain. Preserve
RED and GREEN logs under the task evidence directory. Full native checks do not
authorize Windows performance changes.

For managed changes run `pwsh -NoProfile -File tools/test/run_unity040_tests.ps1`
using its isolated project, and refresh/check the user's open project through
UnitySkills after backup/sync. Missing prebuilt dependencies are reported and
restored from verified artifacts; do not invent an Editor inference pass from
compilation or turn this into Windows implementation.

Every task's final step includes affected builds/checks, spec then quality review,
repair/re-review, exact result/status update and explicit-file commit. A task that
needs device evidence cannot be marked complete from fake dispatch tests.

---

### Task 1: Pin deterministic input fixtures and reference contracts

**Files:** Create `tools/test/r4_fixture_manifest.py`, `tests/reference/test_r4_fixture_manifest.py`; modify `tools/test/topdown_video_reference.py`; create `docs/validation/ANDROID_R4_INPUT_PARITY.md`.

**Interfaces:** Python `build_manifest(video_path: Path, frame_index: int, output_dir: Path) -> dict` and `validate_manifest(manifest: dict, root: Path) -> None`. Schema 1 contains decoded RGBA byte SHA, video SHA, decoder/version, frame index, width/height, row stride, RGB range/color space/alpha, transforms, expected tensor hashes, model/profile hashes and per-person annotations. Generated RGBA/tensor files remain ignored.

- [ ] Write `R4FixtureManifest` tests: altered byte, wrong frame index, missing decoder, wrong shape/stride and nonfinite tensor each reject; identical extraction from pinned input gives identical bytes/hash. Analytic asymmetric corner/grid fixtures prove all rotations and mirror transforms preserve annotated locations.
- [ ] Run `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_r4_fixture_manifest.py -v`; save expected RED.
- [ ] Implement exact offline decoding/export using the pinned reference tooling; choose and record one full-person frame and one multi-person frame from the supplied videos after viewing them. Do not guess annotations from model output. Serialize exact preprocessing/packing metadata and generate independent expected tensors.
- [ ] Run GREEN and existing video-reference/model golden tests. Record commands/hashes, not private pixels, in the parity report.
- [ ] Review and commit `test(android): pin exact-frame GPU parity fixtures`.

### Task 2: Instrument GPU boundaries and fix the first proven mismatch

**Files:** Create parity files in the map; modify only evidenced boundaries in `runtime/gpu/android/unity_vulkan_bridge.cpp`, `unity_vulkan_plugin.cpp`, `runtime/plugins/backend/ncnn/ncnn_preprocess.cpp`, `ncnn_android_session.cpp`, and build/shader registration. Create `tools/test/collect_android_r4_parity.ps1`.

**Interfaces:** In namespace `humanvision::gpu`, define `ParityStage` (Source, Producer, ImportedRgb, Normalized, Packed), `ParitySummary` (generation/source ID, dimensions/dtype/packing, element count, max/mean error, mismatch count, first mismatch coordinate, fixed sample grid), and `GpuParityProbe::Record(ParityStage, const ParityGpuView&, const ParityGpuView&, const ParityContract&, ParityTicket&) -> bool`; `TryCollect(ParityTicket, ParitySummary&) -> bool` is nonblocking. `ParityGpuView` is a native opaque resource view, shape/stride/dtype/packing and device identity; no Unity types. `ParityContract` carries Task 1 expectations and tolerances. Tickets are a bounded reusable pool. Compile execution hooks only in evaluation builds.

- [ ] Write `R4GpuParity` tests with independent goldens and injected vertical flip, channel swap, clipped copy, stride error, stale slot, scale and packing errors. Assert each fails at its introduced boundary and summaries stay fixed-size. Add pool-pressure and old-generation rejection tests.
- [ ] Run focused RED, implement cached comparison/reduction GPU passes and bounded summary readout. Source and golden must have independently verified identities; comparing two products of the same preprocessing cannot count as a golden check.
- [ ] Extend eval builder with `-R4ParityManifest <path>` (mutually exclusive with video/live acceptance), include manifest hash in APK evidence. Collector accepts `-Manifest <path> -RunLabel <unique> -Serial e7c07019`, verifies installed APK/source/device and writes stage summaries. Reuse existing build/install verification rather than a second app pipeline.
- [ ] On device exercise injected textures through production bridge/preprocess at all transforms and every supported measured copy path. Compare every element via GPU reductions; return no full image/tensor. Record first failing boundary. Apply its minimal regression fix and rerun downstream stages; at most two evidence-backed fix/review attempts per unresolved boundary, then stop with evidence.
- [ ] Run GREEN, affected A/B lifecycle/no-readback tests, Android build/ELF and detector/pose golden parity. Input pass with raw model-output failure stays blocked; use the same reference tensor to isolate ncnn, with no converter rewrite/candidate substitution.
- [ ] Review and commit `fix(android): restore verified GPU input parity` only if demonstrated; if unresolved, document failure without calling this task complete.

### Task 3: Prove the actual VideoPlayer route and close R4.1

**Files:** Modify `tools/test/TopDownEvalVideoSource.cs`, `TopDownEvalBuild.cs`, `build_android_topdown_eval.ps1`, parity collector/report; create `tools/test/r4_parity_analysis.py`, `tests/reference/test_r4_parity_analysis.py`, `U/Tests/EditMode/HumanVisionVideoLatchTests.cs`.

**Interfaces:** Eval component `TryLatchFrame(long videoFrameIndex, long sourceFrameId) -> bool` schedules GPU copy into an owned reusable texture before advancing decode; immutable latch metadata is consumed by Task 2. Python `analyze_parity(manifest: dict, records: list[dict]) -> dict` requires all stage/copy/transform and annotated-person checks, outputs separate `injected_texture_pass` and `video_route_pass`.

- [ ] RED tests reject advanced texture with old frame index, duplicate source, wrong video hash, missing route, nearby-frame reference, zero candidates with annotated person and normalized pass/raw-output fail. GPU-fence completion, not frameReady callback alone, makes a latch available; test replacement while latched.
- [ ] Run Python unittest discovery for `test_r4_parity_analysis.py` and Unity latch tests for RED, then implement bounded frame-ready/render-timeline latch. Pause/step playback as needed; it is not a throughput benchmark.
- [ ] Compare exact latched source versus copied/imported/transformed contents using device-local GPU summaries; never treat separately decoded pixels as byte-identical. Verify detector boxes/pose for actual annotated people and exclude the old live-source freshness bypass from overlay claims.
- [ ] Run both user videos, all R4.1 injected fixtures and inherited golden tolerances. Archive original failure, first divergence, fix and exact source/model/build hashes. `analyze_parity` must return both passes with no missing evidence.
- [ ] Review and commit `test(android): verify latched video GPU skeleton inputs`. Only now open R4.2.

### Task 4: Measure Camera2/Vulkan capability before camera implementation

**Files:** Create capability files in map; modify `runtime/gpu/android/unity_vulkan_plugin.cpp`, `tools/test/TopDownEvalBuild.cs` and builder; create `docs/validation/ANDROID_R4_CAMERA_GATE.md`.

**Interfaces:** `ProbeCameraCapabilities(const CameraProbeRequest&, CameraCapabilities&, std::string&) -> bool`. Request defines camera ID, 30-FPS target and candidate sizes. Result records timestamp source, actual output format/usage/external format, sampled features, YCbCr model/range/chroma requirements, semaphore/fence and external/foreign ownership support. `ValidateCameraCapabilities(const CameraCapabilities&) -> CameraCapabilityDecision` returns explicit reason codes. No hardcoded RGBA assumption.

- [ ] Add `R4CameraCapabilities` RED tests for unsupported sampled PRIVATE, UNKNOWN clock, missing YCbCr feature, wrong GPU UUID, unavailable fence/ownership and absent 30-FPS stream; each rejects with named requirement. Cover supported RGBA and supported external-format sampling independently.
- [ ] Implement bounded native camera probe inside integrated eval APK via new `-R4CameraProbe` switch, with permissions and clear failure status. Negotiate supported required Vulkan features during Unity device creation; do not attempt to enable features on an existing VkDevice.
- [ ] Build API26/ARM64, verify ELF and inspect actual Snapdragon 888 front-camera buffers/clock/stream choices. Persist capability JSON and APK/device hashes; select and record one actual supported contract. Temporary probe resources must drain and release.
- [ ] If format/sampling/clock/ownership cannot satisfy R4, stop here with the report; no CPU/GLES/direct-ncnn substitute. Otherwise GREEN/review and commit `feat(android): validate Camera2 GPU source capabilities`.

### Task 5: Implement bounded exact image/metadata pairing and clock provenance

**Files:** Create frame-contract files in map; modify `runtime/common/pipeline_diagnostics.h`; test `tests/runtime/test_camera_frame_contract.cpp`.

**Interfaces:** `CameraFrameKey { uint64_t session; std::string camera_id; int64_t sensor_ns; }` (camera ID stored/interned at session setup, no hot-path string allocations); `CameraImageToken` is a move-only opaque lease; `CameraCaptureMetadata` carries key, frame number and domain; `CameraPair` owns token+metadata. `CameraPairer::PushImage(CameraImageToken, const CameraFrameKey&, int64_t arrival_boot_ns)`, `PushMetadata(const CameraCaptureMetadata&)`, `TryTake(CameraPair&) -> bool`, `Expire(int64_t now_boot_ns)`, `Reset(uint64_t session)` use fixed-capacity storage. A release callback owns rejected leases. `ComputeSensorAgeNs(sensor_ns, publication_boot_ns, domain, int64_t& age) -> bool` rejects non-REALTIME/negative/regressing samples.

- [ ] `R4CameraFrame` RED tests: metadata before/after image pairs exactly; other session/lens same timestamp cannot pair; nearest timestamp does not pair; after 100 ms expires once; ninth metadata entry cannot grow storage; fourth image prevents further admission; reset releases all unmatched leases exactly once. Assert UNKNOWN/negative ages never become verified and pause/resume invalidates old pairs.
- [ ] Run RED; implement fixed arrays, interned camera/session identity and immutable source-ID assignment on accepted pairs. Keep ns until reporting and use BOOTTIME-compatible publication clock for REALTIME.
- [ ] Run GREEN/native ABI/diagnostics regressions. Pairing is metadata-only and does not copy pixels. No clock offset fitted to callback arrivals.
- [ ] Review and commit `feat(android): pair camera images with exact sensor timestamps`.

### Task 6: Implement camera leases, cached Vulkan imports and RGBA producer

**Files:** Create native lifecycle/header/composition files in map plus `tests/runtime/test_camera_source.cpp`, `test_camera_vulkan_import.cpp`; modify existing Unity Vulkan plugin hooks and CMake Android links (`camera2ndk`, `mediandk`, `android` as actually needed).

**Interfaces:** `CameraSource::Open(const CameraCapabilities&, uint64_t session, std::string&) -> bool`, `TryAcquire(CameraPair&) -> bool`, `StopAdmission()`, `Drain()`. `CameraVulkanImport::Prepare(const CameraPair&, ProducerToken&, std::string&) -> bool`, `RecordCopy(ProducerToken, void* unity_texture) -> bool`, `PollCompletions()`, `RetireBuffer(uintptr_t identity)`, `Drain()`. `ProducerToken` has generation/slot/source IDs and owns no raw borrowed image beyond a tracked lease.

Add `HV_AndroidCameraApiV1` behind `HV_GetAndroidCameraApi(uint32_t version)`; size/version-prefixed config/frame/status records and function pointers `open`, `prepare`, `get_status`, `stop`, `destroy`. `prepare(handle, unity_texture, frame_out, event_data_out)` reserves work and returns nonblocking; the Unity render event records copies. Status includes producer texture retention/quarantine. Reuse existing HV result/error types. Define all new layouts in the new header and managed mirrors in Task 7; do not extend old V1 structs in place.

- [ ] Write `R4CameraSource`/`R4CameraImport` RED dispatch tests proving maxImages6/held≤4, acquire-only held<4, exactly-once fd/image release on every failure/drop, no render-thread waits, three producer textures and eight imports maximum. Buffer removal during a read retires without destruction until completion; pointer reuse never aliases a retired retained buffer.
- [ ] Implement NDK Camera2 capture callback + ImageReader acquisition feeding Task 5. Keep acquire fence until GPU wait import; handle fd=-1 as already ready per API contract. Validate ownership queue/layout against actual capability data, including FOREIGN when required. Return camera images using valid release-fence handoff or worker-confirmed GPU completion only.
- [ ] Implement cached sampling/YCbCr conversion into Unity-owned reusable RGBA textures. Register actual Unity native texture handles, query valid Vulkan access via Unity plugin APIs, and record on the supported render timeline; do not pass a raw VkImage where Unity expects a texture object. Use existing bridge copy after RGBA completion. Producer reuse waits for preview AND bridge-copy completion; camera lease waits only for its own reads/release. ncnn bridge-slot release remains independent.
- [ ] On buffer replacement retire/drain and rewarm generation; no per-frame pipeline/image create. Enforce cache bound. On unknown GPU completion quarantine retained resources and fail session; never destroy in-use images to satisfy shutdown timeout. Test permission loss, camera switch, pause/rotation, device loss, copy/import failures and consumer backlog.
- [ ] Run native GREEN, A/B synchronization regressions, Android build/ELF and on-device camera→RGBA→AHB stage probe with no inference required. Record allocation/fd/lease counts before/after warm-up and lifecycle loops.
- [ ] Review and commit `feat(android): bridge Camera2 GPU images through cached Vulkan resources`.

### Task 7: Integrate SDK Runtime Mode, Unity preview and provenance

**Files:** Create `U/Runtime/Android/HumanVisionAndroidCameraSource.cs` and mirrors; modify `U/Runtime/Android/HumanVisionAndroidGpuFrameBridge.cs`, `U/Demo/Live/{HumanVisionLiveSource.cs,HumanVisionCameraManager.cs}`, `U/Editor/HumanVisionAndroidRuntimeSettings.cs`; create `U/Tests/EditMode/HumanVisionCamera2RoutingTests.cs`; modify `profiles/android-ncnn-vulkan.json`, `runtime/composition/android_gpu_c.cpp` and additive provenance bindings if needed.

**Interfaces:** `HumanVisionAndroidCameraSource.Open(settings)`, `TryPrepareFrame(out CameraGpuFrame frame)`, `Close()` operate on Unity main thread; `CameraGpuFrame` identifies one texture slot/source/session/sensor timestamp/domain and a completion token. `HumanVisionLiveSource` selects it only for Android NCNN camera input. Preserve existing public API and video source selection. New metadata crosses through additive size/version records defined in Task 6; old submission layout remains valid.

- [ ] RED EditMode tests: NCNN camera route cannot instantiate WebCamTexture/AsyncGPUReadback; explicit ORT routing stays unchanged; permission/capability failure exposes actionable status; mirror/orientation does not change anatomical left/right; HasRecentFrame advances only on the actual source. ABI tests compare old layout sizes and offsets.
- [ ] Bind native camera API, create/register exactly three RGBA RenderTextures, issue native render events, and expose preview only after its copy ordering is established. Carry immutable pair metadata through `HV_RuntimePrepareAndroidGpuFrame` via an additive entrypoint/version, never patch old timestamps by arrival time.
- [ ] Add build validation for camera symbols/libraries/profile capability declarations and matching Demo/UPM files. Update profile/hash manifests through existing generation tooling. Do not mutate user ORT settings or enable automatic fallback.
- [ ] Run GREEN/native/Unity checks. Backup and sync reviewed files to `E:/UnityProject/Human-Vision-SDK-Test`; refresh and inspect Console. Build/install the integrated CameraDemo and verify front camera portrait/landscape, start/stop and pause/resume with exact paired IDs, verified sensor ages, bounded resources and no readback. This is R4.2's device exit; lack of a valid sensor clock is FAIL.
- [ ] Review and commit `feat(unity): use timestamped Camera2 source for Android ncnn`.

### Task 8: Prove rendered skeleton and fail-closed acceptance evidence

**Files:** Modify `U/Demo/Live/HumanVisionSkeletonOverlayer.cs` (instrumentation only, no renderer redesign), `tools/test/TopDownEvalProbe.cs`, `tools/test/android_topdown_gate_analysis.py`, `tools/test/test_android_topdown_gate_analysis.py`, collector and camera/parity reports. Preserve mirrors and thickness defaults.

**Interfaces:** Add an internal immutable `PresentationEvidence` record: source/result IDs, body IDs, valid joint count, emitted primitive counts, visibility/culling decision, presentation timestamp and preview source/transform. Eval probe reads the record; it cannot manufacture renderer evidence from BodyCount. Analyzer retains `analyze`'s existing entrypoint and requires separate independently viewable evidence references.

- [ ] RED tests: enabled-but-empty mesh, off-screen/culled output, stale result/current image, wrong transform, no independent visual evidence, changed source/profile/PID, missing sensor domain, and zero Bodies with annotated person all fail. Valid empty scene never satisfies one-person recall.
- [ ] Add lightweight publication/presentation instrumentation only; maintain 24/72 defaults. Record source age and publication-to-presentation separately. Keep parity shader probes disabled in timed artifacts and hash all switches into evidence.
- [ ] Run `pwsh -NoProfile -File tools/test/test_android_topdown_gate_analysis.ps1` and managed/native tests GREEN. Capture actual on-device aligned moving skeleton and bind screenshot/video evidence to process/source timeline. Do not infer alignment from counters alone.
- [ ] Review and commit `test(android): verify actual skeleton presentation and sensor age`.

### Task 9: Run bounded integrated cadence sweep with valid Bodies

**Files:** Modify `tools/test/collect_android_topdown_gate.ps1`, `summarize_android_topdown_gate.py`, report; only evidence-required fixes to `runtime/plugins/pipeline/simcc/{detector_cadence.cpp,topdown_gpu_pipeline.cpp}` and existing bridge/preprocess modules with matching tests.

**Interfaces:** Extend collector with `-Phase ShortWindow` and unique `-RunLabel`; preserve existing Interval/Capacity/Serial arguments. Evidence includes paired source clocks, actual current Bodies, stage costs, detector/pose admission/drop counters and declared interval. Output `timing_eligible` is provisional and not final PASS.

- [ ] RED analyzer tests reject body-summed/repeated FPS, missing people, >200ms detector gap, >500ms anchor, stale/new-track detection and diagnostic-probe-enabled timing. Assert acceptance values at exact boundaries (29/29.5, 75/100/250ms, 1%).
- [ ] Build the first real-person candidate using `pwsh -NoProfile -File tools/test/build_android_topdown_eval.ps1 -Interval 2 -Capacity 1`; collect `-Interval 2 -Capacity 1 -DurationSeconds 65 -VisiblePersonCount 1 -Phase ShortWindow -RunLabel <unique>`. Confirm user is available/in frame before measuring. Iterate 2→6 only while each is a meaningful eligible correctness candidate; stop early on prerequisite failure.
- [ ] Identify measured stage bottlenecks with GPU/CPU queue traces. For a correctable cost inside R4, create regression RED, implement minimal fix, then GREEN/review before remeasurement. No extra detector search, ORT, predictor or ignored person. Do not spin indefinitely: one five-interval sweep, at most one evidence-backed repair cycle and rerun; further architectural work requires a new decision.
- [ ] Select shortest interval passing unchanged short-window metrics with actual Bodies. If none passes, archive FAIL and stop without Task 10. Otherwise verify capacity2 short window with the same policy; an interval passing capacity1 alone is not full acceptance.
- [ ] Review and commit `test(android): qualify integrated TopDown cadence with live bodies` with exact outcomes, never a fabricated passing profile.

### Task 10: Complete physical and thermal gate and deliver reviewable artifacts

**Files:** Modify collector/analyzer tests and `docs/validation/ANDROID_R4_CAMERA_GATE.md`, `ANDROID_NCNN_TOPDOWN_GATE.md`, `docs/DEVELOPMENT_STATUS.md`; no runtime changes unless returned to an earlier task/review.

**Interfaces:** Collector `-Phase Acceptance` records five-second warm-up then 60-second window; `-Phase Thermal -SkipInstall` records 900 measured seconds in the same artifact/process lineage. Evidence has exact phase start/end/source/clock/hashes, not an inferred duration from file modification time. Existing strict analyzer identity checks remain.

- [ ] RED collector/analyzer fixtures prove thermal missing sample intervals, changed PID/profile/backend, monotonic-age growth, vanished Bodies and missing user visual evidence cannot pass. Fix collection/analysis without weakening rules.
- [ ] Collect full one-person and two-person windows after warm-up, including entry/exit, fast motion, occlusion, crossing and orientation. Ask for human cooperation only when needed; supplied videos supplement correctness, not live camera/age acceptance. Keep annotated transitions distinct and bounded, not a way to discard ordinary failures.
- [ ] After short-window eligibility, collect 15-minute thermal evidence on each capacity artifact being claimed accepted, maintaining same APK/device/PID/log identity for its associated run. No reinstall/restart during a thermal lineage. Record all 30-FPS/age/drop/body/backend criteria throughout; probe instrumentation stays off.
- [ ] Run final native/Android/Unity/architecture/ABI/package consistency checks once on final changes. Independent whole-branch review; publish status with measured PASS/FAIL, exact APK path/hash, commit, Unity settings and reproduction steps. Documentation-only final commit may follow the measured code SHA; explicitly distinguish them. Do not label an unmeasured runtime change as tested.
- [ ] Commit `docs(android): record Revision 4 physical acceptance evidence`. Keep main/Release gated until the user confirms final physical acceptance. A failed/missing hardware gate remains open; no RTMO work starts automatically.

## Spec coverage and execution handoff

| R4 section | Tasks |
|---|---|
| 1–4 outcome, evidence, preserved architecture/rulings | Global constraints and all task reviews |
| 5 fixture parity, raw model parity, production video route | 1–3 |
| 6 capability/Camera2/pairing/clock/sync/cache/lifecycle | 4–7 |
| 7 actual skeleton, bounded sweep, physical/thermal | 8–10 |
| 8 tests, independent commits/reviews, open Unity project | Common verification and each task |

Self-review: stage order, buffer bounds, clock domains, route evidence, original
ABI preservation and review-focus tests are mapped above. Adaptive bug-fix files
are limited to a demonstrated first divergence; file mapping is not authority to
rewrite unrelated modules. No tests or device gates were run by writing this plan.

The user has already selected Subagent-driven execution; retain that method.
Review/approval of this written plan is the remaining pre-implementation gate.
After approval begin Task 1 and proceed automatically within the stated stop
conditions; do not ask the user to choose the execution method again.
