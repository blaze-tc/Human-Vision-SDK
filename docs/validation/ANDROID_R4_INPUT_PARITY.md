# Revision 4 Task 1 — exact offline input fixtures (2026-09-26)

This report pins **offline** decoded source bytes and detector input tensors for
R4.1. It does not establish Unity VideoPlayer byte identity, Android GPU parity,
person detection on device, or physical acceptance. The Revision 3 integrated
gate remains FAIL. Task 2 must upload these goldens through the production GPU
route; Task 3 must separately latch and compare the actual VideoPlayer texture.

## Sources and human annotations

I inspected exact decoded frame 150 from both supplied videos at source resolution.
The `video-1.mp4` frame (1080×1884 portrait) visibly contains five complete
people. The `video-2.mp4` frame (1920×1080 landscape) contains six complete
children and one foreground adult whose legs leave the bottom edge; one rear
child overlaps a front child. The manifest stores manually estimated source-pixel
bounding boxes and visibility notes for **all** visible people. These boxes are
human annotations, not detector output or pose ground truth. The frame called
“full-person” contains multiple people; it is not a single-person fixture.

| Source | Video SHA-256 | Frame | Decoded RGBA SHA-256 | FP32 tensor SHA-256 | FP16 RTZ tensor SHA-256 | People |
| --- | --- | ---: | --- | --- | --- | ---: |
| `video-1.mp4` | `55cd66ae01696939a9c78ab55039e4011c2be4b8449e9e1c893959bc0d1975e0` | 150 | `6b1a0f7d5f07ef160ecfa36d7d748231239a00bae9aa38a305ce0fba9bc36b40` | `571c836eddc943db8269e5d46deff131e9ea46c2b428b044d43362c77d3ebc71` | `33d48d56b6decffd19c8fa2b142bc701b684a4454022f27ca39e6f7601a07228` | 5 |
| `video-2.mp4` | `6abd4a523e9e0dc9961a3f037e0c33600271ff3a53d170e5f1dbd8c562480f53` | 150 | `9291077b50f995044116342737c1e6b3a6b60f274b5f66f4afb5222b9569319e` | `9c5ed7d87ec15e69ef10abe3d4a51a30896e70ae8247da71a8b383e8865b102d` | `8e0b067d4aa9c8893aef9a3a65b5709f5eb807024da839e7daeb83148d52507d` | 7 |

The generated manifests and raw files are ignored under
`out/android-r4/video-1-frame-150/` and `out/android-r4/video-2-frame-150/`.
Manifest SHA-256 values are `cb0d101de39d7a2c5aa98a1c0831828178828c528f7352a661264813206c31ca`
and `05143fb8291beb190d79e5e7434c1ed63962daee53c6ddde7e130aa622e8a392`,
respectively. No video frames or tensors are committed.

## Analytic upload fixtures

`tools/test/r4_fixture_manifest.py --analytic` writes an asymmetric 5×3 RGB
corner/color pattern and a non-square 7×4 unique-color grid. Each source is
rotated clockwise by 0/90/180/270 degrees, then either left unmirrored or
mirrored horizontally, for 16 ignored cases. Each case contains exact RGBA8,
detector FP32 and FP16 RTZ files, source/derived shapes and strides, hashes,
and five named landmark mappings (source coordinate, transformed coordinate,
RGBA value). The manifest at `out/android-r4/analytic/manifest.json` has SHA-256
`11f772fe4d375dfafdb6a91ed22e2dc5984e373b1bf91a580027659700629635`.
This supplies uploadable known colors/locations for Task 2; it does not imply
any GPU path passed. Tests independently pin hand-derived landmark coordinates
for all eight transform choices. A separate saved-output pixel oracle reads the
RGBA files and checks literal source colors at hand-derived expected coordinates
for all five landmarks in all 16 cases. Deliberately generating mirrored cases
without mirroring made that oracle fail 40 comparisons; the generator's existing
self-validation would otherwise share the same faulty transform helper.

## Decode and tensor contract

`tools/test/r4_fixture_manifest.py` sequentially decodes from frame zero
through index 150 using OpenCV 4.10.0 with FFmpeg backend (`avcodec 58.134.100`,
`avformat 58.76.100`, `avutil 56.70.100`). It writes top-left-row RGBA8,
full-range RGB, opaque alpha, packed row stride `width×4`. OpenCV's decoded RGB
values are labelled `decoded_srgb_assumed`; the MP4's color metadata and
VideoPlayer conversion are not asserted identical by this label.

The independently computed detector input uses the pre-existing pinned ONNX
reference: bilinear letterbox to 320×320, RGB 114 padding, RGB order, means
`[123.675,116.28,103.53]`, scales
`[0.017124753831663668,0.01750700280112045,0.017429193899782137]`,
NCHW planar FP32, then observed Snapdragon FP32→FP16 round-toward-zero
packing (`elempack=1`). The portrait content is 183×320 with 68-pixel left pad;
landscape content is 320×180 with 70-pixel top pad. The checked detector
input contract identifies model pack `precision-t-26-ncnn-fp16`, detector param
SHA-256 `9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3`,
bin SHA-256 `4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da`,
and the checked profile SHA-256 is
`20e2714759f03fee79a94d0dcf7ccc80700785a2cb191a4a01b403e0b975d0b2`.
The local model
files were hash checked against the contract when generated.

The manifest declares clockwise rotations 0/90/180/270 followed by optional
horizontal preview mirror. The analytic 5×3 asymmetric grid tests corner and
interior point positions across all eight combinations. It is a transform
contract for subsequent GPU tests, not evidence that the current GPU path
already handles each combination.

## Verification

- Focused RED: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_r4_fixture_manifest.py -v` failed because the manifest module was absent; log `out/android-r4/task-1/red.log`.
- Focused GREEN: the same command passed 11/11. Tests reject altered bytes, changed frame index, missing/wrong decoder, shape/stride errors, nonfinite tensors even with a renewed hash, and changed fixture identity. Separate oracles check padding, RGB channels, sampled FP16 RTZ bits and saved RGBA colors at all eight geometry mappings. Controlled wrong-mirror RED and normal GREEN logs are `out/android-r4/task-1/review-oracle-red.log` and `review-oracle-green.log`.
- Analytic generation: `.venv-reference/Scripts/python.exe tools/test/r4_fixture_manifest.py --analytic --output-dir out/android-r4/analytic` wrote 16 cases with the manifest hash above; a saved-manifest validation pass checked every artifact byte.
- Existing video reference: `.venv-reference/Scripts/python.exe tools/test/topdown_video_reference.py --model out/c2-local-detector/rebuild/rtmdet-nano.onnx --video 'E:\Project\Human Vision SDK\video-1.mp4' --video 'E:\Project\Human Vision SDK\video-2.mp4' --seconds 5` passed; its frame-150 FP32 hashes equal the manifest hashes. The pinned ONNX scores are 0.646 and 0.744 respectively, without any Android GPU claim.
- Existing prepared detector golden: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_prepared_gate_golden.py -v` passed 2/2.
- Existing ncnn model contract: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_ncnn_model_contract.py -v` passed 30/30. `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py` passed.

The exact commands and final check results are also in `docs/DEVELOPMENT_STATUS.md`.

## Revision 4 Task 2 review candidate (2026-09-27)

Task 2 is pending independent spec and quality reviews. The aggregate evidence is
`out/android-r4/task2-review-evidence.json`; it deliberately keeps
`task2_pass=false`. Native candidate SHA-256:
`71b661ab2e9b9e1dfbc2c1ba343d4c43fa5aac90a3bafb59f5d3bf651e52bc96`.
All device runs use serial `e7c07019`, verify the installed APK and embedded
manifest, and preserve continuous prelaunch logcat, normal model outputs, hashes
and reports. No source image or preprocessed tensor is downloaded.

The final matrix is `matrix-final-tickets-summary.json`: 16 original-source
analytic cases through the production orientation shader, each on measured blit
and color-attachment paths, 32/32 PASS. Canonical top-left clockwise angles are
adapted explicitly to Unity UV angles by F*R*F=R^-1; selected goldens retain the
canonical transform. Each run includes five same-frame stages, 16 GPU clean/fault
controls and three descriptor controls. Source/producer/import errors are zero;
normalized max0.0175070763 and packed max0.017578125 remain within max0.02/
mean0.002. Image-stage observed slots are0,1 and tensor-stage slots0,2; do not
interpret this as every stage covering all three cached slots.

The user replaced video1 with landscape video SHA
`e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`.
Its current frame1500 manifest is
`7787f25a914b6507ce9a9b4ac1b635800088d2f71887c31534b99b28b199f193`.
Video2 frame150 retains manifest
`05143fb8291beb190d79e5e7434c1ed63962daee53c6ddde7e130aa622e8a392`.
Old portrait video1 failures and fixes remain historical evidence, not current
video acceptance. Future actual VideoPlayer playback starts37s as requested.

Real-frame evidence directories:
- `device-astra-current-video1-tickets` and `...-color`;
- `device-astra-video2-tickets-p1` and `...-p2`.

Both paths pass input and inherited detector comparison for both videos.
Normal raw detector outputs are saved twice with exact hashes. Offline reference
runs use the independently prepared FP16-RTZ input, cast to FP32 for pinned ONNX.
Raw max/mean/P95 errors remain separate in `model-analysis.json`; they are not
called zero-error raw-array parity. The existing authoritative detector comparator
is `tools/models/ncnn/compare_detector_outputs.py`: one-to-one IoU>=0.95,
score error<=0.01, repeat agreement, threshold0.35. This is a spatial/score check,
not merely matching candidate counts. There is no existing absolute raw-logit
array tolerance to invent after observing errors. Review must assess this
interpretation against the R4 raw-output requirement. No converter/model
substitution or confidence change was made.

Each primary blit sample preserves7 bodies x32 canonical slots. Independent
image-only shoulder/pelvis regions associate all seven annotated people
one-to-one; either anatomical midpoint can establish a unique association, and
missed regions remain recorded. Whole-person box annotations are unchanged.
Video2 far-right pelvis midpoint678.292 falls outside its approximate region
beginning680; its shoulder uniquely associates. Color-path log bursts omit some
individual joint messages, so those pose samples fail closed as incomplete
capture; they do not replace the complete primary samples.

Fresh official strict-VkMat pose golden replay passes8/8 (four pinned cases,
two runs), with normal output bytes identical to the previously validated
outputs. `task2-fresh-pose-golden/report.json` records runner/source/model/input
and output hashes. Strict mode excludes the runner's input-echo path.

Additional current-image comparison uses the hash-pinned official checkpoint
`6020f8a6746639c0144eb979df0be0baa707af428d0e20a2db8991cf7452e5d6`
and clean pinned vendor sources, as the official golden does. The deployed ONNX
contains an mmdeploy custom operator unavailable in ORT, so PyTorch is the
reference. Offline float bilinear preprocessing and FP16RTZ use the same actual
pose crop; reconstructed crop values must exactly reproduce the six-significant-
digit native log. Only normal reference outputs are saved. Script/input/output/
checkpoint/tool-version hashes appear in each
`same-crop-pose-reference/report.json`.

Accepted-joint coordinate/confidence metrics pass7/7 on each image using the
existing P95 distance0.01, max distance0.03 of box diagonal, and P95 confidence
error0.02. The supplemental exact valid-mask diagnostic passes7/7 for video1
and6/7 for video2: the clipped foreground ankle is reference y1080.327 (outside)
versus actual1078.692 (inside), a1.635px boundary straddle. That failure remains
explicit; no validity threshold, coordinate clamping or reference mask was
changed. Three visible video1 wrists are below confidence threshold in both
candidate and reference. These are quality limitations for visible acceptance,
not perfect/full-joint skeleton claims.

### Commands and checks

- Android: `pwsh -NoProfile -File tools/package/build_live_native.ps1 -Platform Android -R4Parity`; `.venv-reference/Scripts/python.exe tools/test/verify_android_native.py`: PASS.
- Host: VS bundled `ctest.exe --test-dir build/windows-test -C Release --output-on-failure`:313/313 PASS,24.17s; `task2-final-native-tests.log`.
- Architecture: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/architecture -p 'test_*.py'`:20/20 PASS before new association test; focused `test_r4_pose_association.py`:5/5 PASS, including ambiguous/stale/missing rejection and one-region RED->GREEN.
- Reference discovery under `tests/reference`: `test_r4_fixture_manifest.py`11/11; `test_prepared_gate_golden.py`2/2; `test_ncnn_model_contract.py`30/30.
- `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py`:PASS.
- Current authorized Unity editor exact classes: `HumanVisionAndroidGpuRoutingTests`12/12; `HumanVisionAndroidGpuGateBuildTests`9/9. An unmatched broad filter produced an empty-suite artifact and is excluded.
- Collector example: `pwsh -NoProfile -File tools/test/collect_android_r4_parity.ps1 -Manifest out/android-r4/analytic/manifest.json -RunLabel UNIQUE -CaseIndex 0 -CopyPath 1 -RequireTicketMetadata -NativeLibrary out/android-topdown-eval/r4-task2-final-matrix-tickets/libhumanvision.so -BuildDirectory out/android-topdown-eval/r4-task2-final-matrix-tickets`. Use a new label; retain immutable APK/native pairs.
- Model analysis: set `PYTHONPATH` to repository root, then `.venv-reference/Scripts/python.exe tools/test/analyze_android_r4_model_outputs.py --manifest <manifest> --run <device-directory> --reference <offline-reference-directory>`.
- Additional exact commands and repair RED/GREEN chronology:
  `.superpowers/sdd/2026-09-26-android-gpu-input-camera-revision-4/task-2-report.md`.

The open Unity project was backed up before staging; the editor build adapter
restored scenes, runtime mode and project settings in finally, independently
checked by the controller. Staged evaluation assets still have backups.
Static repeated frames do not establish sensor/live FPS, actual VideoPlayer
acquisition or visible overlay acceptance. Task3 remains blocked on Task2 review.

### Review round 1 corrections (2026-09-27)

The first review required changes, so the preceding candidate is historical.
Task 2 remains active pending revised device evidence and scoped review.

`RecordRgbImport` now marks the imported destination as a compute shader write
before either the aligned return or packed conversion. The pinned ncnn import
recorder does not update that state. A compiled harness executes the actual
production function body with fresh buffer state and verifies that both branches
supply the first reader's dependency. The previous aligned branch fails; both
repaired branches pass. The separate assignment-order assertion is supplementary.
No queue ownership or sync-fd behavior changed.

The tracked builder now supports parity capacities 1 through 8, defaults to the
largest annotation count in the fixture, and rejects undersized explicit capacity.
The existing live acceptance path still allows only capacities 1 and 2.
`-ResolveConfigurationOnly` exposes the effective configuration;
`-StageOnly` prepares the evaluation project without launching another editor.
The collector checks observed capacity against the saved configuration and hashes
that configuration. Controlled builder regression: RED then 3/3 GREEN.

Actual source and producer VkImages now feed a cached GPU mutation image, followed
by the same image comparison adapter against independently uploaded goldens.
Actual imported RGB feeds a cached ncnn mutation buffer and its reduction adapter.
Each boundary exercises clean, channel swap, vertical flip, truncated copy,
wrong stride, wrong packing, wrong scale and deliberately stale-content controls.
Only bounded scalar summaries return to the CPU. The collector requires all 24
controls and independently joins clean preceding stage records by generation,
source ID and slot; a claimed native PASS without those records is insufficient.
The older device capture fails this new requirement, and the first revised actual
case passes all 24 controls. Existing normalized/packed GPU and metadata controls
remain required.

Revised native SHA-256:
`20becafd129a84696be85c252e5c1ebef6b10b72fb6dbe8d088c9123a0f51c07`.
The first revised analytic APK is
`30076e3ef866b18c2ef4f6c7018b1266894181a40c6314e065cae97ed4ee29b7`.
Its build directory is
`out/android-topdown-eval/interval-2-capacity-8-r4-parity-review1-boundaries`.
The source, embedded manifest, native library and installed APK are hash checked.
First device report: `device-astra-review1-boundaries-c0-blit/report.json`.

Reproduction uses the tracked commands below, then the supported current-editor
menu `HumanVision/Evaluation/Build R4 Static Parity`:

```powershell
pwsh -NoProfile -File tools/test/build_android_topdown_eval.ps1 -Interval 2 -Capacity 8 -R4ParityManifest out/android-r4/analytic/manifest.json -RunLabel review1-boundaries -StageOnly
pwsh -NoProfile -File tools/test/stage_android_r4_open_project.ps1 -BuildDirectory out/android-topdown-eval/interval-2-capacity-8-r4-parity-review1-boundaries -BackupDirectory out/android-r4/review1-open-project-backup
```

Fresh checks: native 313/313 in 25.53 seconds (`review1-native-tests.log`),
architecture 33/33 in 62.602 seconds (`review1-architecture-all.log`, including
compiled first-use tests under the pinned VS environment), Android build and ELF
verification PASS. Revised transform/path matrix passes32/32 with all24 boundary
controls per capture; `matrix-review1-summary.json` binds reports and hashes.
Observed image slots0 and tensor slots0/2 are recorded without claiming full
pool coverage. Revised real-frame captures are in progress.

The exact uploaded-reference-tensor ncnn detector isolation run is absent. The
saved detector comparisons use production preprocessed ncnn input and the pinned
offline FP16-RTZ reference input, whose near parity is separately measured. This
is not claimed as identical-input isolation. The inherited spatial/score detector
gate passes; raw array deltas remain explicitly reported without an invented
absolute-logit threshold. The existing pose, clipped-edge, wrist, logging and
static-display limitations above remain in effect.

### Large-frame diagnostic scheduling correction

The revised 1920x1080 blit capture failed twice during startup. Preserved reports
are `device-astra-review1-video2-p1` and `device-astra-review1-video2-p1-retry1`.
Their crash buffers show Adreno GPU command submission failure (errno35), then
`VK_ERROR_DEVICE_LOST`, surface dequeue failure and a secondary null image access
inside Unity's Vulkan render-pass code. There is no owned-library program counter
in the crash stack. The preceding native candidate71b completed the identical
fixture once in `device-astra-review1-video2-old-control`.

The new diagnostics had queued16 full-resolution image controls in one producer
submission and8 full-resolution import controls in one consumer submission.
The bounded correction schedules one control per completed submission, using
cached resources and cursors. Full-image comparisons and all corruption cases
remain intact. Image baseline probes continue until imported-buffer controls
finish; every control must independently bind clean preceding stages from its
own generation/source/slot. No CPU image readback, wait, production synchronization,
model, geometry, confidence or tolerance change was introduced.

The focused aggregation regression fails before this correction and passes4/4
afterward, including rejection when only another frame has a clean baseline.
Patched native/ELF verification passes with SHA
`d6f0ec685c1817d63ae7060edcd829e40f840b4497061a5425ad1c189e78349f`.
Actual large-frame GREEN and scoped review remain pending. The32-case matrix
above remains evidence for unchanged geometry on20bec; it is not mislabeled as a
32-case run of this later scheduling binary. Final verification is deliberately
focused on1920x1080 both paths and1024x576 representative input.

The bounded scheduling correction subsequently passed the1920x1080 fixture on
both paths (`device-astra-scheduled-video2-p1` and `-p2`), including all24 controls,
strict detector comparison and complete seven-person association. The1024x576
representative `device-astra-scheduled-video1-p1` passed input/control/detector
checks; its incomplete canonical log sample remains excluded from pose acceptance.
The complete video1 pose evidence is `device-astra-review1-video1-p1` on20bec,
whose production geometry/model logic is identical. Video2 primary same-crop
coordinate/confidence remains7/7; supplemental exact-mask remains6/7.

A further scoped review found that the shared fixture's completion boolean could
survive a new consumer session. `ParityControlEpoch` now assigns a new pending
session token atomically before admission and accepts completion only for that
token. A stale session cannot complete or overwrite the current session. Focused
compiled lifecycle tests fail2/2 with the boolean implementation and pass2/2 with
the epoch implementation. This is host lifecycle evidence, not a claimed device
restart-cycle test. Final native/ELF SHA is
`1237b475866e64496d5424f27b5a286272483d291750db74f83db6b26883d0b3`;
one current-epoch1920 blit smoke and scoped final review remain pending.


Final current-epoch smoke (`device-astra-epoch-video2-p1`) passes on native1237b:
all five same-frame input stages,24 controls, strict detector comparison and
complete seven-person association. The scheduled captures each contain16 distinct
image-control submissions and8 distinct import-control submissions, verified from
actual generation/source/slot records. `task2-final-review-evidence.json` binds
all final reports, preserves both crash failures, and distinguishes matrix20bec,
scheduling d6f and final epoch1237b evidence. Final scoped review remains pending.
