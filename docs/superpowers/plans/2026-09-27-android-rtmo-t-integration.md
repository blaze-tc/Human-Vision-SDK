# Android RTMO-t integration: approved route adjustment

User authorization: 2026-09-27, following the recommendation to replace the
internal repeated per-person TopDown path with RTMO-t416 and retain GPU input,
Unity API, Tracker and region assignment. User replied "可以继续".
This authorizes the RTMO work below despite the earlier R4 exclusion; it does
not approve main merge, Release, Hand/QNN/MediaPipe/ORT/Windows optimization.
Minimum is 25 fresh complete observation frames/s; target remains30. All current
people must be included; empty/partial frames and repeated renders do not meet
that metric. Existing25fps video can test following at25, not30distinct frames.

Architecture remains the existing versioned Pipeline/Backend/ModelPack/Profile
extension design. API26 ARM64 Vulkan, matching physical-device identity, cached
AHB slots, external sync-fd and ownership transfer remain unchanged. Fail fast,
no ORT fallback, no full-frame/input-tensor CPU readback, no TTL/prediction cover.
Body17 measurements retain canonical semantics; unavailable joints stay invalid.

## Sequential execution and review

1. **RTMO-M1 model eligibility (failed eligibility; closure review pending):** inspect pinned official416 model;
   select bounded raw-output export if embedded ONNX postprocess is unsuitable;
   pin hashes/contracts; prove conversion and golden outputs using existing
   reference/ncnn tooling. 45-minute initial feasibility timebox, one primary
   route and one bounded repair. No infinite converter work. Stop with evidence
   on an unsupported route; no silent model switch. Independent spec/quality
   review before separate verified commit and next task.
2. **RTMO-M2 GPU pipeline adapter:** use existing GPU plugin API for one full-frame
   RTMO inference, model-specific geometry/decoder in pipeline, generic tensor
   backend, data-only separate ModelPack/Profile. Register at composition root,
   preserve public API and common services. Focused decoder/contract/lifetime
   regression tests and affected native build; independent review and commit.
3. **RTMO-M3 integrated device:** stage into authorized open Unity project; build
   and install on e7c07019; samevideo37s seven-person comparison. Measure fresh
   complete7-person FPS, partial/empty ratio, P50/P95 age, drops, real visible
   tracking and errors. Check source/frame identity and geometry for new BGR/
   resize contract. A compiled/imported model is not a hardware performance pass.
   If below25, report actual bottleneck before another change. User final physical
   acceptance remains required; no release/mainmerge.

## Preserved in-progress work

R4Task3 cropfusion evidence remains immutable (measured nativeec021d, APK927bd050).
Partial evaluation GPUoracle is frozen, tests10/10 and13/13 but not deviceaccepted.
Its shader currently assumes RTMDet320 RGB; it MUST NOT certify RTMO416 BGR.
Latest paritynativea94293d is not the performance baseline. Keep parity disabled
for initial RTMO timing, preserve partialwork, and adapt only contract-matched
correctness checks when model eligibility is known. No old task markedcomplete.

Ownership: fresh RTMO-M1 implementer owns new conversion/test files and ignored
out/android-rtmo artifacts. Root owns documentation, currentUnity and device.
Separate review precedes task completion; only explicit verified files staged.

## M1 outcome and bounded repair ruling

The corrected device runner executes both raw and DCC networks on Adreno660,
but execution success is not numerical correctness. All eight raw outputs contain
nonfinite values on the real single-person fixture; the finite DCC output differs
by up to382.339 image pixels on the valid person's keypoints. No RTMO SDK adapter
is authorized by this failed eligibility gate. Root allowed one final <=10minute
replacement of exactly seven static shape operations after correcting the runner
allocator bug; identical ONNX results did not establish ncnn numerical parity.
Conversion work stops here. Preserve the evidence; do not label RTMO universally
unsupported or infer the unproven cause of the numerical failure.

The previously proposed YOLOv8n-pose/ncnn alternative may now undergo a separate
bounded model eligibility task under the user's approval to continue toward
>=25 fresh complete multiplayer observations/s. This is a development candidate
selection, never a runtime fallback. Keep licensing/provenance explicit and do
not distribute new third-party artifacts as part of this evaluation.
