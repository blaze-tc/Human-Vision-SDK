# Android ncnn TopDown Cadence Revision 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make live Android camera skeletons follow people steadily at 30 fresh complete observation frames/s on the authorized Snapdragon 888.

**Architecture:** Reuse the accepted Unity Vulkan → three-slot AHB → ncnn Vulkan bridge. Run RTMPose on each accepted current frame; run the golden-validated RTMDet detector on bounded keyframes using a detached, reusable GPU input. Never reuse old joints to make a result fresh.

**Tech Stack:** Unity 2021.3.45f1 / IL2CPP, Android API 26 ARM64, Vulkan, AHardwareBuffer and sync-fd, pinned Tencent ncnn FP16, C++17, versioned C plugin ABI, Python model/golden tools.

**Spec:** `docs/superpowers/specs/2026-09-25-android-ncnn-detector-cadence-revision-3-proposal.md` (user approved 2026-09-25); Revision 2 remains controlling outside its narrowly superseded §9.1, §10 and §13.2 clauses.

## Global Constraints

- This plan replaces only Milestone C Tasks C2–C6 in `2026-09-13-android-vulkan-ncnn-production-runtime.md`. Milestones A/B and C1 are complete; execute the tasks below in order, then Milestone D only after the TopDown device gate passes.
- The user chose Subagent-driven execution. Each task gets a fresh GPT-6 Sol **medium** implementer, independent SPEC and QUALITY review, any needed fix/re-review, one separate verified commit, and an updated status. No task advances on a failed review.
- Preserve the V1 C ABI, existing V2 GPU tables/query, Unity skeleton API, Canonical Skeleton, Region index and Tracker ID semantics. An additive V3 C plugin query/table is permitted only to carry the detached GPU-input contract; V1/V2 callers retain their layouts and behavior.
- `NCNN Vulkan` remains the explicit Android default and fails fast. ORT modes remain explicitly selected compatibility modes; never route an ncnn error into ORT.
- No full-frame CPU readback, CPU stand-in for the bridge, per-frame AHB/import creation, silent fallback, long skeleton prediction, or duplicate-result FPS inflation. Small detector/SimCC outputs may cross to CPU for decode.
- Use the same VkPhysicalDevice identity and the existing measured blit/color-attachment selection, slot lifecycle, sync-fd ownership, and physical-device UUID checks. A Unity render callback must never wait for inference.
- Detector capture interval: 2–6 accepted pose frames and at most 200 ms source capture time between attempts. New-track detector output must arrive within 200 ms of capture. A pose-supported track expires after 500 ms without successful detection or two missed detector matches. Rejected current poses never publish old joints.
- Keep target 30 fresh **complete observation frames/s**, with Revision 2 clock tolerance: ≥29.0 in every 10-second window and ≥29.5 over 60 seconds, age P50 ≤75 ms, P95 ≤100 ms, no ordinary sample >250 ms, combined bridge drops ≤1%, and the unchanged correctness/orientation/identity/thermal gates. A 1-, 4-, or 8-body frame counts once.
- This plan authorizes one provisional RTMDet Nano local evaluation ModelPack after its strict Vulkan/golden checks. No fifth detector search, public trained-weight redistribution, main merge, Release, RTMO, Hand, QNN, MediaPipe, Windows, renderer, or ORT optimization is authorized here.
- Every task starts with a meaningful RED test, runs focused and affected full checks, updates `docs/DEVELOPMENT_STATUS.md` with exact commands/results, and commits only verified files. Keep raw model/device artifacts ignored under `out/`; record SHA-256 and reproduction commands in validation docs.

## Review Focus

The spec's high-risk user inputs and failures are pinned to these owning tasks:

1. **Camera rotation or source restart during a detached detector job:** Task 4 rejects the stale generation and drains the prepared token without releasing an AHB twice.
2. **Person enters while detector is busy:** Task 7 counts missed 200 ms capture deadlines and Task 10 measures discovery latency; a zero-body frame does not hide the missing person.
3. **Pose fails or hallucinates joints behind an occluder:** Task 6 removes that body from publication immediately and Task 7 forbids republishing its previous joints.
4. **Detector output arrives after a newer pose:** Task 6 matches the detector-time anchor without rewinding the crop or changing an already published snapshot.
5. **Region edit or crossing between detector keyframes:** Task 8 discards old-revision results and asserts stable region-index/track-ID separation.

## File and interface map

| Unit | Files | Responsibility |
| --- | --- | --- |
| Detector eligibility | `tools/models/ncnn/finalize_rtmdet_eval.py`, `runtime/plugins/backend/ncnn/ncnn_android_session.cpp`, `tests/reference/test_rtmdet_eval.py`, `tests/runtime/test_ncnn_input_contract.cpp` | Reproduce audited RTMDet files and real golden, bind 3-channel FP16 pack1, emit local-only detector assets. |
| Pose eligibility | `tools/models/ncnn/export_rtmpose.py`, `tools/models/ncnn/compare_pose_outputs.py`, `tools/models/ncnn/build_local_eval_pack.py`, `tests/reference/test_rtmpose_ncnn_golden.py`, `tests/runtime/test_ncnn_modelpack.cpp` | Produce hashed schema-2 local evaluation pack with detector and RTMPose Body26. |
| Additive GPU ABI | `runtime/include/humanvision_plugin_v3.h`, `runtime/host/backend_factory.{h,cpp}`, `tests/runtime/test_plugin_abi_v3.cpp` | Version-3 prepared-input and GPU-pipeline query while V1/V2 byte layouts stay unchanged. |
| Prepared GPU input | `runtime/plugins/backend/ncnn/ncnn_prepared_input.{h,cpp}`, `ncnn_android_session.{h,cpp}`, `ncnn_vulkan_backend.cpp`, `tests/runtime/test_ncnn_prepared_input.cpp` | Cache one detached detector `VkMat` and prove copy/lease/reuse/retirement. |
| GPU runtime host | `runtime/host/gpu_runtime_host.{h,cpp}`, `runtime/host/profile_manager.{h,cpp}`, `runtime/composition/{session.h,session.cpp,android_gpu_c.cpp}`, `runtime/gpu/android/unity_vulkan_plugin.{h,cpp}`, `tests/runtime/test_runtime_gpu_composition.cpp` | Resolve strict V3 profile, claim GPU slots, run V3 pipeline, publish atomic snapshots, preserve ORT V1 route. |
| Track crop policy | `runtime/plugins/pipeline/simcc/gpu_track_crops.{h,cpp}`, `native/src/tracking/center_iou_tracker.{h,cpp}`, `tests/runtime/test_gpu_track_crops.cpp` | Current-joint crop updates, bounded detector age, delayed-result association, no old-joint publication. |
| Cadence and TopDown | `runtime/plugins/pipeline/simcc/detector_cadence.{h,cpp}`, `topdown_gpu_pipeline.{h,cpp}`, `simcc_pipeline.cpp`, `tests/runtime/test_topdown_gpu_pipeline.cpp` | One prepared detector job, pose per accepted current frame, scheduling and complete observation. |
| Region | `runtime/composition/region_assignment.{h,cpp}`, `session.cpp`, `tests/runtime/test_region_assignment.cpp` | Pelvis-first post-inference assignment, collision and revision rules. |
| Diagnostics | `native/include/humanvision/humanvision_v2.h`, `native/src/core/stats_collector.{h,cpp}`, `runtime/common/pipeline_diagnostics.h`, `unity/HumanVisionDemo/Assets/HumanVision/{Runtime/RuntimeBindings.cs,Runtime/HumanVisionManager.cs,Demo/HumanVisionHud.cs,Tests/EditMode/HumanVisionDiagnosticsTests.cs}` | Add versioned capture-to-publication, detector, pose, drop, provenance and FPS metrics. |
| Device gate | `tools/test/build_android_topdown_eval.ps1`, `collect_android_topdown_gate.ps1`, `docs/validation/ANDROID_NCNN_TOPDOWN_GATE.md` | Build integrated APK, inspect hashes/libraries, collect 1/2-person and thermal evidence. |

Existing host `HV_GpuBackendApiV1::run_image` and `HV_GpuPipelineApiV1::process_gpu` synchronously borrow the AHB slot. Task 3 adds **separate V3 query functions**, leaving their tables byte-identical. The proposed V3-only prepared extension is:

```c
#define HV_PLUGIN_API_V3 3u
#define HV_GPU_PREPARED_API_V1 1u
#define HV_GPU_PIPELINE_API_V2 2u
typedef struct HV_GpuPreparedRefV1 {
    uint32_t struct_size, api_version;
    uint64_t token, generation;
    int64_t frame_id, timestamp_us;
    uint32_t flags, reserved;
} HV_GpuPreparedRefV1;
typedef struct HV_GpuPreparedApiV1 {
    uint32_t struct_size, api_version;
    HV_Result (HV_CALL *prepare_image)(void*, const HV_GpuFrameRefV1*,
        const HV_GpuImageTransformV1*, HV_GpuPreparedRefV1*, HV_ErrorBufferV1*);
    HV_Result (HV_CALL *run_prepared)(void*, const HV_GpuPreparedRefV1*,
        HV_TensorViewV1*, uint32_t, uint32_t*, HV_ErrorBufferV1*);
    HV_Result (HV_CALL *discard_prepared)(void*, const HV_GpuPreparedRefV1*,
        HV_ErrorBufferV1*);
} HV_GpuPreparedApiV1;
typedef struct HV_GpuBackendApiV2 {
    HV_GpuBackendApiV1 v1;
    const HV_GpuPreparedApiV1* prepared;
} HV_GpuBackendApiV2;
typedef struct HV_HostServicesV3 {
    HV_HostServicesV2 v2;
    HV_Result (HV_CALL *create_gpu_backend_v3)(void*,
        const HV_GpuBackendConfigV1*, const HV_GpuDeviceContextV1*,
        const HV_GpuBackendApiV2**, void**, HV_ErrorBufferV1*);
    void (HV_CALL *release_gpu_backend_v3)(void*,
        const HV_GpuBackendApiV2*, void*);
} HV_HostServicesV3;
typedef struct HV_GpuPipelineApiV2 {
    uint32_t struct_size, api_version;
    HV_Result (HV_CALL *create)(const HV_PipelineConfigV1*,
        const HV_HostServicesV3*, void**, HV_ErrorBufferV1*);
    void (HV_CALL *destroy)(void*);
    HV_Result (HV_CALL *process_gpu)(void*, const HV_GpuFrameRefV1*,
        HV_ObservationFrameV1*, HV_ErrorBufferV1*);
} HV_GpuPipelineApiV2;
typedef struct HV_PluginApiV3 {
    HV_PluginApiV1 v1;
    const HV_GpuBackendApiV2* gpu_backend;
    const HV_GpuPipelineApiV2* gpu_pipeline;
} HV_PluginApiV3;
```

`prepare_image` borrows the observation AHB only until its GPU copy is proven complete; it does **not** retire the observation. The pipeline's subsequent current-frame pose roles finish before the existing final role release/slot retirement. `run_prepared` owns only the cached small GPU tensor and can execute after AHB retirement. Its output views remain backend-owned until the next run/discard/destroy, as in V2. Tokens are one-shot, generation-bound, and explicitly discarded if never run. The V3 GPU pipeline receives an `HV_HostServicesV3` with `create_gpu_backend_v3`; its `process_gpu` still returns exactly one `HV_ObservationFrameV1` for the current source frame. Task 3 freezes exact table sizes/offsets before Task 4 uses them.

## Task 1 — C2-R: make RTMDet a strict local evaluation detector

**Files:** Create `tools/models/ncnn/finalize_rtmdet_eval.py`, `tests/reference/test_rtmdet_eval.py`; modify `runtime/plugins/backend/ncnn/ncnn_android_session.cpp`, `tests/runtime/test_ncnn_input_contract.cpp`, `docs/validation/RTMDET_NCNN_CONVERSION_GATE.md`, `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** Consume C1 pinned source/ONNX/ncnn hashes and existing `InputContract`. Produce ignored `out/c2-local-detector/{model.param,model.bin,model.json}` with `local_evaluation_only=true`, exact SHA-256, `RGB 320×320`, FP16 pack1, `cls`/`bbox`, and the existing 0.35 person threshold.

- [ ] Add RED tests: changing one of the six static `Reshape` dimensions, `Input` dimensions, checkpoint SHA, blob name, or FP16 pack1 declaration fails `finalize_rtmdet_eval`; a 3-channel input reaches the ncnn extractor as `c=3, elempack=1, elembits=16`, not padded pack4.
- [ ] Run `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_rtmdet_eval.py -v` and `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter NcnnDetectorPack1Input`; record the expected failures.
- [ ] Implement a deterministic param-correction function that checks the raw 316-layer graph and replaces only the audited `Input` and six `Reshape` dimension fields. Reject any unexpected original line or output hash. Implement the detector-role pack1 conversion branch without altering Body26 pack4.

```python
def corrected_rtmdet_param(raw_text: str) -> str:
    lines = raw_text.splitlines()
    dimensions = {'/Reshape': 1600, '/Reshape_1': 400,
                  '/Reshape_2': 100, '/Reshape_3': 1600,
                  '/Reshape_4': 400, '/Reshape_5': 100}
    seen = set()
    for index, line in enumerate(lines):
        fields = line.split()
        if fields[:2] == ['Input', 'in0']:
            assert fields == ['Input', 'in0', '0', '1', 'in0']
            lines[index] = line + ' 0=320 1=320 2=3'
            seen.add('in0')
        elif fields and fields[0] == 'Reshape':
            name = fields[1]
            assert name in dimensions and line.endswith('1=-1')
            lines[index] = line[:-4] + f'1={dimensions[name]}'
            seen.add(name)
    assert seen == set(dimensions) | {'in0'}
    return '\n'.join(lines) + '\n'
```

- [ ] Regenerate the ignored detector files from the pinned checkpoint, run C1 graph audit, four-image 1/1/2/0 person golden, no-CPU-fallback device audit, and `diagnose_c2_failure`. Keep the previous >33.33 ms every-frame P95 as a documented failure; this task passes model **eligibility**, not integrated performance or weight redistribution.
- [ ] Run focused and full reference tests, full native tests, Android ARM64/API26 build, architecture guard, and `git diff --check`; record commands, hashes, and results. If any correctness/Vulkan eligibility gate fails, stop before Task 2.
- [ ] Obtain independent SPEC and QUALITY approvals; commit only scripts/tests/input-contract/doc changes with `git commit -m "feat(models): qualify local RTMDet Vulkan detector"`.

## Task 2 — C3-R: convert RTMPose and compose the local schema-2 ModelPack

**Files:** Create `tools/models/ncnn/build_local_eval_pack.py`, `tests/reference/test_rtmpose_ncnn_golden.py`, `tests/runtime/test_ncnn_modelpack.cpp`; modify `tools/models/ncnn/export_rtmpose.py`, `compare_pose_outputs.py`, `tests/native/CMakeLists.txt`, `docs/MODEL_MANIFEST.md`, `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** Consume Task 1's hashed detector files. Produce ignored `out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16/{modelpack.json,detector/*,body/*}` and an unchanged profile ID `android-ncnn-vulkan`; all model paths remain confined to the pack root.

- [ ] Add RED golden tests for official RTMPose-t Body26: full body, clipped person, mirrored and rotated image, exact 192×256 source crop and inverse affine, `simcc_x/y` shape/name, invalid-joint mask, normalized joint distance P95 ≤0.01 bbox diagonal/max ≤0.03, confidence P95 error ≤0.02; a changed checkpoint/param/bin hash or outside-root path fails.

```python
def test_pose_golden_contract(reference, ncnn_pose):
    assert ncnn_pose.valid_mask == reference.valid_mask
    assert ncnn_pose.normalized_joint_distance_p95(reference) <= .01
    assert ncnn_pose.normalized_joint_distance_max(reference) <= .03
    assert ncnn_pose.confidence_error_p95(reference) <= .02
```

- [ ] Run `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_rtmpose_ncnn_golden.py -v` and `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter NcnnModelPack`; record RED.
- [ ] Convert with pinned ncnn tools, explicit FP16 pack4 Body26 input, deterministic named outputs, source/model/tool SHA-256 and per-file license/provenance. The builder emits real schema-2 hashes; `local_evaluation_only=true` prevents it from being mistaken for a distributable pack.
- [ ] Run C1 pose golden, model-pack native tests, default reference suite, full native, Android build and architecture guard. Reject CPU fallback or any model path/hash mismatch. Keep trained weights in ignored `out/`; the later public release gate still requires redistribution rights.
- [ ] Independent SPEC/QUALITY review and separate commit `feat(models): build local ncnn RTMPose TopDown pack`.

## Task 3 — freeze the additive V3 prepared-input contract

**Files:** Create `runtime/include/humanvision_plugin_v3.h`, `tests/runtime/test_plugin_abi_v3.cpp`; modify `runtime/host/backend_factory.{h,cpp}`, `tests/native/CMakeLists.txt`, `docs/SDK_API.md`, `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** Produce separate `HV_QueryPluginV3`, `HV_GpuBackendApiV2` (V1 prefix plus `prepared` table), `HV_GpuPipelineApiV2`, `HV_HostServicesV3` (V2 prefix plus V3 create/release callbacks), and the `HV_GpuPreparedRefV1`/`HV_GpuPreparedApiV1` signatures above. Existing V1/V2 query results remain byte-identical.

- [ ] Write RED layout/callback tests: compile-time V1/V2 size/offset assertions remain unchanged; V3 rejects truncated tables, null callbacks, nonzero reserved fields, token 0, duplicate/stale token, bad generation and wrong provider; `RegisterV3` cannot silently pick ORT. Use a fake V3 backend/pipeline fixture.

```cpp
static_assert(sizeof(HV_GpuBackendApiV1) == 40);
TEST(GpuAbiV3, RejectsOldGeneration) {
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1, 7, 10, 88, 1000};
    EXPECT_EQ(fake_backend.RunPrepared(ref, 11), HV_ERR_INVALID_ARGUMENT);
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter GpuAbiV3` and record RED. Define V3 tables and independent query/registration; ensure a V2 caller never interprets the V3 header and V3 host services are passed only to the V3 pipeline `create` callback.
- [ ] Run focused/full native, V1/V2 ABI regression, Android ARM64/ELF export check, architecture guard; independent SPEC/QUALITY review; commit `feat(runtime): add versioned GPU prepared-input ABI`.

## Task 4 — detach one reusable detector input without breaking AHB ownership

**Files:** Create `runtime/plugins/backend/ncnn/ncnn_prepared_input.{h,cpp}`, `tests/runtime/test_ncnn_prepared_input.cpp`; modify `ncnn_android_session.{h,cpp}`, `ncnn_vulkan_backend.cpp`, Android CMake, `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** Implement Task 3's `prepare_image`/`run_prepared`/`discard_prepared` for the detector role only. A fixed one-job `PreparedSlot` owns cached `VkMat`, command/descriptor resources, and generation-bound token; pose `run_image` remains unchanged and uses its own session/role.

- [ ] RED fake-bridge tests prove: `prepare_image` copies only 320×320 FP16 on GPU, confirms completion before returning, leaves the AHB role open for pose, then permits final role release; `run_prepared` works after AHB retirement; second prepare while busy is rejected; stale/duplicate tokens and rotation/end-source discard safely; unprovable GPU completion quarantines rather than recycles.

```cpp
TEST(NcnnPreparedInput, InputOutlivesAhbWithoutCpuPixels) {
    auto ref = backend.PrepareImage(frame88, detectorTransform);
    EXPECT_EQ(backend.FullFrameCpuReadbacks(), 0u);
    backend.FinishPoseAndRetireAhb(frame88);
    EXPECT_TRUE(backend.RunPrepared(ref).contains("bbox"));
    EXPECT_EQ(backend.CreatedPreparedBuffers(), 1u);
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter NcnnPreparedInput` and record RED. Implement one-time buffer/pipeline allocation at session/generation start, command-buffer GPU copy and explicit FP16 pack1 conversion. After the copy completion proof, invoke the existing `CompleteGpuRole(false)` handoff so the pose backend can acquire the same observation; the pose role alone performs final ownership release and slot retirement. No `AHardwareBuffer_lock`, `AsyncGPUReadback`, per-frame import, or CPU input pixels.
- [ ] Validate with Android API26 ARM64 build and device-side source→prepared-tensor parity against the pinned golden input. Inspect actual AHB usage/features and sync-fd ownership on the device; no hard-coded transfer-destination assumption. Test source restart during a running prepared job; if GPU completion is unproven, stop this task.
- [ ] Full native/architecture checks, independent high-risk SPEC/QUALITY review, separate commit `feat(android): cache detached ncnn detector input`.

## Task 5 — compose and publish GPU observations in the runtime host

**Files:** Create `runtime/host/gpu_runtime_host.{h,cpp}`, `tests/runtime/test_runtime_gpu_composition.cpp`; modify `runtime/host/profile_manager.{h,cpp}`, `runtime/composition/{session.h,session.cpp,android_gpu_c.cpp}`, `runtime/gpu/android/unity_vulkan_plugin.{h,cpp}`, runtime CMake, `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** `GpuRuntimeHost::Start(V3Pipeline, HV_HostServicesV3, config, error)`, `Claim/Process/Publish`, `CopyLatest(frame,revision)`, `Stop()`. The production bridge exposes a read-only, lease-scoped `UnityVulkanProducerBridge()` and `UnityVulkanProducerContext()` to the worker; the `HV_ANDROID_GPU_GATE` helper retains its separate test role.

- [ ] RED tests with a fake V3 pipeline: NCNN profile selects only V3 GPU route, claims a real token/generation, publishes one full `HV_ObservationFrameV1` with original frame ID/time and all bodies atomically; ORT profiles still use V1 CPU route; a V3 creation/run error reaches `LastError` with no fallback; stop/restart drains each claimed slot exactly once.

```cpp
TEST(RuntimeGpuComposition, NoCpuFrameSubmission) {
    RuntimeSession runtime;
    ASSERT_TRUE(runtime.Start(localRoot, "android-ncnn-vulkan", 2, error));
    fakeBridge.PublishReady(88, 1000);
    EXPECT_EQ(fakeGpuPipeline.ProcessedFrameIds(), std::vector<int64_t>{88});
    EXPECT_EQ(fakeCpuFrameSubmissions, 0u);
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter RuntimeGpuComposition` for RED. Add a strict V3 GPU selection branch in `ProfileManager::Resolve` for `android-ncnn-vulkan`, using the existing profile ID, schema-2 ModelPack, single declared ncnn backend and `allow_fallback:false`; do not weaken the V1 resolver for ORT profiles. Route `HV_RuntimePrepareAndroidGpuFrame` only through the existing nonblocking render event; a native worker owns `ClaimConsumer` and V3 `process_gpu`. Keep the body snapshot and region-revision lock boundary in `RuntimeSession::Copy`.
- [ ] Full native, Android build, Unity EditMode route tests, V1/ORT regression, architecture and no-readback guards. Independent SPEC/QUALITY review; commit `feat(runtime): publish ncnn GPU observation snapshots`.

## Task 6 — bound current-joint crop updates and delayed detection association

**Files:** Create `runtime/plugins/pipeline/simcc/gpu_track_crops.{h,cpp}`, `tests/runtime/test_gpu_track_crops.cpp`; modify `native/src/tracking/center_iou_tracker.{h,cpp}` only where detector-time anchor matching needs a tested correction; update native CMake and `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** `DetectorResultMeta{frame_id,capture_us,arrival_us,region_revision,generation}` and `GpuTrackCrops::ApplyDetection(meta,boxes)`, `NextCrops(currentFrameId,currentTimeUs)`, `ApplyPose(currentFrameId,currentTimeUs,trackId,joints,validity)`, `PublishedJointSourceFrame(trackId)`, `InvalidateGeneration()`. Preserve `track_id` separately from region index; publishable bodies carry internal pose frame ID and detector anchor age.

- [ ] RED tests: current joints update next ROI; a detector result for frame 80 arriving after valid pose frame 85 cannot move crop backwards; first new track requires detector result age ≤200 ms; pose failure removes published body immediately; two detector misses or >500 ms since last matched detector expires track; fast motion, edge clipping, occlusion and crossing do not create duplicate IDs.

```cpp
TEST(GpuTrackCrops, DelayedDetectorNeverRewindsPose) {
    crops.ApplyPose(85, 85000, 7, jointsAtX300, true);
    crops.ApplyDetection({80, 80000, 86000, 7, 3}, boxesAtX250);
    EXPECT_GE(crops.NextCrops(86, 86000).front().center_x, 300.0f);
    EXPECT_EQ(crops.PublishedJointSourceFrame(7), 85);
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter GpuTrackCrops` for RED. Implement fixed-capacity (≤8) crop/anchor storage; derive ROI from current valid pose joint bounds with margin/clipping; use existing `CenterIouTracker` identity association but never its three-second prediction policy on the GPU route.
- [ ] Focused/full native and architecture checks; independent SPEC/QUALITY review; commit `feat(tracking): bound GPU pose crops and delayed detection`.

## Task 7 — schedule keyframe detection and run TopDown on each current image

**Files:** Create `runtime/plugins/pipeline/simcc/detector_cadence.{h,cpp}`, `topdown_gpu_pipeline.{h,cpp}`, `tests/runtime/test_topdown_gpu_pipeline.cpp`; modify `runtime/plugins/pipeline/simcc/simcc_pipeline.cpp`, `runtime/plugins/pipeline/simcc/component.json`, `runtime/host/profile_manager.{h,cpp}`, `profiles/android-ncnn-vulkan.json`, `tests/runtime/test_model_profiles.cpp`, runtime CMake and `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** `DetectorCadenceScheduler::ShouldCapture(frameId,captureUs,trigger)`, `AdmitPrepared(ref)`, `FinishDetector(ref,boxes)`, `CancelGeneration(generation)`; V3 pipeline `process_gpu` returns one atomic observation for its current frame. One background detector worker owns at most one prepared job, with pose priority at job-admission boundaries.

- [ ] RED deterministic fake-GPU tests for intervals 2–6 and 200 ms cap even under dropped frames; a busy detector that prevents a capture by the 200 ms deadline increments a scheduling-failure counter; no tracks/pose reject/edge/Region change cause earlier request; one detector busy + newer keyframe replaces only an unstarted job; a 40 ms nonpreemptible detector may cause a measurable pose miss, never a duplicated result; a frame with 2 people has two current-frame pose runs and one publication.

```cpp
TEST(TopDownGpu, PoseOnlyFrameIsFreshOnce) {
    pipeline.Process(frame90);                 // detector keyframe request
    pipeline.Process(frame91);                 // current-image pose only
    EXPECT_EQ(fakePose.FrameIds(), (std::vector<int64_t>{90, 91}));
    EXPECT_EQ(pipeline.FreshObservationCount(), 2u);
    EXPECT_EQ(pipeline.DetectorExecutions(), 1u);
    EXPECT_EQ(pipeline.Observation(91).source_frame_id, 91);
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter TopDownGpu` for RED. Add `detector.cadence_interval_frames` to the hashed `android-ncnn-vulkan` profile; reject values outside 2–6 and cap `detector.max_capture_gap_us` at 200000. Implement bounded scheduler; `prepare_image` only on admitted detector keyframes; run RTMPose crops on every claimed current AHB frame; decode only small raw outputs; update Task 6 crop state; publish complete current bodies only after all current poses finish. Detector results may initialize/update only future frames and cannot mutate published snapshots.
- [ ] Confirm the render callback never waits, the GPU worker uses latest-ready/drop, no per-frame heap allocations after warm-up, no old-joint carry-forward, correct 0/1/2-body observation counts, and provenance across rotation/restart. Run focused/full native, Unity route tests, Android native build, model golden and architecture checks.
- [ ] Independent SPEC/QUALITY review; commit `feat(pipeline): run current-frame pose with bounded detector cadence`.

## Task 8 — assign Regions after current-frame inference

**Files:** Create `runtime/composition/region_assignment.{h,cpp}`, `tests/runtime/test_region_assignment.cpp`; modify `runtime/composition/session.cpp`, native CMake and `docs/DEVELOPMENT_STATUS.md`.

**Interfaces:** `AssignedObservation AssignRegions(const HV_ObservationFrameV1&, const RegionSet&, int64_t revision, const TrackAnchors&)`; `AssignedObservation` holds `body_count`, unchanged body observations, `std::array<int32_t,8> region_indices`, and its region revision. `CanPublish(const AssignedObservation&, int64_t currentRevision)` rejects a mismatch. At most one current body occupies each region; track IDs remain separate.

- [ ] RED tests: pelvis inside one region while bbox center is outside; bbox-center fallback applies only when pelvis is invalid; overlapping regions prefer existing assigned track then higher pose confidence, detector confidence and source order; outside bodies are discarded; revision change rejects old detector/pose results; crossing does not swap `track_id` merely because region indices differ; GPU path never calls `MaskRegions`.

```cpp
TEST(RegionGpu, PelvisWinsAndOldRevisionCannotPublish) {
    auto result = AssignRegions(frame88, regionsAtRevision7, 7, anchors);
    EXPECT_EQ(result.body_count, 1u);
    EXPECT_EQ(result.region_indices[0], 1);
    EXPECT_FALSE(CanPublish(result, 8));
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter RegionGpu` for RED. Implement post-inference assignment before atomic snapshot publication, preserving the unmasked original image for detector and pose.
- [ ] Full native, Android build and architecture checks, independent SPEC/QUALITY review; commit `feat(runtime): assign GPU pose results to regions`.

## Task 9 — expose honest fresh-observation and detector-cadence diagnostics

**Files:** Modify `native/include/humanvision/humanvision_v2.h`, `native/src/core/stats_collector.{h,cpp}`, `runtime/common/pipeline_diagnostics.h`, `runtime/composition/session.cpp`, `unity/HumanVisionDemo/Assets/HumanVision/Runtime/{RuntimeBindings.cs,HumanVisionManager.cs}`, `unity/HumanVisionDemo/Assets/HumanVision/Demo/{HumanVisionHud.cs,Live/HumanVisionCameraManager.cs}`, `tests/native/test_stats_collector.cpp`, `docs/DEVELOPMENT_STATUS.md`; create `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionDiagnosticsTests.cs`.

**Interfaces:** Add a size/version-prefixed `HV_RuntimeStatsV2` query. Keep `HV_RuntimeStatsV1` unchanged. Internal `StatsCollectorV2::Publish(frameId,bodyCount,captureUs,publishedUs)` accepts a unique complete observation; `Sample(frameId)` cannot increase that counter. Expose separate `source_frames_seen`, `source_rate_limited_drops`, `gpu_bridge_*_drops`, `pose_job_drops`, detector attempted/completed/late/discarded, detector interval/age, fresh observation FPS, capture-to-publication P50/P95 and per-body pose time.

- [ ] RED tests: three snapshots with 1/4/8 bodies count as three fresh frames; sampling/rendering the same snapshot counts zero extra; pose-only frame with current joints counts once; stale detector or old-joint clone does not; invalid monotonic capture time is rejected; bounded rolling quantiles produce exact deterministic P50/P95.

```cpp
TEST(StatsV2, CountsCompleteFramesNotBodies) {
    stats.Publish(1, 1, 1000, 6000);
    stats.Publish(2, 4, 2000, 7000);
    stats.Publish(3, 8, 3000, 8000);
    stats.Sample(3);
    EXPECT_EQ(stats.FreshObservationFrames(), 3u);
}
```

- [ ] Run `pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter StatsV2` and Unity diagnostics EditMode tests for RED. Implement fixed-capacity rolling windows and additive interop/HUD fields labelled `Fresh observations/s`, `Age P50/P95`, `Detector interval/age`, `Pose P50/P95`, `GPU/pose drops`, `Copy path`, and `Actual backend`.
- [ ] Run full native/Unity suites, Android ABI/ELF and architecture checks; independent SPEC/QUALITY review; commit `feat(runtime): report fresh GPU skeleton observation metrics`.

## Task 10 — build and physically gate the integrated TopDown player

**Files:** Create `tools/test/build_android_topdown_eval.ps1`, `collect_android_topdown_gate.ps1`, analyzer tests and `docs/validation/ANDROID_NCNN_TOPDOWN_GATE.md`; modify `docs/DEVELOPMENT_STATUS.md`. Keep build products and model binaries under ignored `out/`.

**Interfaces:** Build an Android Development/IL2CPP/ARM64/API26/Vulkan `HumanVisionCameraDemo` APK from the local evaluation pack, without `HV_ANDROID_GPU_GATE`; emit APK/profile/model/native-library hashes and an analyzer JSON with per-interval/per-capacity metrics.

- [ ] RED analyzer fixtures reject reused observation IDs, body-summed FPS, >200 ms detector capture gap, >200 ms new-track detector age, missing/changed model hashes, ORT fallback, CPU readback, stale Region revision, >1% bridge drops, P95 age >100 ms, FPS below target, missing thermal segment, an all-empty scene where the test log marks a visible person, or valid native bodies that Unity never draws because presentation/source frame IDs exceed the overlay lag limit.
- [ ] Run `pwsh -NoProfile -File tools/test/test_android_topdown_gate_analysis.ps1` for RED. Implement build script that regenerates local pack from pinned assets, stages it in an ignored temporary Unity project copy, validates Vulkan/ARM64/API26 and APK library closure, and emits exact build commands/sha256. The script must not publish or commit trained binaries.
- [ ] Implement collector/analyzer around the integrated scene: capture source, pose, detector, bridge, backend, body correctness and thermal logs. For each interval 2–6, bake a separately hashed profile/APK (or an explicitly validated runtime setting logged with its hash); never adapt the interval invisibly. Sweep in ascending order under live camera with an annotated visible-person count; the shortest candidate that passes all provisional timing and body checks enters 5-second warm-up + 60-second 1-person and 2-person windows and a 15-minute thermal run. The user's movement/occlusion review is still required before selection is final. Do not use a benchmark-only app.
- [ ] Run full native/reference/Unity/model golden/architecture suites, Android native/Unity build, APK inspection and analyzer fixtures; record exact commands/results and hashes. On the authorized Snapdragon 888, collect automatic timing/bridge evidence via ADB, then ask the user only for the actual human entry/exit, movement, occlusion, crossing and portrait/landscape physical actions/evidence needed for final acceptance.
- [ ] PASS only if the spec's 30-fresh-observation target (Revision 2 clock tolerance), P50/P95 age, body recall, discovery ≤250 ms, track/Region stability, orientation, drops, no readback/fallback/crash, and thermal conditions all hold at capacities 1 and 2. If no interval passes, record FAIL and stop Milestone C; do not start RTMO or relax a metric. The user's physical acceptance closes this gate.
- [ ] Independent SPEC/QUALITY review of scripts, evidence and status; commit `test(android): gate integrated ncnn TopDown skeleton` only after the appropriate result is accurately documented. No main merge or Release before the user's final device acceptance.

## Plan self-review and handoff

- [x] Compare every Revision 3 requirement and retained Revision 2 invariant against Tasks 1–10; verify the five Review Focus cases each have a named RED test.
- [x] Scan for placeholders/undefined interfaces, C2→C3 dependency errors, V1/V2 layout drift, accidental RTMO/Hand/ORT scope, and any path that could count old joints as fresh.
- [x] Reconfirm all task commands and paths against the worktree, `git diff --check`, and architecture/docs guards. Record any Design-over-Plan ruling in the task ledger.
- [ ] Present this plan for user review. Their prior Subagent-driven selection is preserved; implementation resumes only after plan review, in this worktree, Task 1 first.
