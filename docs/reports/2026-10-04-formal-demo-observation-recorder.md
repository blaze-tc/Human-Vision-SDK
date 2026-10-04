# Additive formal Demo observation recorder

The earlier private Task11 harness changed adapter scheduling and lost native
snapshots while using an invalid whole-Tick clock bracket. Its measurements
remain invalid. This bounded correction observes the official Demo without
disabling Update, invoking Tick or replacing its source, model or renderer.

`tools/test/FormalDemoObservationRecorder.cs` subscribes synchronously to
`HumanVisionManager.ResultUpdated`. It deep-copies each delivered complete raw
body array (32 canonical,17 legacy and6 hand entries), source identity, frame,
timestamp and public counters into fixed buffers. Hands-disabled fields remain
as provided; no points are fabricated. Adjacent Unity/Input/Unity clock reads
exclude source polling, copying and inference. JSON and disk export occur after
the60s run; first10s are warmup and the half-open10-50s window contains40s.

The analyzer counts whole observations, including zero/partial body frames.
Native sequence gaps invalidate complete native coverage and cannot be
reconstructed. Descriptive retained delivery FPS counts only known complete
raw manager snapshots and is independent from age eligibility. Accepted-rate
and age calculations require coherent typed identities/timestamps/provenance;
invalid data cannot enter retained age percentiles. PTS is never a clock.

## Verified software evidence

Evidence root: `out/input/production-correction/formal-observation-correction/`.

- Original missing-implementation RED and subsequent review reproductions remain
  preserved. V1 had invalid retained ages and integer/provenance weaknesses;
  V2 fixed those but coupled retained delivery counts to age validity.
- V3 RED:26 tests,5 expected assertion failures. V3 GREEN:26/26 PASS.
- V3 independent numerical probes:10/10 PASS. Spec compliance and code quality
  PASS in `v3/independent-review.md`; all four owned hashes match frozen sources.
- `v2/managed-compile-final.log`: actual frozen public v13 Input/Runtime/Demo
  and separate observer assembly compile for Windows and Android. Recorder
  source is unchanged in V3, so that evidence remains applicable; no new V3
  managed compile is claimed.59 compilation inputs retain verified hashes.
- Broader reference suite:239 tests with17 missing ONNX/ORT dependency errors;
  this is excluded from passing evidence, preserved without altering unrelated
  tests or installing new dependencies.

Integration is an additive component on each official generated scene. The SDK,
input package, native libraries and model files are unchanged. Current formal
bare APK already installed with verified source/APK hashes shows seven upright
skeletons. Its appearance evidence is `out/input/formal-android-oct04-v13/`;
one screenshot does not certify motion fidelity or sustained tracking coverage.

## Actual Snapdragon888 Video run

The additive observer APK built successfully with the same formal scene builder,
API26/ARM64/IL2CPP/Vulkan settings, frozen packages, native libraries and models.
APK SHA256 is
`6e49568884eec09d659b871b8d234856b53e445bc2aa5a5c76c5c80337b207fe`;
length82,967,493 bytes. All14 compared native/runtime entries match the bare
APK. Installation succeeded; the pulled installed base.apk has the same hash.
No source, scheduling, renderer, model or backend change was made for collection.

Evidence: `out/input/formal-android-oct04-v13-observer/`. The original user's
video SHA256 is
`e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`.
The public Video settings use MaxBodies8, no regions, and line3/point9 reference
canvas units; display pixels scale with Canvas.scaleFactor. The original9/27
settings looked too thick on this phone and remain preserved separately.
Current user PC project settings were not changed.

The60.026581s run exported44,262,115 bytes/4,224 JSONL records after measurement.
Footer:922 observations,1,497 source publications,1,803 samples; no buffer
overflow, interrupted run, copy-invalid flag or hand-only notification. The
half-open10-50s analysis window contains627 complete manager observations:

| Metric | Actual result |
| --- | --- |
| Complete observation FPS |15.675 (627/40s) |
| Observed source publication FPS |25.0 |
| Publication-to-result receipt age P50/P95 |100.106/133.6298ms |
| Maximum adjacent clock bracket uncertainty |19us |
| Body-count histogram (count: frames) |0:18,1:181,2:12,3:1,4:1,5:2,6:74,7:338 |
| Public runtime drops / bridge drops / CPU readbacks |0/0/0 |
| Manager observation validity |VALID |
| Native complete coverage |INCONCLUSIVE; native fresh counter unavailable |
|30 fresh complete observations/s target |FAIL |

The video changes between single-person and multi-person content, so this is
not a continuous seven-person coverage test. Zero-body frames are included;
body count is never summed to inflate FPS. Public stage-statistics medians in
the same window are Pose60.554ms, Tracking2.579ms and Total63.536ms. These point
to pose processing as the dominant reported stage, without identifying its GPU
layer cause or proving a sustained optimization opportunity. The public native
inference_fps field reports0 despite actual delivered results; it is not used
as the complete observation FPS measurement.

Battery-reported temperature was40.1C before and42.9C after this run. These two
snapshots do not establish thermal sustainability. No screen recording was active
during measurement; an earlier diagnostic screenrecord failed and is excluded.
The initial manifest used an unsupported source_nominal_fps key; its analysis is
preserved but superseded by the explicit source_rate_hz25 manifest and fresh
video-analysis.json. The corrected analysis retains the25FPS source limitation.

## Explicit limits

Fixed buffers allocate roughly70-80MiB at MaxBodies8 before measurement. Actual
device export and manager-run validity have been exercised; comparison against
the bare app to quantify observer overhead remains unqualified. Public
ProcessedFrames means BodySequence, not native V2
FreshBodyFrames. Native GPU/import/readback/decoder counters are inaccessible
through the frozen public API and remain null/inconclusive. Public bridge
counter evidence is recorded separately. A valid manager measurement cannot
alone grant physical/native30FPS acceptance.

The public CPU path uses a different frame/clock mapping; formal age joins are
explicitly unsupported there. Sensor capture-to-display age, motion fidelity,
thermal sustainability and real camera/RTSP acceptance need separate evidence.
The original25FPS video cannot certify30 fresh complete observations/s. Task11
remains open; no main merge, Release or model/backend/performance change.
