# Task7: Android RTSP GPU lifecycle verified (2026-10-02)

Task-scoped fresh review and fix-round review pass after two acceptance-tool
fixes. Input-only physical evidence is distinct from SDK skeleton acceptance.
The retained final hardware run uses native2810d88a and APKfa44f59d; only the
runner/analyzer and their focused tests changed in the review fix. Current
stronger analyzer re-evaluated the exact unchanged hardware log separately.
No new GPU/device run is claimed for those tool-only fixes.

Actual Snapdragon888:60.004s streaming,10 lifecycle transitions,10 actual consumer
GPU copies,6 closes overlapping conversion,1466 balanced converter completions/
release exports/ownership pairs,65 imports/destroys,32 target views/destroys,
terminal resource/error/readback counts0. Positive releaseFD holds/transfers1466,
peak1/final0; positive acquireFD hardware remains unqualified. Native36/36,
Core12/12,existing camera/video regressions6+12,API26 closure and architecture pass.
Fix tests: actual owned-child cleanup4/4 and signed terminal accounting7/7 pass.

The temporary diagnostic APK relies on an owned PC publisher and USB reverse,
both removed at cleanup. Reopening it is not a standalone playback/skeleton Demo.
Visible preview and accurate pending/error status exist; final three mode Demos
remain Task10. No30 fresh skeleton FPS, API26-28 physical codec support, final
physical acceptance, main merge or Release is certified. Physical source proof is
API29+ Snapdragon888; input plugin native minimum remains API26.

## Original implementation evidence

# Task7 implementation report - frozen for root review

Implemented only Task7 on `codex/android-ncnn-vulkan-implementation`, base
`4d1823d770eb8add241cb26f56b9c2347476746a`. Root owns plan/status/public report,
review/index/commit. No staging, commit, push, merge, release, SDK/Task8 edits,
subagents or user Unity-project changes were performed. The 34 protected
preexisting paths excluding root-owned DEVELOPMENT_STATUS match their original
hashes. Source and raw failed runs are retained. Task7 is ready for fresh review;
positive acquire-fd hardware remains unqualified, as explicitly permitted in the
brief rather than represented by mocked hardware evidence.

## Owned paths

- `native/input/README.md`
- `native/input/CMakeLists.txt`
- `native/input/include/android_input_gpu.h`
- `native/input/include/android_input_vulkan.h`
- `native/input/src/input_session.cpp`
- `native/input/src/input_frame_ring.h`
- `native/input/src/input_gpu_sync.h`
- `native/input/src/android/input_frame_ring.cpp`
- `native/input/src/android/input_gpu_sync.cpp`
- `native/input/src/android/input_vulkan_private.h`
- `native/input/src/android/mediacodec_source.cpp`
- `native/input/src/android/unity_input_vulkan.cpp`
- `tests/input/test_input_frame_ring.cpp`
- `tests/input/test_input_gpu_sync.cpp`
- `upm/com.blazetc.humanvision.input/Runtime/AndroidRtspGpuSource.cs`
- `upm/com.blazetc.humanvision.input/Runtime/AndroidRtspGpuSource.cs.meta`
- `upm/com.blazetc.humanvision.input/Runtime/NativeInputBindings.cs`
- `upm/com.blazetc.humanvision.input/Runtime/RtspFrameSource.cs`
- `upm/com.blazetc.humanvision.input/Runtime/VideoFrameSource.cs`
- `upm/com.blazetc.humanvision.input/Runtime/SourceRetirement.cs`
- `upm/com.blazetc.humanvision.input/Tests/EditMode/FrameContractTests.cs`
- `upm/com.blazetc.humanvision.input/Tests/PlayMode/AndroidInputLifecycleProbe.cs`
- `upm/com.blazetc.humanvision.input/Tests/PlayMode/AndroidInputLifecycleProbe.cs.meta`
- `tools/test/AndroidInputCapabilityBuild.cs`
- `tools/test/analyze_android_input_gate.py`
- `tools/test/collect_android_input_gate.ps1`
- `tools/test/run_input_tests.ps1`

All additions beyond named plan files were declared to the controller and
approved: support headers/build wiring, direct Android source/interop plus .meta,
virtual existing base accessors, internal pending-copy bookkeeping, focused tests,
private actual consumer-copy fixture support, runner and maintenance README.
Stable Task3 V1 C layouts and exported signatures are unchanged; GPU APIs are
additive Android-only. The SDK V1 ABI and protected unfinished R4 code are intact.

## Implementation and rulings

- Three fixed Unity RT output slots implement Free -> Acquired -> CopyQueued ->
  Published -> Retiring -> Free. One reusable converter command/fence is
  serialized; this does not claim three simultaneous GPU copies. Native render
  events poll actual fence status without queue/device idle or inference waits.
- AHB import cache remains independent of slots and keyed by actual buffer,
  generation and import contract. Source leases retain the AImage/AHB through
  actual converter completion. Buffer removal/reader teardown/close retire
  imports only after completion. Pending decoded data is latest-only with counted
  drops. No decoded CPU image mapping/copy/readback or inference dependency exists.
- Extended GPU Poll atomically returns/observes exact latest sequence/generation/
  slot. Observed slots stay held until managed source-copy fences finish. Only
  superseded never-observed publications retire automatically as drops. Invalid
  sequence observations/releases cannot affect a different slot. Generic V1
  PollFrame reads completed metadata only and never acquires output ownership.
- Managed RtspFrameSource directly forwards Android frames/leases to the three
  slots, avoiding an extra normalizer output. Existing video/camera signatures
  are only made virtual. Unity initializes each new RT through rendering before
  native binding; native submission uses actual AccessTexture followed by
  serialized AccessQueue(flush=true). Flush/enqueue is never GPU completion.
- Close prohibits new publication, requests cancellation and continues actual
  GPU retirement through persistent managed pumping after disable/destroy.
  Native joins only an already-completed worker. Unity RT destruction also waits
  consumer source-copy leases. App pause closes; resume waits retirement before
  reopening. Initialization and GPU failures retain actionable Error/LastError
  on subsequent ticks. No source close waits inference.
- Compressed application backlog retains one packet, while reader/decoder/output
  storage is bounded. Healthy admitted P/B access units are preserved. Decoder
  pressure or transport failure destroys/recreates RTSP/decoder/reader resources,
  then requires a keyframe. This controlled reconnect is the approved recovery
  alternative to codec-only flush; startup timeout remains bounded and explicit.
- InputGpuSync uses the real Vulkan import/export backend in production. A
  successful import consumes only a duplicate fd; failure closes the duplicate
  and preserves original acquire ownership for AImage return. FOREIGN acquire/
  return and actual release export remain in queued commands. Owned-fd accounting
  includes original acquire plus positive exported release fds, and decreases
  only on actual close/transfer to AImage_deleteAsync. It is plugin-owned sync-fd
  accounting, not an OS-wide fd audit.
- The tested Unity2021 player cannot query AsyncQueueSynchronisation
  GraphicsFence.passed. Private device-fixture consumer support therefore uses
  actual Unity source/destination RT access, VkCmdCopyImage on serialized Unity
  queue, and an actual Vulkan completion fence. Its fixed RT/command/fence are
  reused; command pool/fence explicitly retire at test end. Transactional handle
  creation ignores undefined failed outputs; canceled unsubmitted work is not
  counted as a successful GPU copy. Production source never calls these private
  diagnostic functions. This is not the SDK adapter or Task8 implementation.

## TDD and exact commands

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Windows -RunTests
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -TestFilter HumanVision.Input.Tests.FrameContractTests.EveryOutstandingCopyMustCompleteBeforeResourceRetires -Output out/input/task7-consumer-managed-red
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -TestFilter HumanVision.Input.Tests.FrameContractTests.AndroidInitializationFailureRemainsActionable -Output out/input/task7-init-error-red
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -Output out/input/task7-managed-final-core
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase UnitySources -Output out/input/task7-unity-sources
py -3.13 tools/maintenance/check_architecture_boundaries.py
pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate Lifecycle -Serial e7c07019 -Output out/input/task7-device
```

Each RED used runnable native/managed code, not a missing-type/build failure.
The original expected missing-module fixture failure is separately retained.

| Evidence | Observed result |
| --- | --- |
| `out/input/task7-native-red.log` | 4/33 FAIL: no-free-slot returned reused slot; Close lost live queued slot; reconnection published old generation; backlog admitted non-keyframe. |
| `out/input/task7-sync-red.log` | 5/34 FAIL including real helper's missing positive fd import/ownership handling; unaffected29 baseline tests PASS. |
| `out/input/task7-protocol-green.log` | 34/34 PASS after actual ring/backlog/sync implementation. |
| `out/input/task7-consumer-red.log` | 1/35 FAIL: published consumer slot could not release after correct sequence; wrong sequence stays held. |
| `out/input/task7-consumer-managed-red/20261002T1050419036419Z-2754e8bc4a38435da31313e31782b339/EditMode-results.xml` | 1/1 FAIL: pending-copy bookkeeping incorrectly false while queued/unqueued leases hold the actual texture. |
| `out/input/task7-publication-red.log` | 1/36 FAIL: superseded never-observed Published slot leaked; regression captures actual device-discovered capacity exhaustion. |
| `out/input/task7-init-error-red/20261002T1103370728955Z-bafb6ec8d0d64a9d9eaf04eaa85d2623/EditMode-results.xml` | 1/1 FAIL: initialization failure reverted to Stopped on following Tick rather than Error with Vulkan reason. |
| `out/input/task7-managed-final-core/20261002T1105101567790Z-3460a125128b42439577608e5e4a3926/summary.json` | 12/12 PASS including actionable initialization failure and consumer-copy ownership. |
| `out/input/task7-unity-sources/20261002T1054286712052Z-244ff2e917474d898b283c30feccad46/summary.json` | 6/6 EditMode +12/12 PlayMode PASS for existing video/camera normalization, callbacks, retirement and allocation regressions. |
| `out/input/task7-native-fd-final.log` | Final API26/ARM64 build/strong-symbol dependency closure and actual Windows PE36/36 CTest PASS. |
| `out/input/task7-architecture-final-frozen.log` | Public surface contract PASS; architecture/documentation boundaries PASS. |

Host executables are actual Windows PE under `out/input-native/windows`, not
Android ELF run on Windows. Existing locked-NDK CMake deprecation warnings remain
visible; no compiler upgrade or suppression occurred. Generated color shader,
SPIRV and locked compiler provenance remain part of the successful build receipt.
This task does not claim four-matrix color golden runs repeated on the new binary;
Task6 archived color/crop qualification remains separate from Task7 lifecycle.

## Retained actual failures and intermediate evidence

- `out/input/task7-native-first-build.log`: retained native declaration/order
  compilation errors, fixed before any qualified native result.
- `out/input/task7-device/20261002T1042394544923Z`: isolated Unity fixture build
  FAIL because reused source base required built-in Audio/Video modules; runner
  manifest corrected. No physical success from that compile failure.
- `out/input/task7-device/20261002T1043421225034Z`: preliminary production
  publication ran60s but consumer fence query threw NotSupportedException.
  Whole lifecycle FAIL: missing terminal counters/retirement/ten transitions/
  overlapping closes/consumer copies. Never narrowed to only the exception.
- `out/input/task7-device/20261002T1054090780288Z`: actual visible production
  screenshot, controlled disconnect/reconnect, actual pause/resume and60.010s /
  1269 observed input frames, followed by real three-slot pin timeout. Entire
  lifecycle FAIL; superseded-unobserved slot leak fixed with native RED above.
- `out/input/task7-device/20261002T1101402483188Z`: earlier native5d22a913 passed
  its archived lifecycle requirements:1475 submits/completes,63 imports/destroys,
  33 target views/destroys,10 transitions,6 overlaps,7 old-generation suppressions.
  It is preliminary PASS under that exact analyzer, not current fd gate proof.
- `out/input/task7-device/20261002T1108006440951Z`: native1af8c1cc completed real
  60.006s,10 transitions/consumer copies,8 overlaps and private copy-resource
  retirement. Stronger analysis FAIL solely because inherited held-fd counter
  omitted exported release fds. Original startup analysis snapshot and actual
  stronger executed snapshot are both retained; `analysis-tool-identity.json`
  explains exact versions. Original result/source-artifact receipt bytes remain
  valid. `analyze_android_input_gate.executed.py` reproduces the recorded FAIL.
- Final admission freezes native, managed, runner/build and analyzer sources.
  Final analysis executes the copied analyzer from the run archive, avoiding
  mutable repo-tool evaluation while running. No source changed after admission.

## Final actual device outcome

Final archive: `out/input/task7-device/20261002T1111585811869Z`.
Full exact facts are in `task7-final-device-facts.json` beside this report.

Native SHA256:
`2810d88a05b50084dfcb0a42684e66cb2eed8319d341fbb5e78bde6c829f9ffe`.
APK and independently pulled installed APK SHA256:
`fa44f59d43921c9df5722fb4062a4bb42be4b105862f012f41337e94473f2d0a`.

- Actual Streaming playback accumulated60.004s. The ten transitions are four
  disables, three closes and three destroys/recreated sources; owned action trace
  additionally records publisher disconnect/restart and actual HOME app pause/
  resume. These are input observations, not skeleton/inference results.
- Three distinct actual published textures are pinned simultaneously; subsequent
  publication does not overwrite them. Native drops are observable. Consumer
  leases then release and live preview resumes.
- Ten actual VkCmdCopyImage submissions/completions use logged actual source/
  destination image, source sequence and generation. Each close holds the source
  lease until its actual GPU completion. Six closes overlap queued native GPU
  conversion; all source workers/outputs retire. Eight closing/old-generation
  conversions are suppressed instead of published.
- Converter submits/completes/release exports/FOREIGN acquire+return are1466/1466.
  Imports/source views/pipelines/descriptor pools create/destroy65/65. Target views
  create/destroy32/32. Errors=0, cache_live=0, slots_live=0, CPU image readbacks=0.
  Private consumer command pool/fence explicitly retire to0 after10/10 copies.
- Actual positive release-fd held records1466, AImage transfer records1466; owned
  fd held peak1 and final0, without negative values. All actual acquire fds=-1
  already complete, positive acquire waits0. Positive acquire-fd import/wait
  hardware is unqualified; production positive/error fd branches have meaningful
  native helper tests, not represented as physical waits.
- Final slot drops38 and decoded pending/drop count77 are reported, never treated
  as completed frame success or skeleton FPS. Terminal observed input frames1443.
- `live-production-preview.png` captures the visible production RTSP frame and
  accurate Streaming HUD. Screenshot uses Android screencap only, not production
  decoded CPU readback. All APK native entries match locked source libraries;
  no SDK/Runtime Host/ncnn/ORT native library or model is in this input-only APK.
- Controlled publisher, log collector and USB reverse mapping are owned and
  removed; app is force-stopped in runner finally. The phone's reusable standalone
  RTSP sample is still Task10. Reopening this temporary fixture alone afterward
  cannot play a removed source and is not sold as a usable skeleton demo.

## Self-review, qualifications and freeze

Self-review found/fixed actual unobserved slot supersession, persistent cold error
state, transactional diagnostic resource ownership/retirement, and acquire+release
fd accounting. Main-thread object updates, real queue serialization, real fence
polling, original fd preservation, independent cache lifetime, protected files,
no hot-path image CPU copies and no inference dependency were inspected. Saturated
and live copied slots are covered separately; a host backend test is not a GPU
success claim. Root still owns fresh spec/quality review and commit.

Limits: tested real hardware is Snapdragon888 `e7c07019` with API29+ codec identity
query; native closure is API26/ARM64, not physical qualification on an API26 phone.
Positive acquire-fd hardware and actual Vulkan device loss were not physically
exercised. Decoder input-buffer overload uses tested keyframe policy + the same
controlled reconnect code; the device runner proves disconnect recovery, not an
injected real hardware decoder-pressure event. Transfer/primaries remain unknown
where reliable metadata is absent. No30 fresh skeleton FPS, sensor latency,
physical final acceptance, SDK adapter, model/renderer optimization or Tasks8-11
completion is claimed.

Frozen artifacts:

- `task7-admission-source-sha256.json` and `task7-owned-source-sha256.json`: exact27
  owned source hashes, unchanged from final admission through terminal proof.
- `task7-full-source.zip` + `task7-full-source-sha256.json`:105 input source/support
  paths, including unchanged dependencies needed to inspect/rebuild this boundary.
- `task7-artifact-sha256.json`: raw RED/failed/current gates, copied source tools,
  APK/native/API26/host verification, receipts and report hashes (excludes mutable
  Unity project caches).
- `task7-final-device-facts.json`: recomputed current real counters, fd pairs,
  APK/native hashes and source admission checks. The34 original protected hashes
  excluding root-owned status match baseline.


## Preserved artifact correction

# Task7 artifact freeze correction

Original task7-report.md, task7-artifact-sha256.json and original run receipts are preserved unchanged; preserved copies and exact evidence are in task7-freeze-correction/.

Exactly one of455 original map entries changed: historical110800 device-logcat-stream.txt. Its first10608080 bytes hash exactly to the originally asserted661958a6cddfafd0c7362e6ff79ad4ff90432daa63dfb695b562e7b0974c3ce0. It appended716032 bytes, now11324112 bytes with SHA256 ace7d7b3bdb7b20f3e8aebfd330eb4a26a4e804509defb89316ca7b80a3095c7. Exact matching prefix and appended suffix are retained separately; original stream is not truncated. Appended timestamps19:20:52.385–19:28:55.847 are after original frozen last line19:20:47.504 and after historical gate cleanup19:10:33.874. This stream captured unrelated later device activity after the gate; immutable device-logcat.txt used by gate analysis never changed.

Cleanup defect: StartOwned stored exe=null for adb logcat PID50392/startTicks639265361428205108; later live.Path nonnull made retirement/finally identity guards skip termination. Fresh inspection verified exact startup ticks and serial e7c07019/logcat command before Stop-Process -Force and WaitForExit. Before/after process evidence is retained. Runner source remains unchanged; this concrete cleanup risk needs fresh review and a covering fix through RED/GREEN.

Final111158 run remains qualified by original receipts: all455 entries except historical stream match; copied analyzer and all existing final artifact hashes match result.json. All five final owned process IDs absent; no adb logcat collector remains. Final and historical stream/analysis hashes were stable across5 seconds. All27 owned source hashes unchanged. No production source edits, testing reruns, index or commit.

Derived corrected455-entry map: task7-artifact-sha256.corrected.json (SHA2560297e508ee6ce19a6acd646373c1d0b181d1c5b455962db8ae010e5a944b8aa5). Detailed correction receipt: task7-freeze-correction/correction-receipt.json (SHA25663699dec7dd3042ebf835929c3f0df9fd02dd6e7b51424fac2498407e18e42fd). Original report does not incorporate this addendum; review both.


## Acceptance-tool fix evidence

# Task7 fresh-review fix round1

Addresses only the two Important findings in task7-review.md. Owned changes: tools/test/collect_android_input_gate.ps1 and analyze_android_input_gate.py; added input_gate_owned_process.ps1, test_input_gate_owned_process.ps1 and test_analyze_android_input_gate.py. Root approved these three helper/test additions before edits. Production native/Unity25 owned files unchanged; protected34 unchanged. No index/commit/Task8.

Preservation: pre-edit-snapshot.zip and pre-edit-sha256.json retain original source27, all old Task7 maps/report/source zip/review and correction originals. Old manifests and hardware receipts untouched. New owned-source-sha256.json has30 paths; full-source.zip/full-source-sha256.json have108 paths. round1.diff is exact fivefile patch against pre-edit bytes (new files against empty).

Focused TDD commands (from isolated worktree):

    pwsh -NoProfile -File tools/test/test_input_gate_owned_process.ps1
    py -3.13 tools/test/test_analyze_android_input_gate.py

Raw first attempts retained process-red.log/analyzer-red.log: real process launch-path observability initially also affected normal-case fixture; UTF8 strict decode failed on real adb log bytes. Corrected fixture waits for real child live.Path to populate and decodes using the production analyzer replacement behavior. Meaningful RED is process-red-behavior.log: initial capturedPath=null/later populated fails exactly1/4; mismatched startup ticks and path are untouched; normal cleanup passes. Shared helper initially implements original collector process.Path capture and identity comparison, then receives minimal fix. Analyzer-red-behavior.log:5/7 behavioral failures (final decoder leak after earlier zero, unmatched release hold, negative count, transfer without hold, duplicate held descriptor); balanced real archive and reuse pass. No missingmodule failure represented as behavioralRED.

GREEN logs process-green.log and analyzer-green.log:4/4 actual child-process cases and7/7 analyzer cases PASS. Test-owned child processes are forcibly cleaned by fixture finally even when assertions fail. Valid initial null-path child and normal child are actually terminated and waited, repeat cleanup recognizes absence; deliberate startupTicks/path mismatch raises explicit identity-mismatch error and preserves live child. Test never kills mismatched identities through production helper. No mocked process termination.

Shared process helper captures resolved launched executable independently of initial process.Path, retains exact UTC startup ticks, validates current executable path (CIM fallback if current Path unavailable), rejects identity mismatch, stops exact owned process and waits at most5sec for actual exit. Collector normal retirement, deliberate owned publisher disconnect and finally all invoke this same tested helper. Finally writes cleanup.status/errors and throws if any process cleanup failed; stream freeze follows successful collector termination. No silent skip on null initial path. Runner PowerShell AST parse PASS after changes; git diff --check PASS (existing CRLF normalization warnings retained).

Analyzer parses signed accounting fields, rejects any negative active image/AHB/fd value, requires last decoder closure and final per-resource records to be zero, tracks every positive release hold/transfer with descriptor reuse, rejects duplicate hold/unmatched transfer, requires terminal held set empty, reconciles total holds with final GPU release_exports and observed peak with outstanding release peak. Positive acquire ownership can contribute to active_owned_fds; observed total must never be below release descriptors still owned. Repeated descriptor reuse fixture rewrites actual serial balanced records to same descriptor while preserving1466 exports and terminal baseline; it passes. No presence-only accounting remains.

Stronger current analyzer re-evaluation (separate copy, original hardware files untouched):

    py -3.13 tools/test/analyze_android_input_gate.py .superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/task7-round1/final-archive-reanalysis

Result PASS:1466 holds/1466 transfers, release peak1/owned peak1, terminal releasefds0. final-archive-reanalysis/reanalysis-identity.json binds exact original device-logcat.txt bytes, current analyzer SHA and stronger analysis SHA. This is re-analysis of original actual111158 hardware evidence, not a new hardware run or rerun under changed collector. Native/Unity suites and60s gate need no rerun because only acceptance tools changed; actual child test proves cleanup with OS processes, exact preserved raw hardware log proves stronger accounting. Hardware positive acquirefd remains unqualified and API26 build does not imply physical API26 codec support.

Self-review: all Stop-Process calls in collector moved through shared helper; normal/final/disconnect use same exact ownership decision and wait. Counter baseline uses terminal records rather than earlier clean reconnect. Original scope/freeze intact; no toolkit upgrade or broad refactor. Remaining qualifications unchanged from original report/review. Artifacts below frozen after successful focused checks.


## Final formatting receipt

After clean review, removed exactly one extra EOF CRLF from input_frame_ring.h and
test_input_gpu_sync.cpp; one terminal CRLF retained. Original bytes/maps/builds
are preserved. Root checked exact before=after+CRLF equivalence; executable tokens
unchanged. Derived final-owned-source-sha256.json changes only these2 entries.
No native rebuild or additional device/test run is claimed for EOF-only formatting.
