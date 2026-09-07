# Independent SDK skeleton execution plan

> Execute one milestone at a time with test-first changes, fresh builds/tests,
> measured results and a status update before advancing. Use the
> superpowers:executing-plans workflow; independent research/review may run in
> parallel. The user's 2026-09-07 start instruction authorizes this work.

**Goal:** Validate an independent Windows/RK3588 Android SDK for 1-8 people at
30 complete fresh skeleton updates per second per person, including real palm,
handtip and thumb outputs, before integrating AzureKinectExamples.

**Architecture:** Reuse the existing asynchronous native core and immutable
result snapshots. Keep the existing COCO-17 ABI working while evaluating a
versioned richer result. Model adapters remain separate from Unity; the
Kinect-compatible joint ordering is an adapter contract, not a detector type.

**Tech stack:** C++17, CMake, ONNX canonical models, Unity 2021.3.45f1 accepted
Windows test host, ONNX Runtime reference backend, RK3588 Android ARM64 target.

**Spec:** `docs/SDK_SKELETON_REQUIREMENTS.md`.

## Global constraints

- Actual complete per-person updates count toward 30 FPS; interpolation and
  repeated or stale body/hand snapshots do not.
- Both hands require Hand, Handtip and Thumb; no public 21-point-hand requirement.
- Do not invent unseen endpoints or label derived joints as direct observations.
- Keep dynamic capacity, source timestamps, latest-frame dropping and reusable
  buffers. Never wait for model inference on Unity's main thread.
- Preserve the tested D0 models/ABI and imported vendor sensor code.
- Windows and Android must have separate hardware/runtime evidence.
- Segmentation, Azure integration and application gameplay remain deferred.
- Acceleration/model changes are permitted for this user-approved target only
  after a measured experiment and numerical/quality checks.

## S0 - Reproducible baseline and model feasibility

S0 is complete; see `docs/validation/S0_BASELINE_REPORT.md`. Its deliverable is a reproducible regression
baseline and an evidence-backed next model experiment, not eight-person success.

### Task 1 - Restore scene media acceptance

Files: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionDemoSceneContractTests.cs`,
the imported copy of that test, and the imported
`Assets/Scenes/HumanVisionD04Demo.unity`.

- [x] Add an EditMode test loading the saved Demo scene and inspecting
  `HumanVisionDemoBootstrap.startupVideo` via `SerializedObject`.
- [x] Assert `File.Exists(Path.Combine(Application.streamingAssetsPath,
  property.stringValue))`, with the resolved path in the failure message.
- [x] Run the focused test against the imported scene. Initial PASS: the saved
  path was already correct; expected RED was not applicable.
- [x] No production path repair was needed; preserve the current saved scene.
- [x] Repeat the focused test, then all EditMode tests.
- [x] Build the Windows demo and observe real playback/inference.

### Task 2 - Refresh native and performance evidence

Files: existing native build/tests and `tools/benchmark/hv_video_benchmark.cpp`
are consumed unchanged; evidence goes under `out/validation/s0/` and the
summary goes in `docs/validation/S0_BASELINE_REPORT.md`.

- [x] Build Debug/Release using the installed v143 developer environment and
  project CMake presets; run both CTest presets.
- [x] Run the real two-person fixture with runtime body limits 1, 2 and 8 using
  the existing benchmark's documented CLI. Preserve CSV and JSON output.
- [x] Record measured body count, stage times and reciprocal total-stage FPS;
  label this sequential benchmark distinctly from realtime end-to-end FPS.
- [x] Record Windows hardware and explicitly state that the two-person fixture
  does not validate eight actual people.

### Task 3 - Select the next reproducible experiment

File: `docs/validation/S0_MODEL_FEASIBILITY.md`.

- [x] Compare body-plus-hand and whole-body model candidates using official
  configs, deployable artifact links and exact output-schema references.
- [x] Inspect hand/foot coverage, ONNX/RKNN deployment support and source/license
  records. Do not infer approval to redistribute model weights from code license.
- [x] Select a first experiment and record unresolved conversion/hardware limits.
- [x] Review the diff, update `docs/DEVELOPMENT_STATUS.md`, and commit only the
  declared verified source and documentation files.

S0 acceptance: scene-media regression (RED/GREEN when a defect is present), native Debug/Release tests,
Unity EditMode tests and Windows build/runtime evidence, real baseline metrics,
and an actionable primary-source model assessment. Missing Android hardware
does not block S0; it blocks claims of Android throughput.

## Subsequent milestone gates

These are sequencing gates, not permission to implement unselected models.
Write the current milestone's concrete test/implementation steps when S0
measurements determine the model contract.

- **S1 Model reference:** obtain the selected model with provenance/hash,
  validate actual body/hand/foot outputs on real images and measure 1/2/4/8 ROI
  workloads. Synthetic ROI replication tests compute cost only, never actual
  eight-person recognition. Decide model/precision before production integration.
- **S2 Native rich skeleton:** add a backward-compatible result API and real
  model adapter, direct/derived validity rules, association and snapshot tests.
- **S3 Live input and following quality:** USB input, rotation/mirroring,
  timestamped body/hand synchronization, filter/ID/occlusion regression videos.
- **S4 Platform performance:** Windows acceleration and RK3588 conversion/runtime
  parity, real per-person/hand update metrics, latency and thermal tests.
- **S5 SDK acceptance:** actual 1-8 people, sustained 30 FPS, lifecycle and
  quality evidence on both targets. Only then schedule AzureKinectExamples.

## Execution decisions

- Work in the current dedicated `codex/d0-bootstrap` checkout to retain the
  installed model/toolchain artifacts and linked running Unity test project.
  No merge, push, package publication or vendor-source migration is included.
- Preserve the root requirements/status edits from the preceding conversation.
  The untracked supplied archives/images/document package are user assets and
  must not be included accidentally in commits.
- The old D1 RTSP-first schedule is deferred by the user's explicit SDK-first
  direction. Historical tests remain evidence of that earlier scope only.
