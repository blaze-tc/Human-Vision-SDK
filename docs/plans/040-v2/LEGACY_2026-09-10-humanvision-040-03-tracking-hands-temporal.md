# Human-Vision-SDK 0.4 Tracking, Hands, Canonical Skeleton and Temporal Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make identity, region assignment, hand extension and game-facing skeleton behavior independent of whichever body model/backend is active.

**Architecture:** Pipelines produce anonymous `BodyObservation`s. A common tracker performs global association and lifecycle management; a hand scheduler attaches real hand observations; a canonical mapper converts model-native schemas to a fixed Kinect/Azure-like hierarchy; a bounded temporal output stage publishes low-latency 60 Hz state without pretending that interpolation is new inference.

**Tech Stack:** C++17, Kalman/constant-velocity state, Hungarian assignment, RTMPose Hand21, deterministic unit tests.

**Spec:** `docs/SDK_040_REALTIME_MULTIPERSON_DESIGN.md`

## Global Constraints

- Track IDs cannot depend on RTMO/RTMPose internal IDs.
- Region mode is a tracker constraint, not a Unity-side reordering hack.
- Real hand extension may be stale/invalid independently of body joints.
- Predicted output has a bounded horizon and age; stale people disappear.
- Raw observations remain available for diagnostics.

---

### Task 1: Replace center/IoU greedy tracker with a common observation tracker

**Files:**
- Modify: `native/src/tracking/i_body_tracker.h`
- Create: `native/src/tracking/humanvision_tracker.h`
- Create: `native/src/tracking/humanvision_tracker.cpp`
- Create: `native/src/tracking/hungarian_assignment.h`
- Create: `native/src/tracking/hungarian_assignment.cpp`
- Create: `native/src/tracking/track_motion_filter.h`
- Create: `native/src/tracking/track_motion_filter.cpp`
- Keep: `native/src/tracking/center_iou_tracker.*` as legacy regression only until migration completes.
- Modify: `tests/native/test_tracker.cpp`

**Interfaces:**
- `TrackInput { bbox, body_keypoints, confidence, timestamp_us, candidate_region }`.
- `TrackedBodyObservation { track_id, region_index, lifecycle, observation }`.
- Lifecycle: Tentative, Confirmed, Lost, Expired.

- [ ] **Step 1: Add failing deterministic tests for two people crossing, temporary overlap, short disappearance, delayed old observation, re-entry after expiry and region-locked crossing.**
- [ ] **Step 2: Implement constant-velocity prediction for bbox center/scale.**
- [ ] **Step 3: Implement association cost combining gated IoU, center distance and normalized keypoint/OKS-like similarity; incompatible regions get infinite cost.**
- [ ] **Step 4: Implement Hungarian minimum-cost global assignment; prohibit greedy per-body matching in multi-candidate case.**
- [ ] **Step 5: Implement lifecycle ages and delayed-observation rejection by timestamp/revision.**
- [ ] **Step 6: Run tracker tests until all scenarios pass.**
- [ ] **Step 7: Commit.**

### Task 2: Move region ownership fully into tracker/runtime

**Files:**
- Modify: `native/src/tracking/humanvision_tracker.*`
- Modify: `native/src/core/humanvision_engine.cpp`
- Modify: `native/src/core/result_snapshot_store.*`
- Modify: `tests/native/test_tracker.cpp`
- Modify: `tests/native/test_c_api.cpp`

**Interfaces:**
- Region revision travels with observations/configuration.
- A confirmed valid track retains its slot unless it exits/invalidation criteria are met.

- [ ] **Step 1: Add tests ensuring two detections never publish to one region slot and a late detector result from an old region revision cannot mutate current assignments.**
- [ ] **Step 2: Integrate current region semantics into tracker scoring/lifecycle.**
- [ ] **Step 3: Publish region index directly with canonical body snapshot, keeping legacy assignment getter compatible.**
- [ ] **Step 4: Run C API/tracker/result-store tests.**
- [ ] **Step 5: Commit.**

### Task 3: Add RTMPose Hand21 adapter and asynchronous hand scheduler

**Files:**
- Create: `native/src/models/rtmpose/hand21_adapter.h`
- Create: `native/src/models/rtmpose/hand21_adapter.cpp`
- Create: `native/src/hands/hand_scheduler.h`
- Create: `native/src/hands/hand_scheduler.cpp`
- Create: `native/src/hands/hand_roi.h`
- Create: `native/src/hands/hand_roi.cpp`
- Create: `tests/native/test_hand_scheduler.cpp`
- Create: `tests/native/test_hand_adapter.cpp`
- Modify: model-pack `hands` manifest.

**Interfaces:**
- Scheduler input: confirmed tracked body/wrist/elbow observations plus frame/timestamp.
- Scheduler output: `HandObservation { track_id, side, landmarks[21], confidence, timestamp_us }`.
- Defaults: hand target 15 Hz; invalid/small/off-frame ROI is skipped; body processing never waits for hand result.

- [ ] **Step 1: Add hand adapter golden test for 21 landmarks and source-coordinate ROI inverse transform.**
- [ ] **Step 2: Add scheduler tests for target cadence, 16-hand fairness, fast-motion priority, invalid ROI skipping and stale result rejection.**
- [ ] **Step 3: Implement left/right hand ROI generation from wrist/elbow direction plus bounded expansion.**
- [ ] **Step 4: Implement a bounded latest-request scheduler with round-robin fairness across active hands; do not create an unbounded queue.**
- [ ] **Step 5: Implement real `Hand`, `HandTip=index fingertip`, `Thumb=thumb fingertip` mapping metadata; mark unavailable data invalid.**
- [ ] **Step 6: Run hand tests.**
- [ ] **Step 7: Commit.**

### Task 4: Implement model-independent canonical skeleton mapper

**Files:**
- Create: `native/src/skeleton/skeleton_mapper.h`
- Create: `native/src/skeleton/skeleton_mapper.cpp`
- Create: `native/src/skeleton/schema_adapters.h`
- Create: `native/src/skeleton/schema_adapters.cpp`
- Create: `tests/native/test_skeleton_mapper.cpp`

**Interfaces:**
- Input schemas: RTMO17, Body26, LegacyWholeBody133 plus optional Hand21 attachment.
- Output: fixed `CanonicalSkeletonObservation` only.

- [ ] **Step 1: Write mapper tests with equivalent synthetic poses encoded in all three body schemas; canonical shoulder/elbow/hip/knee/ankle positions must match within tolerance.**
- [ ] **Step 2: Implement direct joint mappings for true observed body joints.**
- [ ] **Step 3: Derive pelvis/spine/clavicle center joints only from valid high-confidence endpoint pairs; propagate conservative confidence and derived flag.**
- [ ] **Step 4: Attach hand values only from fresh Hand21 observations; body wrist alone is insufficient to mark HandTip/Thumb valid.**
- [ ] **Step 5: Add legacy 23-joint projection from canonical state so old Unity accessors remain functional.**
- [ ] **Step 6: Run mapper and C API tests.**
- [ ] **Step 7: Commit.**

### Task 5: Add bounded low-latency temporal output filter

**Files:**
- Create: `native/src/temporal/temporal_skeleton_filter.h`
- Create: `native/src/temporal/temporal_skeleton_filter.cpp`
- Create: `native/src/temporal/one_euro_filter.h`
- Create: `native/src/temporal/one_euro_filter.cpp`
- Create: `tests/native/test_temporal_skeleton_filter.cpp`

**Interfaces:**
- `PushObservation(track_id, skeleton, observation_time_us)`.
- `Sample(track_id, now_us, output)` produces visual/game state using newest observation, estimated velocity, 10–25 ms bounded prediction and adaptive smoothing.
- Output reports `observation_age_ms` and `prediction_ms` separately.

- [ ] **Step 1: Add tests for stationary jitter, constant velocity, sudden direction reversal, 30 Hz observations sampled at 60 Hz, stale timeout and track loss.**
- [ ] **Step 2: Implement per-joint velocity estimation from timestamped real observations.**
- [ ] **Step 3: Implement One-Euro/adaptive filtering without adding multi-frame lag buffers.**
- [ ] **Step 4: Clamp prediction horizon to config maximum and stop publishing once stale/person timeout is exceeded.**
- [ ] **Step 5: Ensure sampling does not increment raw inference/result sequence; raw and game-facing rates remain distinct stats.**
- [ ] **Step 6: Run temporal tests.**
- [ ] **Step 7: Commit.**

### Task 6: Integrate tracker, hands, mapper and temporal output into engine snapshots

**Files:**
- Modify: `native/src/core/humanvision_engine.h`
- Modify: `native/src/core/humanvision_engine.cpp`
- Modify: `native/src/core/result_snapshot_store.*`
- Modify: `native/src/core/stats_collector.*`
- Modify: `tests/native/test_result_snapshot_store.cpp`
- Modify: `tests/native/test_c_api.cpp`

**Interfaces:**
- Engine order: pipeline -> common tracker -> hand attachment -> canonical mapper -> raw snapshot -> temporal sampler/game-facing snapshot.
- Body pipeline may publish at its own rate; hand result can update extension independently; Unity getters always receive coherent per-snapshot data with timestamps.

- [ ] **Step 1: Add integration test with fake 30 Hz body observations, 15 Hz hands and 60 Hz samples. Verify stable IDs, no fabricated hands and bounded ages.**
- [ ] **Step 2: Wire stages without blocking pipeline thread on hand worker.**
- [ ] **Step 3: Extend stats with raw skeleton FPS, output Hz, result age and prediction horizon.**
- [ ] **Step 4: Run the complete native suite.**
- [ ] **Step 5: Commit.**
