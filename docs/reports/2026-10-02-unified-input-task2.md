# Unified input Task2: Unity video and camera preview

2026-10-02. Base `242956f8d576d20adaeb81cadf9b92efb0ea5c0f` in the existing
`codex/android-ncnn-vulkan-implementation` worktree. Production snapshot and
tests pass; fresh spec compliance and scoped code quality reviews both PASS.
No main merge or Release. Existing unfinished R4 files and caches are preserved.

## Delivered scope

The independent `com.blazetc.humanvision.input` package now has VideoPlayer and
WebCamTexture sources, a reusable GPU orientation normalizer, and RawImage
preview. No model, recognition manager, native SDK or inference backend is
required. The public skeleton API and V1 ABI are untouched.

Output geometry uses actual decoded/camera texture dimensions. Rotation and
vertical/display mirroring are applied once; output is upright UnityBottomLeft.
Source observation, Unity publication and video PTS remain separate. Publication
time records Unity-side submission/publication, not a sensor exposure timestamp
or a claim that foreign native GPU work has completed. Gamma-project sampling
is tested; a separate Linear-project qualification remains outstanding.

Pause/resume, geometry changes and reopen change generation; frame IDs continue
increasing. Saved late video callbacks cannot publish or alter a paused/closed
source. Camera permission is asynchronous. Warmed publication and preview/poll
paths pass managed-allocation checks. Production normalization has no full-frame
CPU readback and preserves the caller's active render target.

## Ownership correction

The additional root requirement for an application fence around ordinary
Unity-owned raw texture normalization was withdrawn after an independent Astra
diagnosis. Unity 2021.3 CPUSynchronisation was unsupported and the experiment
prevented cleanup on the tested D3D11 route. Failed experiment receipts remain
recorded; they are superseded, not relabeled as successes.

Raw VideoPlayer/WebCamTexture shutdown uses main-thread Unity APIs after
invalidating callbacks/publication. Published outputs still retain every
external consumer SourceCopyLease until that consumer's real copy fence
completes. A persistent main-thread pump continues after source destruction.
Close never waits for inference. A lease protects lifetime, not immutable pixels.
Same-turn queued-copy/shutdown tests provide finite graphics evidence; they do
not prove every driver or qualify a native external image path. Task5–8 sync-fd,
ownership, native copy completion and copy/inference separation gates are intact.

## Fresh automated evidence

Commands run from this worktree with actual Unity 2021.3.45f1 and hidden,
child-only RunAsInvoker processes; no user Editor or global settings changed.

```powershell
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase UnitySources -Output out/input/task2-final-green
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -Output out/input/task2-final-core
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase UnitySources -GraphicsApi Direct3D12 -Output out/input/task2-final-d3d12
py -3.13 tools/maintenance/check_architecture_boundaries.py
```

| Gate | Actual receipt | Result |
| --- | --- | --- |
| D3D11 sources | `out/input/task2-final-green/20261002T0437567488362Z-38d596be887c463b8d3afe6e5891be42` | EditMode6/6, PlayMode12/12, exits0 |
| Core | `out/input/task2-final-core/20261002T0438409114274Z-aa7d20e3a2c24ee7942f6c6aab7847bc` | 11/11, exit0 |
| D3D12 sources | `out/input/task2-final-d3d12/20261002T0439151546135Z-dffaf2a68f424dc5b0e67818c72ee2da` | EditMode6/6, PlayMode12/12, exits0 |
| Runner correction | `out/input/task2-round1-green/20261002T0446517611374Z-1c9dc2ba186846aba17a063b82870059` | Core11/11, exit0; private-video access0 |
| Architecture | maintenance checker above | Public surface and architecture/documentation PASS, exit0 |

D3D12 reports no GraphicsFence support yet passes this Unity-only preview scope.
Tests cover all16 asymmetric rotation/mirror combinations, midtone encoding,
same-turn copies before shutdown, outstanding consumer ownership, late callbacks,
error/close/reopen, actual playback and warmed allocations. Final source logs
contain no active-RenderTexture release warning. The actual MP4 fixture requests
1920x1080 but publishes1024x576; metadata does not copy the requested geometry.

Initial compile RED and subsequent lifecycle, callback, render-state and runner
behavioral RED receipts are retained in the task ledger. The sole original
review P2 was the runner's unconditional private MP4 check. Validation now occurs
only in UnitySources. A guard reproduced the Core failure before the change,
then actual Core11/11 passed without accessing the MP4; UnitySources still rejects
the same forbidden fixture access. No user video was renamed or moved.

## Actual Android input-only probe

Frozen package source, fixture/build hashes, APK and raw device log:
`out/input/task2-device/final-20261002T0439379355107Z-b9545df1b1064315a6c63941e35dd5a8`.

Unity build PID93608 exited0 with Succeeded/errors0. Vulkan-only ARM64/API26 APK
SHA256 `A249ACCC0E07940698D4464DA8DFBCE825EDCC5A4D66D541FFD448A4DBDDAE22`
matches the installed APK on authorized OnePlus9Pro LE2120/Snapdragon888
`e7c07019`. Native payload contains only libunity, libil2cpp and libmain; no SDK,
ncnn/ORT libraries or model packs. The source snapshot's39 file hashes match the
current production package. The later runner-only fix does not change this APK.

Front camera requested1280x720@30 and actually published1280x720, rotation0,
UnityBottomLeft/InputMonotonic metadata. Pause/resume changed generation1→2.
First Close reached Stopped with892 observed frames and CurrentTexture cleared.
Reopen streamed generation3; final Close reached Stopped, texture cleared, with
1035 total observed frames. Camera source state and generation continuity pass
this bounded probe. No fixture failure, MissingReferenceException or active
texture release warning occurs in the raw device log.

Sampled publication rates, calculated from increasing frame IDs and publication
times within each generation, are30.065/30.074/30.149 frames/s over approximately
12/12/4-second sample spans. These are input publication measurements, not a
60-second sustained performance benchmark or fresh skeleton results. No image
semantic/user acceptance or sensor-age claim is made. Raw device-log SHA256:
`1B90D7C33ECE2D2378A71F7D8BEDD5A155A2952BC7E54E30D8CBDD06A669D638`.

## Remaining boundaries

Task3 follows the completed review gate and independent Task2 commit. RTSP,
SDK adapter/GPU migration, three packaged demos and clean-import acceptance are
later tasks. Existing camera skeleton inference around11FPS and the25/30 fresh
complete observation targets remain unpassed; this input delivery does not
close them. Native RTSP hardware gates and final physical acceptance remain open.
