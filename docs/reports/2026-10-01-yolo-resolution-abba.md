# YOLO-M3 bounded execution/resolution trial: actual Android ABBA

## Decision

The approved execution and intermediate-resolution trial is complete. The
576x352 local FP32 candidate is integrated and software-qualified, but is not
promoted to the default or physically accepted. Same-native repeated Unity
GPU-AHB video captures average18.5375 observed fresh frames/s for640x384 and
21.4875 for576x352. The measured mean difference is15.9137%; two short windows
per shape with uncontrolled GPU frequency do not establish a general sustained
gain. Neither reaches25; the25FPS input cannot certify30 fresh complete frames/s.
No new execution candidate was justified by Task1; Task2 explicitly closed it.
No further backend, model, precision or Tracker changes are authorized by this
trial. Accepted640 APK is restored and force-stopped after comparison.

Implementation: Task1 `713649d`, Task2 `b34699d` + `7aa3142`, Task3a `393d773`,
Task3b `180357c`. Each implementation/closure has independent spec/quality PASS
before the next Task. This report is Task4 root-owned actual-device evidence;
independent Task4 SpecPASS/QualityPASS recomputed raw records, both APKs/four
capture identities, thermal limits and99 evidence hashes. Review file:
`.superpowers/sdd/2026-10-01-android-yolo-execution-and-resolution/task4-review.md`.
No main merge or Release. Stable API/sync guards and existing R4 edits preserved.

## Controlled pair and source identity

Two new scratch Unity projects cloned the reviewed Task3b full stage. Both use
the same native/dependency bytes, ncnn FP32 default execution, Vulkan/API26/ARM64,
capacity8, video1 start37s, upright source/overlay transform, HUD visibility and
overlay3px/9px. A640 changes only the four selected runtime files/index and its
explicit shape/profile selection. Historical640 staging native allowlists stay
intact. No diagnostic timestamp/benchmark flag is enabled in these binaries.
Render60 is visible in both APKs; do not compare with older Render30 measurements.
The original open PC test project and its dirty scene were not staged or edited.

Native SHA256:
`60d847e993db2e8a446f5a6807240be95229c695e20a7040db21474cfab5cb42`.
The native was built from the actual tree including preserved R4 changes;
clean HEAD alone is not asserted to reproduce it. The Task3b freeze captures
149 artifacts and126 actual source files, reverified before staging.

| Artifact | SHA256 |
|---|---|
| A640 APK | `7c59f9e654b9fda593428aae9d6872f335070cf6b89f501b5e6e8b6d4d24720e` |
| B576 APK | `f53e864b8fdebf036d338045f6a06d38f53bbaf02d9963b2abc5e73091483694` |
| A640 raw index | `954b0fb5a152672b2242ccc14d49588be74e488000a110bbe7c91a022b143f00` |
| A640 Unity index | `c410d133aa23606ac3a34d848e403332d540da646a7b38d72cd0e5e37df4e0bf` |
| B576 raw index | `9b1c5b0f3b25c593b4b973ae023cdf3d56615f0fdc1084cdbab3f1a4ede7e46f` |
| B576 Unity index | `ca90ae5654c63d1f79d16f10e6b07bcde0f49def1905626d53fe9af2add08a95` |
| video1 MP4 | `e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8` |

Source video1024x576/25FPS. Weights are unchanged:
param `908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905`,
bin `6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9`.
Both APK ZIPs verify every selected runtime file/index, all seven ARM64 libraries
and the MP4 against staging receipts. Device installed base APK SHA and logged
VideoPlayer source SHA are checked on every run. No camera/source substitution.

Evidence base: `out/android-yolo/resolution-abba-20261001` (ignored, retained).
`stage-pair-receipt.json`, `apk-pair-receipt.json` and `summary.json` retain exact
file/dependency/source hashes, raw conditions and recomputed rows. Full captures
are in `out/android-yolo/eval-rectangle{640,576}-abba-20261001/device-<label>`.
They include PID-bound raw logs, capture report and screenshots0/15/30/45s.

## Measurement and result

Device e7c07019, OnePlus9Pro/Snapdragon888/Adreno660/Android14. Order A1 B1 B2 A2.
Each actual capture records60s after activation; exclude first10s and analyze
the fixed40s half-open interval `[activation+10, activation+50)`. Count unique,
monotonic source-frame ID and result-sequence pairs, including every partial or
empty observation; reject duplicates, resets, malformed records and mixed source.
Do not use held/predicted HUD FPS or sum persons. Separately report native fresh
counter delta over its actual first/last stats interval within the same window.
Age is the observed result-age field in those fresh snapshot records; it is not
a measured camera photon-to-display latency. Video scheduling and snapshot
sampling can differ from native result production.

| Run | Unique/40s | Fresh FPS | Exactly7 | Eight | Partial/empty | Age P50/P95 ms | Native fresh FPS |
|---|---:|---:|---:|---:|---:|---:|---:|
| A1 640 |751|18.775|748|3|0/0|98.535/117.786|18.792624|
| B1 576 |863|21.575|847|16|0/0|86.386/116.433|21.574374|
| B2 576 |856|21.400|840|16|0/0|81.508/100.277|21.399156|
| A2 640 |732|18.300|729|3|0/0|84.235/114.586|18.315944|

640 totals1483 observed/1477 exactly7/6 eight;576 totals1719/1687/32.
Exactly-seven subset rates average18.4625 vs21.0875, separate from complete
observation-frame FPS. Seven reported Bodies do not independently prove seven
correct identities, every joint or continuous arm fidelity. Eight records in a
seven-person source may indicate duplicate detections and need semantic review.
Snapshot inspection establishes upright actual source/overlays and visible
skeletons on unobscured persons only; the HUD obscures part of the right image.
It does not certify temporal fidelity or all seven persons. No accuracy-equivalent
default promotion based on counters or one frozen7/7 raised-arm gate.

All four windows: copy/import errors, pose-validation failures, pose drops,
bridge superseded and full-frame CPU readbacks are0. Bridge no-slot delta A1=3,
others0. PID-filtered captured logs contain no matched fatal/worker error.
These are capture-window results, not a long-duration stability certification.

Logcat SHA256 in run order:

- A1: `9b7ecab4ab1d2229f092f6c18b360b823044b45bf86202312c7766eb805c55c0`.
- B1: `1b05a1ff0780cd17328a533e6998e4dfad35d72ede8d741b5161721036e1d9c4`.
- B2: `6fb71fc32c867022a6463d6bcf854527603fe45268507603e170e535acd57909`.
- A2: `d4eab4ca776b43b4c2603b8bc986ab975bd00b9c76de262e99825dda4f578034`.

## Thermal/DVFS limits

Read only `Current temperatures from HAL`, never the stale cached section.
Raw battery/thermal/load/UTC/frequency attempts are saved before and after each
installation/capture (these timestamps span more than the analyzed40s window).
All thermal statuses and sampled GPU0/CPU7 sensor statuses are0. This alone
does not establish fixed clocks or absence of GPU frequency variation.

| Run | UTC before/after | GPU0 C before/after | CPU7 C before/after | Battery C before/after |
|---|---|---:|---:|---:|
| A1 |11:24:17.019/11:25:50.073|35.8/54.4|36.0/63.1|33.5/35.1|
| B1 |11:28:22.768/11:29:51.114|37.1/55.1|37.7/62.8|35.0/36.2|
| B2 |11:30:45.399/11:32:13.851|39.8/56.4|40.3/59.5|36.6/37.5|
| A2 |11:36:27.038/11:37:55.520|38.1/58.1|38.3/66.1|35.9/37.1|

Dates2026-10-01 UTC. GPU `cur_freq` access is Permission denied on every attempt;
no root/privilege workaround, DVFS lock or frequency-control claim. ABBA order
and identical APK dependency closure improve comparability; different initial
temperatures, gaps and only two repeats limit causal/sustained conclusions.

## Verification and restoration

Task3b software gates:163/163 focused native,115/115 YOLO Python,4/4 legacy
staging tests, architecture/public surface and1813-import API26 ARM64 audit PASS.
Task4 builds both actual scratch APKs with Unity2021.3.45f1 batch Android.
The selected Project Settings use explicit NCNN Vulkan, ARM64 and Vulkan graphics;
no fallback. Existing unused compatibility dependencies remain packaged.

Root commands (from this worktree):

```powershell
py -3.13 out/android-yolo/resolution-abba-20261001/stage_pair.py
# Both scratch projects then built using TopDownEvalBuild.Build:
# Unity.exe -batchmode -quit -force-d3d11 -projectPath <scratch>/UnityProject
# -buildTarget Android -executeMethod HumanVision.Editor.TopDownEvalBuild.Build
# -humanvisionTopDownEval -humanvisionTopDownVideo -logFile <scratch>/UnityProject/unity-abba-build.log
py -3.13 out/android-yolo/resolution-abba-20261001/verify_apk_pair.py
pwsh -NoProfile -File out/android-yolo/resolution-abba-20261001/capture_run.ps1 -Size 640 -Label abba-a1-640
pwsh -NoProfile -File out/android-yolo/resolution-abba-20261001/capture_run.ps1 -Size 576 -Label abba-b1-576
pwsh -NoProfile -File out/android-yolo/resolution-abba-20261001/capture_run.ps1 -Size 576 -Label abba-b2-576
pwsh -NoProfile -File out/android-yolo/resolution-abba-20261001/capture_run.ps1 -Size 640 -Label abba-a2-640
py -3.13 out/android-yolo/resolution-abba-20261001/summarize.py
pwsh -NoProfile -File out/android-yolo/resolution-abba-20261001/restore_accepted.ps1
```

Accepted APK
`out/android-yolo/eval-rectangle640-20261001-m3-armfix/humanvision-topdown.apk`,
SHA256 `9cc3b87f8b824fe9819b85d1163c15b0e4d1241e59e223d9dca099905710caae`.
The restore script verifies local and installed `base.apk` hashes, then force-stops
`com.blazetc.humanvision.topdowneval.i2c8.video`; receipt retained as
`accepted-restore-receipt.json`. Neither trial APK becomes the installed default.

## HD-input question and next bounded proposal

The current live-camera settings request1280x720/30. Higher capture/display
resolution can be requested independently of the model input. Actual dimensions
depend on camera support; Unity can choose the closest supported mode
([Unity2021.3 constructor contract](https://docs.unity3d.com/2021.3/Documentation/ScriptReference/WebCamTexture-ctor.html)). GPU-native
camera texture orientation and AHB copy/import still incur source-resolution
bandwidth/memory cost. Fixed model dimensions keep CNN input work unchanged;
downsampling does not preserve every extra source pixel. Increasing model input
can improve small/remote-person detail but adds compute and must be requalified.
No1080p camera comparison was measured here; video1 remains1024x576/25.
The existing RTSP decoder explicitly rejects NCNN GPU mode because it produces
CPU frames; this question does not qualify a new RTSP GPU-native route.

The bounded trial suggests another small resolution change alone is insufficient
to establish25/30 with unchanged accuracy. A next separately approved proposal
would evaluate one demonstrably lighter multi-person pose graph through unchanged
GPU/API contracts and frozen semantic gates, then identical >=30FPS-source tests.
Model replacement/training/backend work is not implemented by this report.
Current detector/pose convolution cost dominates the diagnostic GPU trace;
do not promise30 from extra threads, prediction or input-HD changes.
