# Android YOLO execution and intermediate-resolution trial

User authorized trying the proposed execution and resolution improvements on
2026-10-01. Base: 0af0b10. This is continuation of YOLO-M3, not a new backend
or a release. Revision 2/3/4 architecture and later explicit YOLO rulings remain
binding. Existing uncommitted R4 changes and all caches must be preserved.

## Global constraints

- GPT-6.1 Sol medium implementers and independent task reviewers; sequential
  implementation, task-local commits, review before advancing.
- Keep Vulkan/AHB GPU camera input, cached slots/imports, physical-device match,
  producer sync-fd wait, external ownership transfer and retirement intact.
- No full-frame CPU readback, ORT fallback, fake GPU copy, automatic precision
  change, held/predicted-frame FPS counting, Unity/C ABI or Renderer changes.
- Current accepted 640 FP32 model and default remain available and unchanged.
  No RTMO conversion retry, Hand, QNN, MediaPipe, Windows or ORT optimization.
- New candidates are explicit local evaluation only. Frozen numerical and
  semantic thresholds are not relaxed. No main merge or Release.
- Device installation/build is already authorized. Preserve current user PC
  project changes; inspect and back up before any Android staging. Root owns
  device use and Unity staging; workers do not install or operate Unity.
- Count unique complete observation frames, including partial/empty failures.
  Seven-body counts alone do not prove joint accuracy or identity. Report FPS,
  age P50/P95, body coverage, drops/errors and sustained/thermal conditions.
  Target30 remains; video1 is25FPS and cannot certify30.

### Task 1: Explain inference cost without changing inference behavior

Timebox investigation to one bounded diagnostic implementation (45 minutes of
active investigation); do not cycle blind tuning. Read stage timing report and
pinned ncnn source. Establish a trustworthy way to separate layer GPU work from
internal ncnn submission/wait overhead in the actual production Run route.
Prefer supported official timestamp instrumentation where usable; otherwise
prepare a hash-checked, diagnostic-only source-copy patch with unchanged
dispatch/wait decisions. Keep benchmark/trace flags OFF in normal builds.
Do not edit cached upstream source in place, dependency pins or binary allowlists.
Do not claim per-layer timing if the VkMat extractor does not collect it.

Own new tools under tools/test/ncnn_execution_* and corresponding focused tests,
plus a bounded diagnostic backend/CMake hook only if strictly necessary. Dirty
existing files require a baseline patch and selective staging; do not commit
preexisting R4 edits. Include actual failure/rejection tests for patch drift,
missing/invalid timestamps and misleading aggregation. Build affected Android
targets with API26 ARM64 audit. Write docs/reports/2026-10-01-yolo-execution-trial.md
with commands and exact identities. Root runs one actual Unity diagnostic capture
after code review. Offline fixture execution may establish correctness only,
never a substitute standalone performance benchmark.

### Task 2: One evidence-selected execution candidate or explicit closure

After Task1 review and actual capture, select at most one safe execution change
supported by the measured dominant cost and official ncnn implementation.
Preserve FP32, model weights,640 input, outputs and every synchronization guard.
No threshold manipulation or removal of internal safety waits. If no defensible
change exists within this scope, record closure and proceed to Task3, rather than
inventing an optimization. If a candidate exists, use explicit local ModelPack
options, run unchanged numerical/semantic gates, build/audit and review before
Root measures actual Unity seven-person throughput. Do not default-promote on
uncontrolled comparisons. Own only selected backend/tool/test files declared
in the task brief; Root records measurements and restores accepted APK.

### Task 3: Intermediate-resolution eligibility and integration

Start with576x320 FP32, same pinned YOLO weights and official decoder. Derive
aspect-preserving letterbox/stride32 geometry explicitly for1024x576 video;
never force stretch to make16:9 assertions pass. If existing supported geometry
requires another padded stride32 height, record an explicit ruling before use.
Generate fresh seven/single/empty fixtures and include seven raised-arm frame1500.
Actual Snapdragon GPU outputs must pass existing CPU/GPU numerical and semantic
limits. Historical fixture/evidence hashes remain unchanged.

After eligibility review, integrate one explicit new input contract below the
stable API, with ModelPack/runner/staging evidence closure and fail-closed shape
validation. Preserve640/512 and all original contracts and gates. New tests must
reject wrong geometry, tampered evidence and unqualified kernel combinations.
Task-owned files: resolution tooling, GPU preprocess/input validation, YOLO raw
contract and focused tests. No Tracker/smoothing changes to hide deficiencies.
If eligibility fails, close without integration. Record exact evidence in
docs/reports/2026-10-01-yolo-rectangle576-trial.md.

### Task 4: Actual integrated comparison and trial conclusion

Root stages/builds/installs reviewed artifacts using backed-up current-project
workflow or isolated scratch project if current PC project must remain intact.
Run comparable warmed repeated640/candidate segments from video1 at37seconds,
same capacity8, orientation, HUD visibility and conditions, preferably ABBA.
Record temperature/load and identities, include every unique observation,
partial/empty results, errors/readbacks, age, coverage and actual screenshots.
Do not claim causality without adequate controls, or temporal fidelity from one
snapshot. Run a >=30FPS source/camera only if source/person cooperation available;
otherwise clearly leave30FPS unverified. Restore accepted APK and editor settings
unless candidate is explicitly accepted by user. Independent facts/quality review,
update DEVELOPMENT_STATUS with measured outcome and commit only owned verified
changes. If neither trial reaches target, give actual measured improvement/limits
and next bounded model-level proposal without implementing unapproved backends.

## Execution record and rulings

Tasks1/2/3a/3b independently reviewed PASS before advancement; commits713649d,
b34699d/7aa3142,393d773,180357c. Task2 execution-candidate investigation closed
without a speculative implementation. Existing geometry requires576x352 for
1024x576 source:576x324 resize plus14 rows top/bottom.576x320 would crop/stretch;
the Task3 allowed explicit geometry ruling is used instead.

Task4 root actual same-native ABBA is complete, independently SpecPASS/QualityPASS:
640 mean18.5375FPS,576 mean21.4875FPS, target25/30 unmet. Captured25FPS video
cannot certify30.576 remains local evaluation; accepted640 APK9cc3b87f restored
and installed hash verified. Original PC test project unchanged. Full identities,
raw logs/thermal/load, limitations and next proposal are in
`docs/reports/2026-10-01-yolo-resolution-abba.md`. No default promotion, main merge
or Release. This bounded plan is complete; overall Android30FPS goal remains open.
