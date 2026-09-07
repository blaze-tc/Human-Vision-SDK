# HumanVision SDK skeleton acceptance requirements

Requirements recorded: 2026-09-07.

## User-confirmed delivery order

Complete and validate the independent HumanVision SDK's skeleton recognition
before integrating it into AzureKinectExamples. The imported examples are a
joint-schema reference at this stage, not an integration work item.

The existing D0.4 implementation and its historical verification remain the
regression baseline. These requirements do not claim that the new target has
been implemented or achieved.

## Confirmed target

- Target platforms: Windows and Android on RK3588 (ARM64).
- Concurrent participants: 1 through 8; retain runtime-configurable body count.
- Actual skeleton output: 30 completed updates per second for every tracked
  participant under the agreed visible-person test conditions, including the
  eight-person case.
- Both hands must provide the hand-related joints present in the imported
  Kinect joint schema, using actual image inference.
- Aim for Kinect-like responsive and stable skeleton following.
- Complete SDK acceptance before AzureKinectExamples adapter development.

## Joint contract

Use the existing 32-joint compatibility definition in
`E:/UnityProject/Human-Vision-SDK-Test/Assets/AzureKinectExamples/KinectScripts/KinectInterop.cs`
as the required output semantics, including its ordering when later mapping to
that project. Its ordering must not be assumed identical to Microsoft's native
Azure Kinect joint enum.

The user explicitly clarified that each hand needs only Hand (palm), Handtip
and Thumb. Wrist remains part of the full-body skeleton. A complete 21-landmark
hand skeleton is not an output requirement; an internal model may use additional
landmarks to obtain the required three points. Hand and fingertip estimates must be grounded
in actual hand-image inference, not copies of the wrist, fixed offsets, or
extrapolations represented as observations.

The current COCO-17 model lacks the required hand endpoints and explicit foot
endpoints. Model selection must cover these gaps as well as the full-body
result. Geometrically derived torso/compatibility joints must remain identified
as inferred. Low-confidence or occluded joints must expose validity/confidence;
the target does not authorize fabricated observations.

The RGB baseline remains image-space/virtual-plane pose. Kinect-like following
does not establish metric depth accuracy. Any future real 3D requirement needs
its own model, coordinate contract and validation.

## Measurement rules

- Measure actual complete skeleton updates separately from Unity rendering and
  input camera FPS.
- Repeated snapshots, display interpolation, or render-loop polling do not
  count as new inference results.
- Updating different people on alternate frames does not satisfy 30 FPS per
  person. Record per-person and per-hand update rates and source timestamps.
- Record body/hand source-frame skew, result age and end-to-end latency. A fast
  publication rate with stale hand results is not complete fresh output.
- Measure all supported person-count cases, with special attention to 1, 2, 4,
  6 and 8 people. Configuring a limit of 8 with only 2 visible people does not
  validate eight-person performance.
- Preserve asynchronous submission, bounded latest-frame processing, reusable
  buffers, stable IDs and Unity-main-thread-only object updates.

## Hardware and test conditions

Read-only inspection on 2026-09-07 identified this Windows development host as
AMD Ryzen 7 4800H (8 cores / 16 logical processors), with NVIDIA GeForce RTX 2060
and AMD integrated graphics. This is an available initial benchmark machine,
not a claim of minimum supported hardware or target performance.

The user selected RK3588 as the Android hardware baseline, referencing
`E:/项目文档/宇视 体育体感游戏/RK3588 Brief Datasheet.pdf` on 2026-09-07.
The supplied three-page Rockchip brief was inspected as source material, not
as development instructions. Its first page lists:

- CPU: four Cortex-A76 cores plus four Cortex-A55 cores.
- GPU: Mali-G610 MC4 (the second-page block diagram labels it MP4).
- NPU: 6 TOPS at INT8; this is a chip specification, not model throughput.
- Memory interface: 64-bit LPDDR4/LPDDR4X/LPDDR5; installed RAM capacity is not
  specified by this chip brief.
- SDK: Android 12 is listed in this supplied document. Actual board firmware
  and runtime/driver versions must be recorded when hardware is available.
- Camera/connectivity: MIPI camera inputs, USB 3.1 and Ethernet interfaces.
- Video decode: H.264/H.265 and other codec support, with codec-dependent limits.

Use RK3588 NPU acceleration as a performance-design candidate for the Android
target; the model conversion, runtime compatibility and achieved performance
still require validation. The chip's advertised TOPS or video decode rate does
not establish eight-person, both-hands skeleton inference at 30 FPS.

Windows and Android performance must each be measured on an identified device;
a Windows pass cannot substitute for Android acceptance. The specific RK3588
board, RAM capacity, firmware, cooling and camera remain test-environment details
to record, not reasons to postpone model and SDK design.

Before final acceptance design, resolve:

- Exact RK3588 board, RAM, Android firmware and NPU driver/runtime versions.
- Intended camera resolution, participant distance and minimum visible hand
  size; record these for every benchmark.
- Numerical latency, jitter, dropout and ID-switch limits corresponding to the
  requested Kinect-like following quality.
- Continuous-run duration and sustained/thermal performance criteria.

## Engineering implications and current limitations

The September 5 status records about 4.6-5.0 inference FPS for a two-person CPU
run. Those results are historical and do not satisfy this target.

Body plus separate hand inference and a whole-body model are candidates to
benchmark; no replacement model or acceleration backend is selected here.
Retain the validated ONNX models and native ABI as a regression baseline while
designing a compatible extension. Do not silently overwrite the COCO-17 ABI
with a differently sized body structure.

The existing D0/D1 plan excludes GPU optimization and Android implementation.
Before such implementation, reconcile the execution plan and scope rules with
these new user requirements. Recording the expanded target is not evidence
that a model, backend, or platform milestone has passed.
