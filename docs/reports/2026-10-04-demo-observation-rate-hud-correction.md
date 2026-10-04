# Formal Demo observation-rate HUD correction

2026-10-04. Bounded Task11 diagnostic correction; spec compliance and code quality
review PASS. Task11 physical acceptance and 30 fresh complete observations/s
remain open. This change measures manager result delivery rather than reporting
the native aggregate `InferenceFps` field as fresh complete skeleton FPS.

## Result and scope

The Demo subscribes to public `ResultUpdated` notifications and counts each known
new body sequence once over actual monotonic elapsed windows. Empty and partial
body frames count as whole observations; body totals and sequence gaps are not
added to the rate. Duplicate hand notifications do not increment it. Idle,
source epoch, readiness, stop, and manager lifecycle transitions reset the
window. A known first notification following an input transition is retained;
held callbacks from a replaced manager are rejected.

HUD labels now distinguish **Manager raw result delivery FPS**, **Native reported
inference FPS (coverage unqualified)**, preview refresh, and unavailable output
sampling/render FPS. The 30 FPS unaccepted statement remains. Manager delivery
alone does not certify joint accuracy, complete native coverage, sustained
physical throughput, or motion following.

Only the paired canonical/UPM canvas, scalar helper, tests and new metadata are
changed. No native library, model, backend, input scheduling, public API, GPU
copy, region assignment, skeleton drawing or layout change is included.
Recurring paths add no delegate or frame-buffer allocation; binding allocates
the cached handler only on manager identity/lifecycle changes.

## Verification

Evidence directory:
`.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/demo-observation-rate-correction/`.

- Actual isolated Unity old-HUD RED: native zero and the old mislabeled text fail
  the corrected expectation. An earlier missing ScreenCapture module compile
  failure was preserved and excluded from regression evidence.
- Review found first-event loss on source transitions. Two actual Unity tests
  failed on v1, and a retained old-manager callback test also failed on v1.
- Final actual Unity EditMode run: **11/11 passed, zero failed or skipped** in
  `out/input/production-correction/demo-rate-red-project/round1-green-results.xml`.
- Canonical and UPM actual C# arithmetic runners each passed 6/6. Both copies
  compiled for Windows and Android against frozen v13 Input/Runtime; existing
  serialization warnings remain. No compiler warnings-zero claim is made.
- Independent final review: `round1/re-review.md`, Spec PASS / Quality PASS;
  all ten owned file hashes and all five pairs independently verified.

Final canvas SHA256:
`cf8bbaed2b207af9bc4abb2bf62d74aa3ac16eccc797c1c8abf012b3e529f134`.
Helper: `0932e3a4d3d923e9beb555b2e95d08277e6a052b2f311222d09feb369d9c648c`.
Tests: `2b61797f47a57f1aa5067cd6202267e5df6424802828d29b86afc8b689773e37`.

## Actual current Unity import

Qualified local SDK v14 plus unchanged Input v13 was imported into
`E:/UnityProject/Human-Vision-SDK-Test`. The 425-file composition has six SDK
file differences from v13: canvas, four new helper/test metadata and source
files, and the regenerated asset hash index. Runtime index, models and native
libraries are unchanged. Local composition is not a published Git-import
package closure and does not authorize a release.

Actual real Video1 Play Mode HUD (`user-import-v14/actual-hud.txt`):
`windows-pc-directml`, `rtmo-t-416`, raw/sampled/capacity 3/3/4,
four enabled regions; manager delivery 23.97 FPS, native reported 24.64 FPS,
preview 24.83 FPS, local age 93 ms. Console errors were zero; the scene bytes
and existing region/capacity settings were preserved. These are a HUD
functional snapshot on a 25 FPS source, not a PC performance acceptance run.

The prior same-native Android measurements remain 15.675 and 15.9 complete
manager observations/s, **30 FPS FAIL**, with native coverage INCONCLUSIVE.
An updated Android build and Camera/RTSP physical validation are subsequent
gates. No speed improvement follows from this display correction.
