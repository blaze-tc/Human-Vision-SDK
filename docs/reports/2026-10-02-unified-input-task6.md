# Unified-input Task6: Android Vulkan import and GPU color conversion

2026-10-02. Base `a60a3fec460e80d6343fb294a49896785a3651e3`.
Review and commit status are recorded in DEVELOPMENT_STATUS and the SDD ledger.

## Implemented boundary

The independent input plugin imports actual MediaCodec PRIVATE AHardwareBuffers
as sampled/read-only Vulkan images on Unity's successfully created device. A
fixed16-entry cache owns per-buffer memory, view, sampler conversion, pipeline and
descriptors. Actual submitted-fence tokens control retirement; buffer/generation/
contract changes invalidate resources. Unity-owned RGBA8 targets are accessed
through Unity AccessTexture, and native GPU work is submitted through serialized
AccessQueue. Actual target format/usage/features are checked. Unsupported contracts
fail explicitly. No Surface/OES handle is cast to VkImage, and no decoded image
plane, AHB lock, CPU RGBA staging, recognition backend or model is involved.

Before sampling, physically supported FOREIGN ownership support is explicitly
enabled and confirmed on the successful Unity device. Actual acquire-fd import/
GPU wait, FOREIGN acquire/return, release-fd export, completion polling and image
return are implemented. The target device only supplied fd=-1, already complete;
positive-fd waits have not been physically qualified. This minimum synchronization
was necessary for Task6 sampling; the full three-slot source/publication/reconnect
protocol remains Task7.

Reliable bounded H264 SPS/VUI metadata resolves matrix/range independently of the
vendor's ambiguous color-standard3, which remains Unknown. Unspecified primaries/
transfer remain unspecified. Unsupported known matrix declarations reject rather
than guessing. Actual MediaCodec coded crop is validated against imported extent
and visible dimensions; AImage's full-window rectangle is not added as an origin.
The optional API28 rectangle getter is dynamically resolved, with API26 integer
keys as fallback; absent reliable crop declarations explicitly fail admission.
Startup waits boundedly for an actual first keyframe and preserves subsequent P/B
packets. Shader source, SPIRV and locked NDK compiler provenance are retained.

## Actual evidence and versions

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate Color -Serial e7c07019 -Output out/input/task6-device -ColorMatrix 709 -ColorRange Limited
pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate StartupTimeout -Serial e7c07019 -Output out/input/task6-timeout
pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate CleanupFault -Serial e7c07019 -Output out/input/task6-round1-cleanup
pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate ViewFailure -Serial e7c07019 -Output out/input/task6-round1-view
py -3.13 tools/maintenance/check_architecture_boundaries.py
```

Original color-golden native SHA256:
`798c805c5eb40205e7aea2ea59ea5ec1644c1fbb7767c309de5f6907c699e760`.

| Run under out/input/task6-device | Contract | Cases | Points | Maximum RGB error |
|---|---|---:|---:|---:|
| 20261002T0913439232573Z | BT.709 Limited, nonzero crop | 8 | 64 | 0.01961 |
| 20261002T0915270723256Z | BT.709 Full | 8 | 48 | 0.01961 |
| 20261002T0917024244550Z | BT.709 Limited | 8 | 48 | 0.01176 |
| 20261002T0918401128891Z | BT.601 Full | 8 | 48 | 0.01176 |
| 20261002T0920147680371Z | BT.601 Limited | 8 | 48 | 0.00784 |

All40 transformations/256 measured points PASS. Real submitted/completed counts
match (201/201,201/201,202/202,200/200,204/204); per-buffer imports/destructions
match13/13 or14/14, target views8/8 and all per-buffer resources retire. Cache,
unexpected errors and production CPU image readbacks are0. Actual imported AHB
is640x368, ordinary visible640x360; strong cropped visible624x344 originates8,8.
Actual externalFormat506 and Unity RGBA target format37/storage features1047939
come from queries. These are target observations, not assumptions for all devices.

Real no-keyframe source `out/input/task6-timeout/20261002T0933421859139Z` produces
the configured5000ms timeout, admits no GPU work and releases all decoded resources.
The actual phone screenshot displays the error. Earlier WAITING/prelaunch and
wrong-color/crop/crash/source-generation failures remain archived as failures.

Current reviewed repair native SHA256:
`ab8667400f8ad67363ba50e61ed51b1d3e8804fa963bc0ff6decc14a49be311f`.
API26 strong-symbol/dependency audit and actual Windows PE29/29 tests PASS;
affected cache/view-owner XML4/4 PASS. The pinned NDK emits external CMake
deprecation warnings; no compiler upgrade or warning suppression was made.

Independent review found three error-path defects: synchronous diagnostic shutdown
could join a worker waiting on render-event retirement; a derived session was
deleted through a nonvirtual base owner; failed view replacement retained a
destroyed handle. The fix uses yielding shutdown/retirement before joining an
already completed worker, an accurately typed opaque owner, and transactional
view replacement which ignores undefined failure output and returns the pending
lease. Retained textures live until actual target retirement.

Real CleanupFault RED `out/input/task6-round1-red/20261002T0953075815385Z`
submits actual GPU work then fails to finish cleanup. GREEN
`out/input/task6-round1-cleanup/20261002T0957209831453Z` retires the same active
lease over4 yielding frames, joins the completed worker and balances every
import/view/pipeline/descriptor/submission/return1/1, with errors/cache/readbacks0.
Real ViewFailure `out/input/task6-round1-view/20261002T1004319238017Z` retires an
old target, injects one declared view-create failure, retains no invalid owner,
returns that pending lease, retries successfully and passes8 transformations/
48 actual reference pixels (maximum error0.0078408). Submissions/completions203/203,
imports13/13 and target views9/9 are balanced. The exact one diagnostic error is
classified explicitly; ordinary Color still requires zero errors. The earlier
capture/checker failure is retained, and continuous owned log capture preserves
the complete metadata. Driver failure is deliberately injected, not claimed as
an observed real out-of-memory event.

The repair preserves shader/color/crop/metadata bytes; new-binary coverage targets
changed ownership/shutdown plus normal transforms. The original four-matrix/crop
results are not represented as tests rerun on ab866. Each archive retains its
actual native, APK native entry, installed APK identity and copied test sources.
Root independently checks29 current source/166 repair artifact hashes, retained
27 original source/250 original artifact hashes, and34 protected R4 originals.
Architecture/public-surface checks PASS. User Unity project and caches remain.

## Visible behavior and limits

The input-only diagnostic now shows the actual GPU target, pending/error state
and an explicitly labeled static diagnostic snapshot after completion. The test
runner owns a temporary H264/TCP publisher and USB reverse mapping; cleanup stops
them. Reopening that diagnostic APK afterward cannot play the removed source.
It is not the final independently usable Camera/Video/RTSP skeleton Demo.

Task7 production slots, backpressure, reconnect/pause/close and device-loss gates
remain outstanding; SDK source-copy retirement/adapter and final packages/demos
remain Tasks8-11. No usable production SDK RTSP publication, skeleton accuracy,
sensor age, sustained thermal behavior or25/30 fresh observation FPS is certified
by this diagnostic. A25FPS fixture cannot prove30FPS; prior raw camera skeleton
about11FPS remains below target. No main merge, Release or final physical acceptance.

Post-review delivery formatting: removed one redundant EOF newline from
`tests/input/test_h264_color.cpp` to satisfy Git whitespace checking. The retained
review snapshot and receipt verify identical executable tokens; no behavior
changed and no additional behavior-test result is claimed. Original review and
compiled-artifact hashes remain preserved separately.
