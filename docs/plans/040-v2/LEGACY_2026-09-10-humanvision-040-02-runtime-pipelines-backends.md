# Human-Vision-SDK 0.4 Runtime Pipelines and Backends Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single hard-coded RTMDet640 + RTMPose133 path with replaceable pipeline/model adapters and explicit execution backends, including RTMO multi-person and RTMDet-nano + RTMPose Body26 precision modes.

**Architecture:** `HumanVisionEngine` schedules an `IPosePipeline` selected by semantic configuration. Pipelines depend on model adapters and `IInferenceBackend` instances resolved by registries. Every pipeline outputs the same internal canonical observation format before tracking/mapping.

**Tech Stack:** C++17, ONNX Runtime, DirectML, Android NNAPI, optional Qualcomm QNN/HTP, OpenMMLab ONNX models, CMake.

**Spec:** `docs/SDK_040_REALTIME_MULTIPERSON_DESIGN.md`

## Global Constraints

- RTMO/RTMPose types never cross C ABI.
- Android does not default to current 640 FP32 detector + 133 whole-body serial path.
- Backend selection reports requested and actual providers separately.
- Failure to initialize a preferred backend produces an explicit fallback reason.
- Latest-frame behavior and non-blocking Unity submission remain intact.
- QNN build must be optional at configure time so developers without authorized QAIRT SDK can still build CPU/NNAPI fallback; restricted Qualcomm binaries are not committed without redistribution permission.

---

### Task 1: Add backend registry and execution diagnostics

**Files:**
- Modify: `native/src/backend/i_inference_backend.h`
- Create: `native/src/backend/backend_registry.h`
- Create: `native/src/backend/backend_registry.cpp`
- Modify: `native/src/backend/onnx/onnx_runtime_backend.h`
- Modify: `native/src/backend/onnx/onnx_runtime_backend.cpp`
- Create: `tests/native/test_backend_registry.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/native/CMakeLists.txt`

**Interfaces:**
- `BackendRequest { preference, platform, model_role }`.
- `BackendSessionInfo { requested, actual, accelerated, fallback_reason }`.
- `IInferenceBackend::SessionInfo()` exposes actual provider after session creation.

- [ ] **Step 1: Write failing registry tests for Windows Auto, Android Auto, explicit CPU, unavailable QNN and provider fallback reason.**
- [ ] **Step 2: Split current `use_gpu` boolean into explicit backend preference internally while preserving compatibility constructor if needed.**
- [ ] **Step 3: Implement backend registry priority policies: Windows `DirectML -> CPU`; Android Snapdragon-capable build `QNN_HTP -> NNAPI -> CPU`; generic Android `NNAPI -> CPU`.**
- [ ] **Step 4: Ensure every session records actual provider and fallback reason.**
- [ ] **Step 5: Run `ctest -R backend_registry`.**
- [ ] **Step 6: Commit.**

### Task 2: Add common pose pipeline contract and move scheduling out of model classes

**Files:**
- Create: `native/src/pipeline/i_pose_pipeline.h`
- Create: `native/src/pipeline/pipeline_types.h`
- Create: `native/src/pipeline/pipeline_registry.h`
- Create: `native/src/pipeline/pipeline_registry.cpp`
- Modify: `native/src/core/humanvision_engine.h`
- Modify: `native/src/core/humanvision_engine.cpp`
- Create: `tests/native/test_pipeline_registry.cpp`

**Interfaces:**
- `PipelineInput { FrameBuffer frame, timestamp_us, max_bodies, regions }`.
- `BodyObservation { bbox, confidence, source_joints, source_schema, timestamp_us }` remains native/internal.
- `IPosePipeline::Process(const PipelineInput&, std::vector<BodyObservation>&, PipelineTimings&, std::string&)`.

- [ ] **Step 1: Add fake-pipeline tests proving engine scheduling works without RTMDet/RTMPose concrete classes.**
- [ ] **Step 2: Extract direct detector/pose construction and per-person loops from `HumanVisionEngine` into a temporary `LegacyWholeBodyPipeline` implementing `IPosePipeline`.**
- [ ] **Step 3: Make `HumanVisionEngine` depend only on `IPosePipeline`, tracker/canonical stages and frame scheduler.**
- [ ] **Step 4: Run all existing tests to verify no behavioral regression in Legacy mode.**
- [ ] **Step 5: Commit.**

### Task 3: Generalize image preprocessing and reusable tensor buffers

**Files:**
- Create: `native/src/preprocess/image_transform.h`
- Create: `native/src/preprocess/image_transform.cpp`
- Create: `native/src/preprocess/tensor_workspace.h`
- Modify: `native/src/models/rtmdet/*`
- Modify: `native/src/models/rtmpose/*`
- Create: `tests/native/test_image_transform.cpp`
- Modify: `tests/native/test_pose_affine.cpp`

**Interfaces:**
- `ImageTransformSpec { dst_width, dst_height, channel_order, mean[3], std[3], letterbox, affine }`.
- `TensorWorkspace` owns reusable aligned buffers sized at initialization/first growth.

- [ ] **Step 1: Move current detector/pose resize/affine/normalize golden behavior into tests before changing implementation.**
- [ ] **Step 2: Implement one reusable preprocessor with buffer reuse; hot path performs no new managed allocation and avoids repeated native vector growth.**
- [ ] **Step 3: Add ARM64 NEON-specialized inner loop behind compile guards with scalar reference path used by tests and unsupported platforms.**
- [ ] **Step 4: Add stage timing boundaries for preprocess/inference/postprocess.**
- [ ] **Step 5: Run image/pose/model tests.**
- [ ] **Step 6: Commit.**

### Task 4: Implement precision top-down pipeline with RTMDet-nano 320 + RTMPose Body26

**Files:**
- Create: `native/src/pipeline/precision_topdown_pipeline.h`
- Create: `native/src/pipeline/precision_topdown_pipeline.cpp`
- Refactor/modify: `native/src/models/rtmdet/*`
- Refactor/modify: `native/src/models/rtmpose/*`
- Create: `native/src/models/rtmpose/body26_adapter.h`
- Create: `native/src/models/rtmpose/body26_adapter.cpp`
- Create: `tests/native/test_precision_topdown_pipeline.cpp`
- Create/update: `tools/setup/prepare_040_models.py`
- Modify: model-pack manifests.

**Interfaces:**
- Detector model role `person_detector` with 320 analysis contract.
- Body model role `body_pose` with Body26/256x192 contract.
- Detector cadence defaults to every 5 accepted inference frames; confidence/invalid ROI requests immediate reacquisition.

- [ ] **Step 1: Add decoder golden fixtures from official OpenMMLab exported models and failing tests that assert source-space boxes/keypoints within recorded tolerance.**
- [ ] **Step 2: Add `prepare_040_models.py` that downloads only official model assets selected in the manifest, computes SHA-256, verifies expected tensor contracts, and writes hashes back to generated release manifests. Do not overwrite an existing model with a different hash under the same pack version.**
- [ ] **Step 3: Generalize RTMDet input metadata instead of hard-coding 640; instantiate Nano at 320 from manifest.**
- [ ] **Step 4: Generalize RTMPose decoder keypoint count/schema; add Body26 adapter.**
- [ ] **Step 5: Implement reduced detector cadence, fresh pose-derived ROI reuse and stale-detector rejection.**
- [ ] **Step 6: Run precision pipeline/model golden tests and existing Legacy regression tests.**
- [ ] **Step 7: Commit.**

### Task 5: Implement RTMO one-stage multi-person pipeline

**Files:**
- Create: `native/src/models/rtmo/rtmo_model.h`
- Create: `native/src/models/rtmo/rtmo_model.cpp`
- Create: `native/src/models/rtmo/rtmo_decoder.h`
- Create: `native/src/models/rtmo/rtmo_decoder.cpp`
- Create: `native/src/pipeline/realtime_multiperson_pipeline.h`
- Create: `native/src/pipeline/realtime_multiperson_pipeline.cpp`
- Create: `tests/native/test_rtmo_decoder.cpp`
- Create: `tests/native/test_realtime_multiperson_pipeline.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: model-pack manifest/setup script.

**Interfaces:**
- One inference produces zero-to-`MaxBodies` `BodyObservation` objects.
- Pipeline output is source-coordinate observations; RTMO keypoint indices stay internal.

- [ ] **Step 1: Record official RTMO-t 416 and RTMO-s candidate ONNX I/O metadata/golden outputs using an offline reference script under `tools/reference/`; select RTMO-t as mobile-safe default if both are packaged, with RTMO-s selectable by manifest profile.**
- [ ] **Step 2: Write failing decoder tests for zero/one/multiple people, confidence thresholding, NMS/pose NMS behavior, MaxBodies truncation and coordinate unletterboxing.**
- [ ] **Step 3: Implement preprocessing, ONNX invocation and decoder without using RTMPose per-person calls.**
- [ ] **Step 4: Add region-aware post-filtering while allowing tracker in Plan 03 to own identity.**
- [ ] **Step 5: Run RTMO tests on CPU backend as deterministic automated validation.**
- [ ] **Step 6: Commit.**

### Task 6: Implement Android QNN/HTP backend as an optional runtime capability

**Files:**
- Create: `native/src/backend/qnn/qnn_backend.h`
- Create: `native/src/backend/qnn/qnn_backend.cpp`
- Modify: `native/src/backend/backend_registry.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tools/setup/prepare_live_dependencies.py`
- Modify: `tools/package/build_live_native.ps1`
- Create: `docs/QNN_ANDROID_BUILD.md`
- Create: `tests/native/test_qnn_backend_contract.cpp`

**Interfaces:**
- CMake option `HV_USE_QNN=ON|OFF` and `HV_QNN_HOME`/authorized package location.
- `QnnBackend` reports `actual=QNN_HTP` only after successful provider/session initialization.
- Failure records provider/session error and returns control to registry fallback.

- [ ] **Step 1: Add contract tests using a fake provider loader so fallback behavior is testable without Qualcomm hardware.**
- [ ] **Step 2: Implement the Android-native QNN integration compatible with the chosen current ONNX Runtime QNN path. For a source/static build, use Android's required static QNN EP configuration; if the repository adopts the current Android plugin package, encapsulate its loading entirely under this backend and runtime packaging layer.**
- [ ] **Step 3: Add `backend_type=htp`, context-cache hooks, session profiling switch and HTP subsystem-restart error classification.**
- [ ] **Step 4: Make QNN dependency preparation conditional. A build without authorized QNN dependencies must still compile with `HV_USE_QNN=OFF` and use NNAPI/CPU.**
- [ ] **Step 5: Build Android ARM64 twice where dependencies permit: fallback build and QNN-enabled build. Do not claim phone execution.**
- [ ] **Step 6: Commit.**

### Task 7: Expand stage statistics and backend reporting

**Files:**
- Modify: `native/src/core/stats_collector.h`
- Modify: `native/src/core/stats_collector.cpp`
- Modify: `native/include/humanvision/humanvision_types.h`
- Modify: `native/src/core/humanvision_c.cpp`
- Modify: `tests/native/test_stats_collector.cpp`

**Interfaces:**
- Stats include detector/body/hand preprocess, inference, postprocess and total; detector/body/hand throughput; pipeline; requested backend; actual providers; fallback reason; result age and dropped frames.

- [ ] **Step 1: Write rolling-stats tests using deterministic synthetic durations.**
- [ ] **Step 2: Add V2 stats structure rather than changing V1 layout.**
- [ ] **Step 3: Populate values from both pipeline implementations/backend sessions.**
- [ ] **Step 4: Run stats/C API tests.**
- [ ] **Step 5: Commit.**

### Task 8: Runtime pipeline selection and fallback integration

**Files:**
- Modify: `native/src/pipeline/pipeline_registry.cpp`
- Modify: `native/src/core/humanvision_engine.cpp`
- Modify: `tests/native/test_pipeline_registry.cpp`
- Modify: `tests/native/test_c_api.cpp`

**Interfaces:**
- `Auto`: `MaxBodies <= 2` -> PrecisionTopDown, `MaxBodies >= 3` -> RealtimeMultiPerson.
- Selection occurs at configure/start, not every frame.
- Initialization failure may fall back only to an explicitly documented compatible pipeline/profile; the fallback is visible through capabilities/stats/error text.

- [ ] **Step 1: Add selection/fallback tests for MaxBodies 1,2,3,8 and unavailable preferred model/backend.**
- [ ] **Step 2: Connect registry to engine initialization.**
- [ ] **Step 3: Ensure changing settings resets pipeline/tracker state exactly once.**
- [ ] **Step 4: Run full native test suite.**
- [ ] **Step 5: Commit.**
