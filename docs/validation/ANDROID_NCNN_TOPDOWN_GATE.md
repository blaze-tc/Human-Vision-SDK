# Android ncnn/Vulkan integrated TopDown gate — FAIL (2026-09-26)

Revision 3 Task 10 remains an end-to-end **FAIL**, not a physical acceptance.
The dedicated Development/IL2CPP/ARM64/API-26/Vulkan `HumanVisionCameraDemo`
APK used the real local RTMDet Nano/RTMPose Body26 ModelPack and the production
AHB/Vulkan/ncnn route, with no `HV_ANDROID_GPU_GATE`, ORT fallback, or
full-frame CPU source readback. The attached OnePlus 9 Pro (`LE2120`, Snapdragon
888, serial `e7c07019`) passed APK/PID/hash binding. The user's one-person
framing was visible in the front-camera preview during the measured window.
The native pipeline nevertheless published zero Bodies, and its fresh complete
observation rate was below target. `WebCamTexture` supplies only a Unity-observed
timestamp, so verified sensor capture age is unavailable. No candidate met
provisional gates; therefore the two-person and 15-minute thermal acceptance
windows were not run. No interval or capacity is selected, and Milestone C
must not advance to RTMO, Hand, or release.

## Reproduction and bound evidence

The scripts are `tools/test/build_android_topdown_eval.ps1`,
`collect_android_topdown_gate.ps1`, `test_android_topdown_gate_analysis.ps1`,
and `android_topdown_gate_analysis.py`. The build regenerates an ignored local
pack/profile per requested interval, stages a fresh isolated Unity project,
audits the ARM64 ELF/API-26/ncnn static link and APK dependency closure, and
builds both the Camera Demo and Camera Settings scenes. The collector derives
the package from the APK, verifies its installed `base.apk` SHA-256 even with
`-SkipInstall`, binds logcat to the launched PID, starts the 5-second warm-up
at the first submitted source frame, and retains raw log/analysis under ignored
`out/android-topdown-eval/`. These scripts do not publish weights.

For the actual 60-second one-person capacity-1 run at interval 2:

| Bound item | SHA-256 |
| --- | --- |
| APK | `4ed73c29b6bc7d9890b92fdc7bb4d7681bcc0badb26f2faf2cb4143ae75bfc72` |
| profile | `4fca84520514ac6ec3e80f7204bf988b6abb037b24518a32660761361b56d63a` |
| ModelPack | `5ad92d4be5ea24505d82cc8d5fc50b4603f76d8e462311ce5f9d9f31d5479a97` |
| detector weights | `4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da` |
| body weights | `0f8a0a864be7af7990366bfb8ce89d4fca911a08b967c00e3b8180725d5054a4` |
| `libhumanvision.so` | `a6f51f21bde461f13900f2fa37d3a7e326ec13b379aef0606a1d9e08f15379a8` |

The ignored evidence folder is
`out/android-topdown-eval/interval-2-capacity-1/device-one-person-physical/`.
Its `measured-humanvision-topdown.apk`, `measured-hashes.json`, `report.json`,
`evidence.json`, `analysis.json`, raw PID log and post-window screenshot are
preserved locally. The process was PID 15626 and stayed the same through the
window; package `com.blazetc.humanvision.topdowneval.i2c1`, selected device
`Camera 1` front-facing, configured/runtime capacity 1, Region count 1,
actual backend ncnn Vulkan and AHB Vulkan copy path. The live camera's
observed timestamp provenance was `UNITY_OBSERVED`.

| 60-second steady-state measure | Observed | Required |
| --- | ---: | ---: |
| distinct fresh observations | 1,137, or 18.95/s | 30/s with Revision 2 clock tolerance |
| minimum rolling 10-second rate | 18.3/s | 29/s |
| native Bodies / facade body slots while one person visible | 0 / 0 | 1 / 1 continuously after discovery |
| rendered skeleton mesh bodies | unavailable | 1 when native body is current |
| detector attempts / completions at end | 1,157 / 1,156 | valid age/gap and discovery |
| pose validation failures | 12 | no visible-person loss |
| new no-free-slot / superseded-ready drops after warm-up | 0 / 0 | at most 1% combined |
| GPU copy / import errors; full-frame CPU readbacks | 0 / 0; 0 | 0; 0 |
| sensor capture age P50 / P95 | unavailable / unavailable | at most 75 / 100 ms |

The probe's former `drawn` label came from
`HumanVisionCameraManager.GetUsersCount()`, which counts facade slots. It does
not inspect `HumanVisionSkeletonGraphic` mesh vertices or prove rendering.
The summarizer now names it `facade_slot_count`, leaves
`rendered_body_count` unavailable, and marks renderer evidence unverified.
Presentation is therefore a separate fail-closed item. With no native Body,
the missing skeleton cannot be attributed solely to the renderer.

The last sampled Unity-observed-to-publication lower bound was P50 21.713 ms
and P95 24.584 ms; these numbers **do not** satisfy sensor capture age. Exact
maximum detector capture gap and new-track detector age were unavailable from
the existing snapshots, so the analyzer fails both rather than substituting
sampled detector age. Thermal evidence is absent by design after the
provisional failure. The physical user entry/exit, occlusion, crossing,
rotation and final acceptance remain unverified.

## Correctness diagnosis after the failed window

The first detector run produced ncnn rank-2 `[2100,1]` and `[2100,4]` views,
where the decoder only accepted rank-3 `[1,2100,C]`. A RED exact-shape
regression preceded the rank-2 fix, including RTMPose `[26,384]` and
`[26,512]` outputs; successful detector completions on device confirmed the
repair. A separate RED non-square test exposed a second input contract
mismatch: the model declares bilinear letterbox with RGB114 padding, but the
GPU path stretched 1280×720 to 320². The repaired GPU transform gives a
320×180 content area and 70-pixel top/bottom pad; the detector decoder now
uses the matching inverse. Pose `bbox_affine` remains separate. Native tests
for crop/interpolation/pad validation and non-square inverse are green.

These repairs did not yield visible-person detection. A 15-second physical
upper-body rerun and a 15-second user-reported full-body portrait rerun both
produced zero detector candidates, zero Bodies and about 19 fresh observations
per second. The full-body run's measured source was 720×1280 with verified
left/right RGB114 letterbox padding and changing camera pixels. Its detector
logit maxima remained roughly -2 to -3, below the fixed 0.35 score threshold.
The screenshot captured after that run is **not** proof of its in-window
framing: camera orientation changed from 270° to 180° after collection.
The user's in-window framing report is retained as the annotation. A
trace-only 5×5×3 GPU readout from the already-normalized input established
live, changing pixel values; it never downloaded the source frame or full
320² input. The pinned ncnn AHB importer multiplies sampled normalized Vulkan
texture values by 255 before producing planar RGB, so no extra 255 factor was
added to the SDK shader.

As supplemental model-domain evidence, `tools/test/topdown_video_reference.py`
reproduces the pinned C2 one-person detector input **byte-for-byte**, then
runs the pinned ONNX reference on two user-supplied videos. At 5/10 seconds,
`video-1.mp4` yields max person scores 0.646/0.663 and `video-2.mp4`
0.744/0.737, with 73/74 and 86/88 cells respectively above 0.35. Their
video, decoded-frame, input-tensor and model hashes are in ignored
`out/android-topdown-eval/video-reference.json`. This proves the model can
recognize those full-body scenes; it does **not** validate the camera/AHB
path.

A single corrected, eval-only `VideoPlayer` → `RenderTexture` → existing AHB
bridge run used `video-2.mp4` inside the **same** integrated Camera Demo. The
Development APK SHA-256 was
`541eaebe8d0cee0c58dc885b876ed618f233f085a2a48102de5b343ce8966b5a`,
package `com.blazetc.humanvision.topdowneval.i2c1.video`, PID 26427,
video SHA-256
`6abd4a523e9e0dc9961a3f037e0c33600271ff3a53d170e5f1dbd8c562480f53`,
profile and ModelPack hashes as above, native library SHA-256
`f662721b2c22e1b18a13a720b92a5d29b8aedb6bd0cb204885f6215eb678ef57`.
The eval component copied only the compressed MP4 from the APK's
`StreamingAssets` URI into package storage and verified its SHA before playback;
decoded frames stayed on the GPU. The PID log has
`HV_TOPDOWN_VIDEO_SOURCE_ACTIVE` with that SHA and advancing accepted video
indices 2→4, then 149→389; it has no `HV_TOPDOWN_CAMERA` marker. The screenshot
shows the gym video in the preview. Raw evidence is preserved under ignored
`out/android-topdown-eval/interval-2-capacity-1-video-diagnostic/device-video2-gpu-ahb-exactsource/`.
This eval VideoPlayer bypasses `HumanVisionLiveSource.Open`, while
`HumanVisionCameraManager.Fresh` depends on that source's `HasRecentFrame`.
Its facade slot/presentation counts do not prove overlay parity; only native
detector output and the GPU-input trace are used in this diagnosis.
An earlier folder ending `device-video2-gpu-ahb-verified` captured a camera
source after the Android `jar:` path was incorrectly tested with `File.Exists`;
it is explicitly excluded.

The controlled video still produced zero detector candidates and Bodies.
Per-frame rank-2 detector output had finite logits, with maximum raw logits
around -2.37 to -2.81 late in the run, whereas the pinned ONNX reference on
the same video at 5 and 10 seconds yielded 0.744 and 0.737 maximum person
scores with 86 and 88 candidates. At native source frame 240, the GPU-only
5×5×3 normalized-input probe sampled input locations approximately
`x={32,96,160,224,288}`, `y={32,96,160,224,288}`. At `y≈224`, all five GPU
values were exactly the normalized black pixel:
`R=-2.11790`, `G=-2.03571`, `B=-1.80444`; `y≈32` had the expected RGB114
padding values `(-0.165682,-0.0399159,0.182484)`. The closest video index
estimated from the logged playback clock is ~304; the pinned reference
bilinear-letterbox input on decoded frame 304 at `y=224`, those five x
positions, has R `[-0.303,-0.063,-1.125,-0.063,-0.423]`,
G `[-0.478,-0.162,-1.073,-0.145,-0.723]`, and
B `[-0.149,0.200,-0.776,0.113,-0.340]`. Frame 304 is a **nearby decoded
reference frame**, not a byte-identical capture of the native frame, so these
numbers establish a spatial/content mismatch lead rather than exact tensor
parity. The black GPU row despite a visible full video frame points toward
GPU texture copying, import layout, or preprocessing coordinates; ncnn model
parity has not been independently eliminated. No threshold was lowered and no
CPU full-frame readback was added. Detector and pose timing from these runs
describe an empty/no-body path and cannot establish full TopDown throughput.
The neighboring decoded-frame hash and full 5×5 reference grid are preserved
in `device-video2-gpu-ahb-exactsource/reference-frame304-grid.json`.
The measured APK predates the subsequent evaluation-component teardown fix.
`TopDownEvalVideoSource` now synchronously ends its GPU source lease before
releasing its RenderTexture on disable/destruction; its current source passed
a compile-only import in the ignored isolated Unity project. The exact C#
source used for the measured APK is preserved as
`device-video2-gpu-ahb-exactsource/TopDownEvalVideoSource-at-apk-build.cs`.
No device result is attributed to the post-review teardown edit.

## Verification after the final gate scripts

- `pwsh -NoProfile -File tools/test/test_android_topdown_gate_analysis.ps1`:
  **10/10 PASS**, including RED→GREEN fail-closed evidence fixtures and
  deterministic interval 2–6 profile hashes.
- `pwsh -NoProfile -File tools/test/run_native_tests.ps1`:
  **302/302 PASS**; this includes exact rank/shape, ModelPack contract,
  non-square letterbox and inverse decoder tests.
- `pwsh -NoProfile -File tools/test/run_unity040_tests.ps1`:
  **86 passed, 2 intentionally skipped, 0 failed** in the shared worktree,
  run independently after the Unity source edits.
- `pwsh -NoProfile -File tools/test/build_android_topdown_eval.ps1 -Interval 2
  -Capacity 1 -VideoDiagnosticPath 'E:/Project/Human Vision SDK/video-2.mp4'
  -DiagnosticTrace`: **PASS**, including Android ARM64/API-26 ELF/static-ncnn
  audit and APK dependency closure; this generated the corrected diagnostic
  APK whose SHA is recorded above.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`:
  **PASS**. `git diff --check`: **PASS**.
- `Unity.exe -batchmode -nographics -quit -projectPath <ignored isolated
  UnityProject> -logFile <postreview-compile.log>`: **PASS** after the
  evaluation-only teardown fix; the measured APK was not rebuilt or rerun.

Review RED→GREEN command output is saved locally under ignored
`out/android-topdown-eval/`: `review-red-thermal-body-and-config.txt`
(exit 1, four false-pass fixtures),
`review-green-thermal-body-and-config.txt` (exit 0, 9/9),
`review-red-renderer-thermal-provenance.txt` (exit 1, eight false-pass
fixtures), `review-green-renderer-thermal-provenance.txt` (exit 0, 9/9),
`review-red-missing-identities-region.txt` (exit 1, five false-pass fixtures),
`review-red-monotonic-observations.txt` (exit 1, seven false-pass fixtures),
`review-red-required-fields.txt` and `review-red-malformed-durations.txt`
(malformed evidence exceptions), and
`review-green-missing-identities-monotonic-required.txt` (exit 0, 10/10).
The earlier RED analyzer runs were observed in the task console and not saved
as separate files; they are not represented as archived logs.

## Current open Unity project validation

The requested open project at `E:\UnityProject\Human-Vision-SDK-Test` was
inspected through UnitySkills instance `HumanVisionSDKTest_F988EAA7` (Unity
2021.3.45f1). Before edits, the five Task 10 Unity counterparts and
`ProjectSettings/EditorBuildSettings.asset` were copied into ignored
`out/android-topdown-eval/open-project-validation/backup-20260926/` with
SHA-256 records. The five reviewed sources were synced by exact hash. Their
older runtime dependencies required four further source files:
`HumanVisionManager.cs`, `HumanVisionRuntimeSession.cs`, `RuntimeBindings.cs`
and `HumanVisionAndroidGpuFrameBridge.cs`; each was backed up and hash-verified
before replacement. Compilation initially identified the missing methods and
clock structure, then `asset_refresh` and `debug_get_errors` reported zero
errors after the narrow dependency sync.

The builder created `Assets/Scenes/HumanVisionCameraDemo.unity` (GUID
`dcd82dc5456f99d488390ca6d54011e3`) and
`Assets/Scenes/HumanVisionCameraSettings.unity` (GUID
`ed0affe668f768c46af3b7d106d420c6`). The backed-up Build Settings file
contained only two disabled entries with stale GUIDs at those exact paths.
Those two entries were replaced with the new enabled GUIDs; UnitySkills
`project_get_build_settings` then read exactly two enabled scenes in the
expected order. Unrelated settings were not changed. All eight existing
`HumanVisionPreparedGate*.unity` scenes still have line width 24 and joint
diameter 72, and the new Demo overlayer reported the same values in Play.

The Demo ran in the current Editor for about ten seconds and Settings for
about five seconds. Both were actually playing, their scene components were
present, and `debug_get_errors` reported zero Console errors. The Settings
scene exposed `HumanVisionRegionSettingsUI`. The live runtime did **not**
initialize: `HumanVisionCameraManager.Status` was
`Runtime data index missing: HTTP/1.1 404 Not Found`, `IsReady=false`, and
`ResultSequence=0`. The required
`Assets/StreamingAssets/HumanVision/Runtime/index.json` is absent from both
the repository Unity Demo and this open project; an Android ncnn model pack
was not substituted for a WindowsEditor runtime. This Editor check proves
import and scene wiring, not visible-person or Android GPU skeleton behavior.
The open Editor was stopped after both bounded Play runs. Backups, synced
hashes, scene/visual hashes, and actual Play snapshots are saved in the
ignored `open-project-validation` evidence folder. The project's Android
build target and user-owned assets/settings were preserved.

## Gate implementation and remaining decision

RED analyzer fixtures reject repeated IDs, body-summed FPS, missing/changed
APK/profile/model/native hashes, ORT/backend drift, CPU readback, stale Region,
excess bridge drops, missing verified sensor age, detector gap/new-track age,
low FPS, missing thermal, an all-empty output with an annotated visible
person, and native Bodies with no independently verified rendered mesh. The
reviewed analyzer also
checks the exact rolling 10-second exit boundary, capacity-to-visible and
native-to-rendered cardinality, continuous actual runtime interval/capacity/
backend/copy telemetry, actionable native/Vulkan failures, one-second result
freezes, strictly increasing result sequence/source frame IDs, required
nonempty process/device/package identities and valid Region revisions,
and 900-second thermal samples. It rejects missing or malformed gate fields
without allowing two absent values to compare equal. The thermal checks apply
the existing FPS/age/drop/backend and visible/native/rendered body criteria.
Thermal evidence must match the same APK, serial, fingerprint, package and
PID, and cover a later 900-second PID log timeline with a verified log hash.
The current collector has no such thermal segment, so missing provenance
cannot be marked PASS. The live overlay uses its actual
result-age presentation policy, rather than the file-playback frame-lag
limit. Hashes for intervals 2–6
are deterministic and distinct. The analyzer emits a prethermal
`timing_eligible` result while final PASS still requires thermal and human
physical evidence.

To clear the sensor-age blocker, a future approved camera route must pair the
**same** image handed to the AHB bridge with a native sensor timestamp (for
example a Camera2 image and its `SENSOR_TIMESTAMP`). A separate Camera2
timestamp stream cannot be paired to Unity `WebCamTexture` frames by timing
alone and must not be labelled verified. That architecture decision, and the
detector/FPS failures above, precede any renewed interval sweep or thermal run.
