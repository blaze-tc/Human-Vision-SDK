# Rectangle576 FP32 trial: Task3a offline eligibility

Fresh actual Snapdragon888/Adreno660 CPU/Vulkan shared-input comparisons pass
for seven-person, original single-person and analytic-empty fixtures. Both CPU
and GPU detect seven raised left arms in sequential video frame1500. This is
offline eligibility pending independent review; production GPU-AHB integration,
temporal behavior and throughput remain unverified. YOLO-M3 remains active.

## Geometry ruling

The explicit Task3 ruling retains upstream aspect-preserving `geometry(1024,576,576)`:
scale0.5625, resize576x324, stride32 padded576x352, left0/top14 and bottom14.
576x320 cannot contain324 resized rows without crop or stretch and is rejected.
Seven and uniform114 empty sources are1024x576. Their outputs have4158 anchors:
`(72*44)+(36*22)+(18*11)`; raw out0 `[4158,65]`, out1 `[4158,51]`.

The original verified single PNG is218x346. Its natural upstream resize362x576,
padded384x576, left11/top0/right11, has4536 anchors and raw out0 `[4536,65]`,
out1 `[4536,51]`. It is an offline graph-shape test only and does not authorize
production portrait input. Proposed future production qualification is576x352.

## Provenance and immutable archive

New archive: `out/android-yolo/rectangle576-eligibility-20261001`.
Fixture preparation reuses exact original PNG bytes and verifies seven RGB pixels
against the existing sequentially decoded RGBA/manifest. No historical640/512
fixtures or evidence were regenerated. The shared tensor oracle uses installed
OpenCV4.13.0 INTER_LINEAR uint8, CHW RGB float32 divided by255. This does not prove
byte parity with production ncnn resize or GPU-AHB preprocessing.

- Seven source PNG: `017ac2921ed78faff17d125165430bf45eb350861ff80ef5408c3a4b873dad04`.
- Seven source RGBA: `83db08727c2db6aafe2369e71f689428c3e9197176f2d10933a839b141ab9b67`.
- Seven R4 manifest: `7787f25a914b6507ce9a9b4ac1b635800088d2f71887c31534b99b28b199f193`.
- Video1: `e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`.
- Single source PNG: `cce59593686244632e5f1fd2e23da9c72f8c52c3948f28c16b9ba8299b668ad7`.
- Frozen eligibility index: `tools/models/ncnn/yolo_rectangle576_gate_evidence.json`, SHA256
  `c3389a91ff01e4bfe2a293b52b67b0fc21608ba9cb5916ab60786259acd52cee`.
- Seven arm report SHA256: `b281dc4f4938bf03a364ee4b626cf30968e62c300bf0b13eaff6fa92a33ef92c`.

Index records exact original fixture/source/video/annotation identities, runner
binary/source/CMake hashes, execution/comparison/log/output hashes, dynamic raw
shapes, device fingerprint and fresh run UUIDs. Weights remain pinned upstream
`f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca`; no conversion or precision change.

## Actual device gates

Root alone ran unchanged `yolo_golden_runner` in normal `gpu-fp32` mode, serial
`e7c07019`, OnePlus9Pro Android14. CPU/GPU use the exact same fresh tensor.
GPU input/output are pack1 bits32;205 layers, unsupported0, storage16/arithmetic16
disabled. All execution identities and numerical comparisons were reread and
recomputed before freezing; recomputed reports equal the actual saved reports.

| Fixture | Fresh GPU run UUID suffix | CPU/GPU people | out0 max/mean absolute error | out1 max/mean absolute error |
|---|---|---|---|---|
| seven-576 | f190aa94c8e540bd9fff84857da7e824 |7/7|0.0002450943 /0.00000206419|0.00004297495 /0.00000166396|
| one-576 |80f4b8a2a43b4289b60b831794c1d295|1/1|0.00007915497 /0.00000169369|0.00003385544 /0.00000150397|
| empty-576 |849c33129c66453aa05d2c1187112357|0/0|0.0001964569 /0.00000131350|0.00002950709 /0.00000149827|

Frozen limits remain raw max0.2/mean0.01, CPU/GPU box IoU>=0.95, score error<=0.01,
joint XY error<=3 source pixels and confidence error<=0.01. Every association
passes. Existing independent manual-annotation person coverage IoU>=0.3 passes
for all seven; minimum CPU0.5920319383, GPU0.5920322031.

The unchanged raised-arm predicate uses joint5 left shoulder and joint9 left
wrist, both confidence>=0.2, both inside source bounds, wrist above shoulder.
All7 pass in CPU and GPU. Minimum shoulder confidence CPU0.9644455562,
GPU0.9644456134; minimum wrist confidence CPU0.8816706563, GPU0.8816705320.
Minimum vertical raise CPU10.432739 pixels, GPU10.432800 pixels. Detailed
source coordinates/confidences and per-person annotation IoUs are in the frozen
arm report within the seven run archive. One frame does not establish temporal
fidelity, identity stability or all-joint physical accuracy.

## Commands and verification

Run from `E:/Project/Human Vision SDK/.worktrees/android-ncnn-vulkan`:

```powershell
py -3 -m unittest tests.reference.test_yolo_rectangle576_gate -v
py -3 tools/models/ncnn/yolo_rectangle576_prepare.py
py -3 tools/models/ncnn/yolo_device_gate.py --root out/android-yolo/rectangle576-eligibility-20261001 --runner out/android-yolo/runner-android/yolo_golden_runner --serial e7c07019 --gpu-mode gpu-fp32 --fixtures seven-576 one-576 empty-576
py -3 out/android-yolo/rectangle576-eligibility-20261001/freeze_evidence.py
py -3 -m unittest tests.reference.test_yolo_rectangle576_gate tests.reference.test_yolo_pose_gate -v
py -3 tools/maintenance/check_architecture_boundaries.py
```

Initial actual rejection RED: wrong geometry320, rehashed tampered float tensor,
rehashed tampered PNG each failed with `ValueError not raised` before validator
implementation (3 failures). GREEN:22 tests (7 fresh fixture tests plus15 decoder
regressions). Unknown fixture/metadata, pinned source/provenance/annotations,
altered limits/raw shapes, production-claim mutation and existing output path
are also rejected. Existing destination sentinel remains unchanged. Architecture
and public-surface checks pass. Fixture CLI refuses existing output directories.

No runtime/backend/ModelPack/staging changes or native compilation occur in3a.
All original640/512 contracts remain available. No FPS, GPU-AHB acceptance,
new execution kernel qualification, default promotion, Release or main merge.
Target30 and at-least25 fresh complete observations/s remain unmet;25FPS video1
cannot certify30. Integration requires its separate review/gates.
