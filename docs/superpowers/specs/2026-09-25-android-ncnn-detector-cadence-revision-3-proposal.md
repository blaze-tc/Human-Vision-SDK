# Android ncnn TopDown detector cadence — Revision 3 proposal

Status: **proposed for user design review; no implementation authorized**  
Date: 2026-09-25  
Base: approved [Revision 2](2026-09-13-android-vulkan-ncnn-production-runtime-design.md), implementation branch `codex/android-ncnn-vulkan-implementation` at `c6425acdaca72cce8cd75f513233ffd44bae3b7e`  
Scope: Milestone C TopDown detection scheduling and its acceptance sequence only

**User outcome:** On the Android device, the live camera image stays smooth and
the visible skeleton stays attached to moving people without flashing, long
pauses, or accumulating delay. Detector cadence is only a means to that end;
model microbenchmarks do not constitute acceptance. The integrated camera
scene and the user's physical test decide whether the SDK works. The user
reaffirmed on 2026-09-25 that **30 fresh complete skeleton observation
frames/s remains a hard acceptance requirement**; visual smoothness alone
cannot waive it.

If approved, this revision supersedes Revision 2 §9.1's per-detector frame-period
exit, §10's detector-on-every-processed-frame steps, and the detector/pose
wording of §13.2 only to the extent needed to count current-frame pose with
a separately timestamped older detector anchor. All other Revision 2
requirements retain priority.

## 1. Decision requested and evidence

Revision 2 §10 runs the person detector for every accepted GPU frame. On the authorized Snapdragon 888, RTMDet Nano passed four-image detector golden parity and fully Vulkan execution, but its three warmed complete-detector P95 runs were **38.7561, 39.6981, and 39.3521 ms**. NanoDet-Plus-m 320 failed at **48.2666, 49.5960, and 49.0909 ms** after its one permitted output crop. The two later authorized candidates, PP-PicoDet-XS 320 and MobileNet-SSD VOC 300, also exceeded 33.33 ms in all three runs of their graph-plus-required-download *lower-bound* measurements. [The C2 gate report](../../validation/RTMDET_NCNN_CONVERSION_GATE.md) records input, model, runner, and log hashes and the limitations of each result. No production detector has been selected, C2 has not passed, and C3 remains unauthorized under Revision 2.

**Proposed change:** execute a validated person detector on bounded keyframes, while running RTMPose on the *current camera image* for every accepted pose frame using short-lived track crops updated by current-image joints. A pose-only frame can count as one fresh complete observation only when all published bodies have joints inferred from that same source frame. Detector boxes and confidence retain their older source metadata; they are never presented as new detections. This proposal does **not** claim that the device can reach 30 fresh observations/s. It changes the testable scheduling hypothesis, not the performance target.

## 2. Alternatives and choice

| Approach | Consequence |
| --- | --- |
| Continue requiring a detector on every frame | Preserves Revision 2 §10 but all four tested candidates already exceed the entire 33.33 ms period before pose. Another unbounded model search is not authorized. |
| **Bounded detector keyframes plus fresh pose on every processed frame** | Keeps the approved TopDown model family and fresh-joint contract; adds scheduling, track-crop, delayed-result, and entry/exit correctness risks that require an integrated device gate. Recommended for one bounded feasibility attempt. |
| Bring RTMO or a different accelerator ahead of TopDown | Changes the approved C→D order or Android backend scope. It is outside this proposal. |

At a 30 FPS camera target, RTMDet's observed roughly 39 ms P95 would consume
about 9.75 ms of GPU time per frame if admitted once every four frames, before
pose and bridge work. This quotient is a screening estimate, not a latency
proof: a non-preemptible detector job can still delay an individual pose frame
past its deadline. Only the integrated age, throughput, and correctness gate
can decide feasibility.

## 3. Unchanged contracts

- A→B→C→D order, `NCNN Vulkan` fail-fast mode, Android API 26/ARM64, same-physical-device Vulkan/AHB bridge, three reusable slots, sync-fd ownership, no full-frame CPU readback, ncnn Vulkan/FP16 and explicit model packing remain mandatory.
- The Unity skeleton API, V1 C ABI, Canonical Skeleton, Tracker identity semantics, Region slots, profile/model-pack architecture, and atomic observation snapshot contract remain stable. Any new diagnostics use additive versioned records.
- RTMPose TopDown precedes RTMO. Hand, QNN, MediaPipe, Windows, ORT performance work, renderer changes, and APK stripping remain excluded.
- One fresh observation means **one unique source frame** with one complete current Body set, not one result per person. The existing Snapdragon 888 gate remains at least 29.0 fresh complete observation frames/s in every rolling 10 seconds and 29.5 over 60 seconds, age P50 ≤75 ms, P95 ≤100 ms, no ordinary sample >250 ms, combined bridge drops ≤1%, plus identity, Region, orientation, crash, and thermal checks. Preview or output sampling FPS cannot substitute for inference FPS.

## 4. GPU data flow and ownership

The existing Unity producer emits an AHB-backed source frame with an original frame ID, capture timestamp, orientation, and generation. The ncnn consumer still acquires it through the measured AHB import and sync-fd protocol. A bounded scheduler selects a *pose job* from the latest ready frame. It uses the current AHB image for each ROI crop, runs RTMPose for every selected track, decodes current-image SimCC tensors, validates joints, updates the crop from those joints, assigns Regions, and atomically publishes the complete observation. All included Bodies have the same current source frame ID/timestamp. If one selected body's pose fails, that body is marked absent/invalid for that observation; no old joints are copied forward.

At a bounded cadence the scheduler also captures a detector keyframe. GPU preprocessing creates a reusable 320×320 FP16 detector input on the ncnn device while the keyframe AHB lease is valid. The AHB slot is released through the existing cross-device protocol **only after both** that GPU copy and all current-frame pose ROI reads have completed; detector inference may then continue from the independent small tensor without holding the AHB. The small detector input tensor and its frame metadata remain owned by a fixed, preallocated detector-job slot until inference and decode finish. There is no CPU copy of source pixels, no per-frame AHB or import-pipeline creation, and no unbounded queue of detector inputs.

The detector and pose use the same ncnn Vulkan device. At most one detector job may be queued or running; newer requested keyframes supersede an unstarted older one. The scheduler gives pose frames priority **at job admission boundaries** and never waits in a Unity render callback or blocks for a detector result on the Unity main thread. It does not assume the GPU can preempt an already submitted detector graph or run the two graphs concurrently. If a detector graph blocks enough pose jobs to violate the fresh-FPS/age gates, the integrated attempt fails; no prediction or duplicate snapshots are added to conceal it. The three-slot AHB ring and latest-frame/drop behavior remain bounded.

Detector output carries its own source frame ID and timestamp. It may initialize new tracks only for subsequent pose frames, and only if the result arrives within **200 ms of its capture** and passes the normal geometry/score gate. For an existing track, delayed detections are matched against the detector-time anchor; they cannot rewind a newer pose-updated crop or revise a published observation. A result from another camera session, size, rotation, bridge generation, or Region revision is discarded. Rotation and session changes drain/retire old jobs before rebuilding cached resources.

## 5. Crop, identity, and detection policy

The scheduler uses detector boxes to discover people and correct track drift. Between detector keyframes, a track's ROI comes from its last valid current-image pose joints, with a bounded margin and image clipping. Short crop-only extrapolation is permitted to find the next pose; **joint extrapolation is not**. The existing CPU tracker can predict a crop for up to three seconds; that timeout is not suitable as the GPU-path acceptance policy. The GPU path must use a separately tested, profile-declared bound and must invalidate a track after repeated failed current-image poses or a stale anchor. It must not reuse a body, confidence, or region index after validation fails.

For the first feasibility profile, the detector interval is bounded to **2–6 accepted pose frames**, never more than **200 ms of source capture time** between detector-keyframe capture attempts while a live camera runs. The time limit applies even if inference drops pose frames; a missed deadline is counted as a scheduling failure. The scheduler may request an earlier keyframe on startup, no tracks, pose rejection, crop near the image edge, changed body count, or Region/session change. A track supported by fresh valid poses may continue between detections for at most **500 ms since its last successful detection**; a longer gap or two consecutive missed detector matches requires reacquisition. The last detector age and consecutive misses are visible in diagnostics. A track with no valid current pose is removed from *publication* immediately and requests reacquisition; its identity may be retained internally only inside the bounded retry window. An unknown entrant cannot be synthesized from another person's track. At most `MaxBodies` tracks are posed, with the same deterministic detector ordering and Region collision policy as Revision 2.

The exact accepted interval is a **measured profile value**, not an adaptive way to skip costly work silently. A bounded sweep of intervals 2, 3, 4, 5, and 6 selects the shortest interval that passes the integrated correctness and performance gates; if none passes, the design fails and stops for another user decision. Rate limiting, detector cadence, bridge-pressure drops, and pose-job drops are reported as separate counters. Changing the interval requires a new profile hash/build or explicit runtime configuration with the same validation and telemetry; it may not change invisibly during acceptance.

## 6. Freshness, failure, and diagnostics

For a pose-only observation, source freshness is established by RTMPose inference on the corresponding current AHB frame, **not** by reusing the last detector result. Each body records internally the pose frame ID, ROI provenance, detector anchor frame ID/age, pose confidence, and validity decision. The public V1 layout stays unchanged; an additive diagnostics record exposes these facts without mislabeling old detector confidence as a current detection. A valid empty observation is possible when no person is currently accepted, but zero-body frames cannot satisfy body-recall tests in a scene with people.

The existing end-to-end age measurement still starts at the original camera capture timestamp and ends at first atomic publication. Add separate P50/P95 pose-only age, detector-keyframe age, detector completion lag, per-body pose time, detector interval, delayed-result discards, pose validation failures, track reacquisition latency, source/bridge/inference drops, and fresh observation FPS. Count a detector-keyframe observation only once even if both detector and pose run for it. Repeated display of a previous snapshot never increments the fresh counter.

If a detector job fails Vulkan, FP16, import, model, or output-contract checks, the `NCNN Vulkan` runtime reports the error and stops; it never selects ORT. A stale detector result is discarded with a counted reason. If detector scheduling cannot meet the configured maximum interval, if people are missed/reassigned, or if the 30-frame/age gates fail, the milestone is **FAIL**, not a degraded success. No long skeleton prediction or automatic runtime-mode fallback is introduced.

## 7. Feasibility and acceptance sequence

The existing RTMDet Nano is the first *provisional* detector for the integrated attempt because it has four-image golden parity and an audited wholly Vulkan graph. Its prior >33.33 ms complete-detector P95 remains a failed every-frame result. Before packaging even a local test APK, reproduce the graph/weights from pinned inputs, bind the detector-only FP16 pack1 input contract to the B5 preprocessor, confirm no per-layer CPU fallback, and repeat the fixed real-image golden tests. The candidate may enter a **local evaluation ModelPack**, clearly marked unaccepted; source and trained-weight redistribution rights must be resolved before any public package or Release. No extra detector candidate is authorized by this proposal.

The current C2 rule requiring a detector's standalone P95 ≤33.33 ms must be replaced, **only after this Revision 3 is approved**, by two distinct gates: (1) model correctness/provenance/strict Vulkan eligibility before C3, and (2) integrated detector-cadence + current-frame pose performance and body correctness at C6. This is a deliberate Design-over-Plan change: C3 may then convert/package RTMPose for local evaluation without falsely declaring C2's original performance gate passed. C4 implements the bounded scheduler and delayed-result contracts; its present test requirement for one detector run *per source frame* must be replaced with one per admitted keyframe. C5 keeps post-inference Region assignment; C6 owns the full player/device gate. Task-by-task tests, reviews, and commits remain required. The implementation plan must be revised before code work resumes.

Automated tests first cover schedule/drop invariants, AHB lease release before asynchronous detector completion, buffer reuse, delayed/out-of-generation detector results, camera rotation/session rebuild, track crop bounds, current-frame joint provenance, multi-body atomic publication, false/occluded poses, entry/exit/crossing, Region assignments, and stable IDs. A fake GPU backend must be able to inject a 40 ms detector stall and prove the render thread never waits and the FPS counter cannot increase from reused snapshots. Model golden and APK/hash/build checks remain required.

The physical feasibility gate is the **integrated `HumanVisionCameraDemo`**, not a benchmark-only pipeline. On the authorized Snapdragon 888, first collect paired GPU/CPU timings and detector/pose queue traces for each interval candidate under live camera input, then run the unchanged Revision 2 5-second warm-up, 60-second 1-person and 2-person windows and 15-minute thermal run for a candidate that meets the short-window metrics. Test a person entering/exiting, fast hand/upper-body motion, temporary occlusion, crossing, and portrait/landscape orientation. Each current visible person must be represented by fresh current-image joints after discovery; record arrival/reacquisition latency and misses rather than counting empty frames as success. The first discovery target is ≤250 ms from a visible camera frame; any failure or ambiguity in this additional target is reported separately and cannot be hidden by the 30-FPS counter. The user retains final physical acceptance. If no interval passes, stop Milestone C and request a new architecture decision; do not start RTMO or relax the thresholds.

## 8. Review boundary

Approval of this document would authorize **writing a revised implementation plan**, not immediately changing production code. Until then Revision 2 and the current plan remain controlling: C2 is blocked and C3 is unauthorized. No main merge or Release occurs before the user's final Snapdragon 888 physical acceptance.
