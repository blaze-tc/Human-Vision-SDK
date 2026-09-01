# HumanVision Windows Unity Vertical Slice Implementation Plan

> **For agentic workers:** execute this plan milestone-by-milestone. Use tests first, verify real outputs, update `docs/DEVELOPMENT_STATUS.md`, and do not enter later roadmap stages while D0/D1 is active.

**Goal:** Build a Windows x64 Unity Demo that processes local MP4 and then RTSP IPC video into RTMDet person boxes, stable track IDs, RTMPose COCO-17 joints, and measurable performance statistics without blocking Unity's main thread.

**Architecture:** Unity supplies frames through a narrow C ABI. Native C++ owns a latest-frame slot, worker thread, ONNX Runtime detector/pose pipeline, tracker, and double-buffered result snapshots. D1 adds RTSP as another source without modifying the body pipeline.

**Tech Stack:** C++17, CMake, MSVC 2022, GoogleTest, ONNX Runtime 1.29.0 CPU x64, RTMDet-tiny, RTMPose-s, Unity 2022.3 LTS, FFmpeg in D1.

**Spec:** `docs/DEMO_SCOPE.md`, `docs/ARCHITECTURE.md`, `docs/MODEL_MANIFEST.md`, `docs/SDK_API.md`

## Global Constraints

- Windows x64 only through D1.2.
- `MaxBodies` runtime configurable; default 4; no body-count fixed array in public ABI.
- COCO-17 joints are the fixed D0 skeleton schema.
- `HV_SubmitFrame` is asynchronous; latest frame wins.
- Unity main thread never waits for inference.
- D0/D1 excludes segmentation, Android, RKNN and GPU optimization.
- Every milestone must leave a runnable/testable artifact.

---

## D0.0 Repository & Build Bootstrap

**Files:**
- Create: `CMakeLists.txt`
- Create: `native/CMakeLists.txt`
- Create: `native/include/humanvision/humanvision_types.h`
- Create: `native/include/humanvision/humanvision_c.h`
- Create: `native/src/core/version.cpp`
- Create: `tests/native/test_smoke.cpp`
- Create: `cmake/` or `CMakePresets.json`

**Produces:** a Windows x64 DLL/test build that does not yet run models.

- [ ] Create the exact repository directories from `docs/ARCHITECTURE.md` needed for D0 only.
- [ ] Add a failing smoke test that includes the public header and expects a non-empty SDK version string.
- [ ] Configure/build the test and confirm the expected failure because implementation is missing.
- [ ] Implement only the version/smoke function required by the test.
- [ ] Build Debug and Release x64.
- [ ] Run CTest and confirm all smoke tests pass.
- [ ] Record commands/results in `docs/DEVELOPMENT_STATUS.md` and advance to D0.1.

**Acceptance:** clean configure/build/test on Windows x64 with no model/runtime dependency.

---

## D0.1 Python/OpenMMLab Reference + ONNX Contract

**Files:**
- Create: `tools/reference/README.md`
- Create: `tools/reference/run_reference.py`
- Create: `tools/reference/export_models.py` or documented MMDeploy commands
- Create: `tools/reference/compare_onnx.py`
- Create: `models/detector/model_info.json`
- Create: `models/pose/model_info.json`
- Create: `tools/reference/environment.lock.txt`

**Consumes:** model identifiers from `docs/MODEL_MANIFEST.md`.

**Produces:** validated ONNX detector/pose files plus explicit tensor/preprocess/postprocess metadata.

- [ ] Run official RTMDet-tiny PyTorch inference on an official demo image and save box/score reference JSON.
- [ ] Run official RTMPose-s PyTorch inference on a person ROI and save COCO-17 reference JSON.
- [ ] Export detector ONNX and write `model_info.json` with input/output names, shapes, opset, preprocess and SHA-256.
- [ ] Export pose ONNX and write `model_info.json` including SimCC split ratio and normalization values.
- [ ] Write comparison scripts that run ONNX Runtime Python inference and compare against reference outputs with documented numerical/coordinate tolerances.
- [ ] Run comparisons and fix export/preprocess mismatches until they pass on at least one reference image each.
- [ ] Save exact Python environment versions.
- [ ] Update status and advance to D0.2.

**Acceptance:** ONNX Runtime Python output is demonstrably aligned with official PyTorch reference. Do not proceed on an unverified export.

---

## D0.2 C ABI + Async Frame Core + RTMDet ONNX

**Files:**
- Create/modify public headers per `docs/SDK_API.md`
- Create: `native/src/core/humanvision_engine.*`
- Create: `native/src/core/latest_frame_slot.*`
- Create: `native/src/core/result_snapshot_store.*`
- Create: `native/src/backend/i_inference_backend.h`
- Create: `native/src/backend/onnx/onnx_runtime_backend.*`
- Create: `native/src/models/rtmdet/rtmdet_preprocess.*`
- Create: `native/src/models/rtmdet/rtmdet_postprocess.*`
- Create: `native/src/models/rtmdet/rtmdet_model.*`
- Create tests under `tests/native/` and `tests/golden/`

**Produces:** asynchronous native detector capable of returning dynamic person boxes.

- [ ] Write C ABI tests for invalid config, `MaxBodies=1/2/4/6/8`, buffer capacity handling, and create/destroy cycles.
- [ ] Write latest-frame concurrency test: submit faster than a fake deterministic worker can consume and assert old frames are dropped rather than queued.
- [ ] Implement config/frame/result stores to satisfy tests without model code.
- [ ] Add ONNX Runtime backend load/run tests using a tiny public fixture model.
- [ ] Add RTMDet preprocess/postprocess golden tests using D0.1 reference data.
- [ ] Implement RTMDet person-only inference and source-coordinate restoration.
- [ ] Run one real image/frame through the C++ detector and compare boxes to reference tolerance.
- [ ] Update status and advance to D0.3.

**Acceptance:** native code asynchronously processes submitted frames and returns real person boxes, with runtime `MaxBodies` enforced at the selection stage.

---

## D0.3 RTMPose + Tracker + Native Video Benchmark

**Files:**
- Create: `native/src/models/rtmpose/pose_affine.*`
- Create: `native/src/models/rtmpose/rtmpose_preprocess.*`
- Create: `native/src/models/rtmpose/simcc_decoder.*`
- Create: `native/src/models/rtmpose/rtmpose_model.*`
- Create: `native/src/tracking/i_body_tracker.h`
- Create: `native/src/tracking/center_iou_tracker.*`
- Create: `native/src/core/stats_collector.*`
- Create: `tools/benchmark/hv_video_benchmark.*`

**Produces:** native local-video pipeline with boxes, track IDs, COCO-17 joints and timing.

- [ ] Write pose affine/coordinate restoration tests against D0.1 reference ROI.
- [ ] Write SimCC decoder tests with known synthetic peak tensors.
- [ ] Implement RTMPose ONNX preprocessing/inference/decoding and verify real reference joints.
- [ ] Write tracker tests for continuous motion, short missing detections, two-person crossing, and unique IDs.
- [ ] Implement center+IoU+velocity tracker behind `IBodyTracker`.
- [ ] Integrate detector -> tracker -> pose into `HumanVisionEngine`.
- [ ] Add native video benchmark input for regression media; using FFmpeg/OpenCV only as a benchmark/test reader is acceptable here if clearly isolated.
- [ ] Run 1-person and multi-person clips and export CSV/JSON timing summary.
- [ ] Confirm result count changes correctly for `MaxBodies=1/2/4`.
- [ ] Update status and advance to D0.4.

**Acceptance:** native video benchmark visibly/quantitatively produces real IDs + 17 joints and reports detection/pose/tracking/total time.

---

## D0.4 Unity Local Video Demo

**Files:**
- Create Unity project under `unity/HumanVisionDemo/`
- Create: `Assets/HumanVision/Runtime/NativeBindings.cs`
- Create: `Assets/HumanVision/Runtime/HumanVisionConfig.cs`
- Create: `Assets/HumanVision/Runtime/HumanVisionManager.cs`
- Create: `Assets/HumanVision/Runtime/HumanVisionBody.cs`
- Create: `Assets/HumanVision/Demo/VideoPlayerFrameSource.cs`
- Create: `Assets/HumanVision/Demo/HumanVisionOverlay.cs`
- Create: `Assets/HumanVision/Demo/HumanVisionHud.cs`
- Copy verified `humanvision.dll` under `Assets/Plugins/x86_64/`

**Produces:** the first user-testable Unity Demo.

- [ ] Write EditMode tests for managed struct sizes/field mapping where practical and configuration validation.
- [ ] Bind the exact C ABI from `SDK_API.md` with explicit packing/calling convention tests.
- [ ] Implement reusable managed body/joint buffers; do not allocate body arrays every Update.
- [ ] Implement `VideoPlayer -> RenderTexture -> AsyncGPUReadback` frame source with a small reusable buffer pool.
- [ ] Submit frames without waiting for inference.
- [ ] Poll latest result sequence; only redraw when a new result is published.
- [ ] Draw source video, bounding boxes, track IDs and COCO-17 bone lines.
- [ ] Add HUD fields from `HV_Stats` and configured `MaxBodies`.
- [ ] Run single-person and multi-person MP4 in Editor and Windows Standalone.
- [ ] Confirm UI/render thread stays responsive when inference FPS is below render FPS and dropped-frame count rises.
- [ ] Save screenshots/logs/benchmark notes and update status to D1.0.

**Acceptance:** Windows Unity Demo visibly works on local MP4 and can change `MaxBodies` at runtime without native recompilation.

---

## D1.0 RTSP IPC Input

**Files:**
- Create: `native/src/video/rtsp/i_rtsp_client.h`
- Create: `native/src/video/rtsp/i_video_decoder.h`
- Create: `native/src/video/rtsp/ffmpeg_rtsp_client.*`
- Create: `native/src/video/rtsp/ffmpeg_decoder.*`
- Create: `native/src/video/rtsp/reconnect_policy.*`
- Add narrow RTSP control API or source component without changing body result ABI.
- Create Unity `RtspFrameSource`/controller only as needed to configure native RTSP.

**Produces:** real IPC frames feeding the exact same HumanVisionEngine.

- [ ] Add unit tests for RTSP URL/config validation and reconnect policy timing/state.
- [ ] Integrate FFmpeg demux/decode off the Unity main thread.
- [ ] Default RTSP transport to TCP; expose UDP as optional Demo config.
- [ ] Keep only latest decoded frame before inference.
- [ ] Add explicit connection/stream/reconnect/error status to HUD/logs.
- [ ] Test with a real IPC H.264 RTSP stream.
- [ ] Disconnect network/camera and verify Unity does not crash.
- [ ] Restore stream and verify processing resumes or provides an actionable manual-reconnect state if automatic recovery is not yet supported.
- [ ] Update status to D1.1.

**Acceptance:** real IPC can replace local video as input without modifying detector/tracker/pose/Unity overlay logic.

---

## D1.1 Real 1~4 Person Field Validation

**Files:**
- Add results under `docs/validation/`
- Update no architecture unless measurements justify it.

**Produces:** evidence-based decision on accuracy/performance.

- [ ] Run 1-person near-distance test.
- [ ] Run 2-person test.
- [ ] Run 4-person test when participants are available.
- [ ] Run far-person test representative of the actual installation.
- [ ] Run two-person crossing/occlusion test.
- [ ] Record false positives from background objects.
- [ ] Record body dropouts, visible ID switches, pose jitter, detection/pose/total latency, input/inference FPS, memory and dropped frames.
- [ ] Repeat with `MaxBodies=1/2/4/6` where scene/hardware allows.
- [ ] Write `docs/validation/D1_FIELD_TEST_REPORT.md` with pass/fail issues and measured bottlenecks.
- [ ] Update status to D1.2.

**Acceptance:** the team can answer whether the baseline model is sufficiently stable and what the actual bottleneck is instead of optimizing by guesswork.

---

## D1.2 Demo Stabilization & Next-Stage Decision

**Produces:** a frozen Demo checkpoint and a documented recommendation.

- [ ] Fix only D0/D1 defects found in field validation that block evaluation or cause crashes/major instability.
- [ ] Re-run the affected field scenarios.
- [ ] Freeze a tagged Demo build and record model checksums/toolchain versions.
- [ ] Decide, based on measurements, whether the next priority is: tracker upgrade, lighter/heavier model, GPU backend, USB/WebCam source, or segmentation.
- [ ] Do not implement the selected next priority in this milestone; update `LONG_TERM_ROADMAP.md` and create a new approved plan for it.

**Acceptance:** reproducible Windows Unity + RTSP Demo package, validation report, and evidence-based next-stage recommendation.
