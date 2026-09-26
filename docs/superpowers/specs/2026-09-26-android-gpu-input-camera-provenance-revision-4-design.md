# Android GPU input correctness and camera provenance — Revision 4

Status: **written spec approved by user on 2026-09-26; implementation plan awaits review**  
Date: 2026-09-26  
Base: `codex/android-ncnn-vulkan-implementation`, commits `4b5d426` and `9a95572`  
Scope: resume Milestone C through deterministic input correctness, same-image camera provenance, then integrated TopDown acceptance.

## 1. User outcome and decision

The SDK must recognize people and draw skeletons that follow them smoothly on
Snapdragon 888. The hard target remains 30 fresh **complete observation frames**
per second, with all current Bodies in each frame; body counts and repeated
rendering never multiply FPS. The user's latest confirmation authorizes this
written revision following the failed Revision 3 gate. It does not retrospectively
turn that gate into a pass.

Approve a sequential recovery inside Milestone C:

1. Prove byte-identical source selection and locate/fix the earliest divergence
   along Unity GPU texture → AHB → ncnn input → detector → pose.
2. Introduce a Camera2 GPU source whose image and sensor timestamp stay paired
   through inference and preview, with explicit device capability validation.
3. Validate actual skeleton presentation, then repeat unchanged single-person,
   two-person and thermal acceptance on the integrated Camera Demo.

This revision grants no new detector candidate, model substitution, RTMO, Hand,
QNN, MediaPipe, Windows or ORT performance work. No main merge or Release is
permitted before final user physical acceptance. A/B remain completed historical
milestones; changes affecting their invariants rerun their relevant gates before
Milestone C resumes. D remains blocked.

## 2. Evidence and uncertainty

[The failed integrated report](../../validation/ANDROID_NCNN_TOPDOWN_GATE.md)
records 1,137 fresh observations in 60 seconds (18.95/s), but zero Bodies while
a person was visible. This is **not 18.95 skeleton FPS**. The corrected video
diagnostic also yielded zero candidates, although the pinned reference detects
people in those videos. Its sparse input probe found an unexpected black row.
The probe and reference used nearby, not identical, decoded frames: the root
cause is not established. GPU copy geometry, format/color handling, import,
normalization/packing and model execution remain hypotheses.

The live source currently provides Unity-observed timestamps, not same-image
sensor capture timestamps. Small observed-to-publication durations cannot satisfy
sensor age. Existing facade Body slots do not prove rendered geometry. The open
Editor's missing runtime index is a separate recorded limitation and is not
evidence of Android correctness or a Windows work authorization.

## 3. Alternatives and selected direction

| Approach | Result and tradeoff |
|---|---|
| Fix deterministic GPU parity, then Camera2 provenance (selected) | Isolates current correctness failures before introducing a camera source; preserves the bridge/backend architecture and makes age measurable. |
| Change camera source immediately | Could hide the old failure and leaves the known video route unexplained; larger simultaneous debugging surface. |
| Keep WebCamTexture and estimate capture age | Smaller change, but cannot establish same-image sensor age and cannot pass the approved gate. |

The selected design changes input acquisition, not public skeleton semantics.

## 4. Rulings against existing designs

[Revision 2](2026-09-13-android-vulkan-ncnn-production-runtime-design.md) and
[Revision 3](2026-09-25-android-ncnn-detector-cadence-revision-3-proposal.md)
remain binding except for these explicit amendments:

- Revision 3's stop after failed intervals is replaced by permission to execute
  the recovery gates in this document **after spec and implementation-plan approval**.
- The NCNN production camera source changes from WebCamTexture to Camera2 image
  acquisition with matching capture metadata. Video texture input remains supported.
- A native camera-buffer adapter is added before the existing Unity RGBA texture
  producer. Camera-buffer import is separate from the fixed three bridge slots.
- Additive internal frame diagnostics carry camera session, sensor timestamp,
  timestamp domain, source identity and publication/presentation times. Preserve
  V1 ABI and public Unity skeleton API exactly.
- GPU diagnostic reductions and bounded sampled-value readouts are permitted in
  evaluation builds. Full source-image or full preprocessed-image CPU readback
  remains prohibited, including during diagnosis. Golden images/tensors may be
  uploaded from offline files. Normal model-output readback remains permitted.

All other constraints survive: same physical GPU/different VkDevice with UUID
matching; explicit Runtime Mode; NCNN fail-fast; sampled/read-only ncnn AHB
imports; sync-fd and ownership transfers; three cached bridge slots; no per-frame
AHB/import-pipeline creation; measured blit versus color-attachment support;
latest-frame/drop; Region after inference; current-image poses; bounded detector
cadence and stale-result rejection. No long joint prediction may mask low FPS.

## 5. Gate R4.1 — deterministic source and first-divergence diagnosis

### Fixture contract

Pin one full-person and one multi-person decoded frame from the supplied videos,
plus asymmetric RGB/color/corner and non-square grid fixtures. Record video hash,
decoder/version, exact frame index, decoded RGBA bytes/hash, dimensions, color
space, orientation, alpha, reference preprocessing and model/profile hashes.
Decode once offline and upload the exact lossless pixels to a GPU texture. Do not
compare independent video decodes, timestamps rounded to frame indices, or nearby
frames. This integrated Demo diagnostic is a correctness test, not FPS acceptance.

Exercise both supported copy paths independently when the target device supports
them; unsupported paths are explicitly skipped, never forced. Cover portrait and
landscape, rotations 0/90/180/270, front-preview mirroring, non-square letterbox,
and repeated slot reuse. Use the same production import and preprocessing code.

Injected-texture parity does not clear the failed VideoPlayer route. Before exit,
also run the production VideoPlayer → RenderTexture route on the pinned videos:
latch a frame-ready frame index and its GPU texture into an owned immutable
diagnostic texture before decoding advances. Perform a GPU comparison between
that exact latched texture and the bridge consumer/preprocessor output using
the declared transform. No nearby-frame estimate is allowed. Use annotated
people for that exact frame to verify native detections and poses. An offline
decode of the same index is a supplementary reference with decoder/color-space
differences recorded, not a claim of byte-identical source pixels. If the latched
decoder texture itself is wrong, isolate acquisition/color conversion there;
successful static injection cannot waive this production-route failure.

### Observable boundaries

Compare source GPU texture, producer RGBA result, consumer imported pixels,
normalized planar tensor, explicit dtype/packing conversion, raw detector outputs,
decoded boxes, and pose outputs in order. An evaluation-only GPU comparison pass
reads the actual resource and an uploaded golden resource, reduces all elements
to max/mean error, mismatch count and first-error coordinate. Only these scalar
results and a fixed small sample grid return to CPU. Shape, strides, channel order,
rotation, crop, padding, range, dtype and elempack are recorded separately.

Nearest-copy unorm8 fixtures must agree within one channel quantization step.
For normalized input, provisional strict limits are max absolute error 0.02 and
mean absolute error 0.002 across every element; analytic color/grid fixtures also
must match expected orientation, extent and padding exactly. These are proposed
design limits, not measured results. FP16 reference computation must model the
actual rounding/packing stages. Existing pinned detector/pose golden tolerances
remain authoritative for model outputs. No tolerance may be loosened after seeing
a failure without a documented numerical justification and review.

Prove the diagnostic fails on injected channel swap, vertical flip, truncated
copy, wrong stride, duplicate/old slot, wrong packing and wrong scale. GPU
comparison success alone is insufficient: every annotated real person in the
fixed fixtures must yield valid detector boxes and current-frame canonical poses.
Keep raw-output parity separate from post-threshold candidate counts.

Fix only the earliest demonstrated divergence, preserve the failing regression,
then rerun downstream comparisons. If input parity passes but ncnn output parity
fails, isolate the pinned network using the same uploaded reference tensor and
existing golden runner; do not rewrite converters, lower confidence thresholds or
substitute a model. After two evidence-backed fix/review attempts at a boundary
without restored parity, record the unresolved difference and request a targeted
decision rather than starting an open-ended conversion effort.

**Exit:** all fixture transforms and supported paths pass, real-person detector
and pose correctness restored, no CPU image readback, original A/B synchronization
regressions pass. Otherwise R4.2 does not start.

## 6. Gate R4.2 — Camera2 same-image GPU source

### Proposed route and capability gate

Camera2 capture → API-26 native ImageReader GPU-sampled PRIVATE buffer →
Unity-device sampled camera import → GPU conversion into reusable RGBA texture →
existing Unity render-event/three-slot AHB bridge → ncnn → observation.

Camera control can use Camera2 capture callbacks; image acquisition uses
`AImageReader_acquireLatestImageAsync`, `AImage_getHardwareBuffer` and image
timestamp metadata. This is an additive source adapter. It does not replace ncnn
with a camera-specific inference path. Preview consumes the same normalized RGBA
source texture/geometry contract; a second WebCamTexture stream is forbidden.
The SDK Project Settings Runtime Mode still selects `android-ncnn-vulkan`,
`android-ort-xnnpack` or `android-ort-cpu` explicitly. The NCNN profile declares
this camera source and its required capabilities; unsupported initialization
does not substitute WebCamTexture or another Runtime Mode. Video diagnostics use
an explicit source selection and cannot be reported as live-camera acceptance.

Before implementing the adapter beyond capability plumbing, query the selected
camera and actual buffers on Snapdragon 888: output sizes/FPS, timestamp source,
AHB format/usage, Vulkan external format, sampled-image support, YCbCr conversion
features and external-memory/fence/ownership requirements. PRIVATE is not assumed
to be RGBA or transfer-capable. Use queried YCbCr conversion/model/range/chroma
metadata when needed. Required Vulkan features/extensions must be negotiated
before Unity device creation. API 26 is the Android floor, not a guarantee that
every API-26 device has those GPU capabilities.

If the actual camera buffers cannot be sampled on the Unity Vulkan device with
correct synchronization, this source fails with a precise capability report.
Do not add an implicit GLES path, CPU conversion, direct-to-ncnn architectural
bypass or ORT fallback. Any alternative camera transport requires a new ruling.

### Pairing and clock contract

Maintain bounded per-session image/metadata maps (maximum eight entries each).
Match image timestamp to the exact capture result sensor timestamp and camera
identity; allow callback reordering. Hold an unmatched image no longer than
100 ms on a native worker, then drop/release it with a reason. Never match by
nearest arrival time. Session changes invalidate both maps. Each accepted image
gets one immutable source ID; sensor timestamp, capture metadata and transform
travel with that ID through copy, inference and first publication.

Only a verified REALTIME sensor domain supports absolute sensor-age acceptance.
Take publication time in the matching boot-time domain; retain nanoseconds until
report conversion. UNKNOWN timestamps can preserve ordering but must fail verified
age. No offset estimated from callback arrival may make UNKNOWN verified. Reject
negative ages and timestamp regressions. Store the clock domain/version in every
evidence set. Sensor timestamps represent exposure start, so age includes exposure.

### Lifetime and synchronization

ImageReader ownership is separate from the existing three-slot bridge:

1. Acquire latest image plus acquire fence; keep its AImage lease and metadata.
2. Import/cache the actual camera AHB for sampled reads on Unity's device. Wait
   the acquire fence on GPU and apply the required Android external/foreign queue
   ownership acquire, never a render-thread host wait.
3. GPU-convert/copy into a reserved reusable RGBA producer texture, recording the
   exact source transform once. The existing bridge then copies from that texture
   with its measured path and unchanged sync-fd/external ownership protocol.
4. Return the camera image only after the GPU camera read and required release
   ownership barrier complete. Use the documented image release-fence mechanism
   or completion confirmed on a worker, never early deletion based on a timestamp.
5. Reuse the RGBA producer texture only when every bridge copy/preview read that
   references it has completed. An AHB bridge slot still waits for ncnn's release
   fence as before. Track these two lifetimes independently.

All success, drop, fence-import failure and shutdown paths have one fd/lease owner.
No free producer/bridge slot means immediate drop without render-thread waiting.
Unmatched/superseded camera images are released safely without inference. Bound
application-held images to four, configure ImageReader `maxImages=6`, and call
acquire-latest only while fewer than four images are held. This retains at least
two reader acquisition slots for discard semantics; assert the invariant in tests.

Use three reusable RGBA producer textures and a bounded cache of at most eight
camera-buffer imports, keyed by live AHB object identity plus session and format
contract. Cache holds explicit AHB references and observes buffer removal. Removal
callbacks mark imports retired and prevent new leases; destruction of their
VkImage/View/conversion objects and release of the retained AHB reference happen
only after all in-flight GPU reads complete. Never
use a recycled pointer as identity after its lifetime ends. Warm the camera-buffer
cache before measurement; unexpected pool replacement retires/drains the old
generation and warms a new one, with a visible transition counter. It cannot create
Vulkan images/pipelines every measured frame. Pool growth beyond the cap fails
explicitly. The ncnn allocator/VkImageMat/import pipeline remain fixed per bridge
slot and rebuild only at generation changes.

Pause/resume, permission loss, camera switch, dimension/rotation/input-contract
changes stop admission, invalidate generation, drain outstanding reads/copies,
close fences and release images before destroying the reader or imported objects.
Old-generation results cannot update current Bodies. Portrait/landscape, crop,
sensor orientation and front-preview mirror are carried in one tested transform;
preview mirroring cannot silently reverse canonical anatomical left/right.

**Exit:** exact image/metadata pairing, verified clock, coherent preview/pose
geometry, no unbounded queues or hot-path resource creation, and device GPU
format/synchronization gate pass. Camera2 alone is not a 30-FPS pass.

## 7. Gate R4.3 — integrated skeleton and physical acceptance

First prove an actual rendered skeleton in HumanVisionCameraDemo: record source
ID/result sequence, Body IDs, accepted valid joints, emitted line/point geometry,
renderer visibility and presentation timestamp together. Capture independently
viewable on-device evidence. An enabled component, facade slot count or mesh count
alone cannot demonstrate alignment. Preserve the user's 24px lines/72px points.

Only after correctness and camera provenance pass, resume the bounded interval
2–6 sweep with actual valid current-image poses. Fix measured input/bridge/ncnn
TopDown scheduling costs within the approved architecture; no model replacement,
new backend or omitted people. Do not infer performance from empty observations.
Choose the shortest passing interval without hidden runtime adaptation.
Disable evaluation GPU comparison passes and sparse pixel probes for timed runs;
retain lightweight production provenance, error and timing counters. Record the
diagnostic switches in the APK manifest/evidence so diagnostic timing cannot be
mixed with the accepted production-path timing.

After five seconds warm-up, run 60-second one-person and two-person windows:

- At least 29.0 fresh complete observations/s in every rolling 10 seconds and
  29.5/s average; count each frame once and enforce annotated visible-person recall.
- Verified sensor-to-first-publication age P50 ≤75 ms, P95 ≤100 ms, maximum
  ≤250 ms outside explicitly logged transitions. Report publication-to-presentation
  delay separately; drawing a stale snapshot never increases fresh FPS.
- Combined bridge no-slot/superseded drops ≤1% of requests; zero copy/import errors.
- Same-frame valid poses for all accepted Bodies, stable IDs/Regions, entry/exit,
  crossing, occlusion recovery, fast motion and orientation changes. Preserve
  Revision 3 discovery ≤250 ms, stale-detection and crop/cadence bounds.
- No CPU image readback, backend drift, Vulkan errors, crash, ANR or persistent
  allocations. Log source, metadata, bridge, scheduling and pose drops separately.

Then run the 15-minute thermal window on the same accepted artifact/process with
the existing analyzer's identity and body correctness requirements. Archive APK,
native/model/profile/source hashes, device fingerprint, camera contract, PID,
timestamp domain, raw logs, visible-person annotations and presentation evidence.
The supplied videos supplement multi-person correctness but cannot replace live
sensor-age, camera/thermal or user visual acceptance. TopDown capacities 1/2 do not
claim completion of the eventual 1–8-person target.

If no interval meets these unchanged criteria, remain FAIL in Milestone C and
request another architecture decision with measured stage costs. Do not start D,
extend prediction, count empty results as recognized skeletons or relax thresholds.

## 8. Implementation and validation boundaries

The subsequent detailed plan must keep R4.1 → R4.2 → R4.3 and task-by-task commits
with fresh implementer, spec compliance and code-quality review. Existing Sol
medium preference applies; Astra is permitted where the user-authorized escalation
is needed. No task-level user confirmation is needed inside approved scope.

Likely affected modules: `runtime/gpu/android/` (camera adapter and producer),
`runtime/plugins/backend/ncnn/ncnn_preprocess.*` and `ncnn_android_session.*`
(only demonstrated parity fixes), `runtime/plugins/pipeline/simcc/` (provenance
and current-image correctness), Unity camera source/bridge bindings, and
`tools/test/` diagnostic/collection/analyzer helpers. Core stays Unity-independent;
public API stays stable. Camera buffer handling is isolated from pose orchestration.

Automated RED→GREEN tests cover deliberate parity corruptions, timestamp pairing
reordering/missing entries/overflow, clock mismatch, old generations, fd ownership,
image and producer leases, buffer removal/cache bounds, no-free-slot behavior,
orientation/mirror transforms, valid-body FPS accounting and fake renderer evidence.
Use existing native, architecture, Unity EditMode and Android ELF/build checks as
applicable. Build/install into the authorized Unity test project/device with
backups of overwritten project files. Device presence/cooperation is checked before
live capture; absence cannot be marked pass. Status updates retain historical FAIL.

## 9. Sources and review request

- [Android CaptureResult SENSOR_TIMESTAMP](https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_TIMESTAMP): same-capture image timestamps and REALTIME versus UNKNOWN domains.
- [Android NDK media API](https://developer.android.com/ndk/reference/group/media): API-26 asynchronous image acquisition, acquire fences, image hardware buffers and lifetime rules.
- Revision 2/3 and the integrated failure report linked above govern the acceptance
  numbers and preserved architecture. Proposed camera transport must still prove
  Vulkan/device support; documentation is not device evidence.

The user approved this written Revision 4 on 2026-09-26. Its detailed plan is
`../plans/2026-09-26-android-gpu-input-camera-revision-4.md` and awaits review.
This revision has made documentation changes only; no runtime behavior or prior
acceptance result is changed.
