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


## Task3b: explicit local SDK integration, Code READY pending review

The reviewed576x352 geometry now resolves4158 anchors below the stable public
API. Its default-kernel `raw_tensor_fp32_v1` ModelPack and profile are explicit
local evaluation only. The16:9 source produces576x324 resized content with
fourteen rows of top/bottom padding. Tests exercise multiple16:9 source sizes,
last cells at all three strides and restored coordinates. Other source aspects,
576x320/square/portrait shapes, alternate SGEMM/no-local-memory contracts,
FP16 and production declarations are rejected. Generic GPU preprocessing,
producer waits, AHB imports, ownership/retirement, Tracker and APIs are unchanged.

The runtime builder rehashes all three actual archives, validates prepared
pixels/geometry/source provenance and recomputes the original raw comparison.
It recomputes both CPU/GPU seven-arm annotation associations and matches their
archived semantic rows. Staging repeats that gate, pins raw/Unity index hashes,
four selected files and runner/source/recipe/semantic provenance. Existing640,
512 and original square contracts and historical staging identities are retained.

The real host rejection test first resolved combined production+FP16, wrong
height, alternate execution and an extra no-local-memory option before the new
scoped guard; the corrected real-asset RED has four `Actual: true` failures.
Transform creation and runtime shape acceptance also failed before integration.
Final GREEN:163/163 focused native tests,115/115 YOLO reference tests and4/4
legacy staging tests. Architecture/public-surface checks pass. API26 ARM64 audit
resolves1813 strong dynamic imports. These checks establish software readiness,
not actual GPU-AHB accuracy, temporal following, throughput or physical acceptance.

The normal ncnn dependency has `NCNN_BENCHMARK=OFF`; GPU_GATE, R4_PARITY,
NCNN_EXECUTION_TRACE and TOPDOWN_EVAL_TRACE are OFF in the new Android build.
Five explicit FP32 backend options retain the normal Winograd/SGEMM/local-memory
selection; no-local-memory is not enabled for576. Existing RTSP/dependency
packaging remains unchanged, including explicit compatibility libraries.

Frozen ignored evidence is `out/android-yolo/rectangle576-integration-frozen`:

- Native SHA256: `60d847e993db2e8a446f5a6807240be95229c695e20a7040db21474cfab5cb42`.
- Raw runtime index: `9b1c5b0f3b25c593b4b973ae023cdf3d56615f0fdc1084cdbab3f1a4ede7e46f`.
- Unity index: `ca90ae5654c63d1f79d16f10e6b07bcde0f49def1905626d53fe9af2add08a95`.
- Freeze manifest: `2297f013e3f5e8c4c387d1a1ed2f9defbc74c01b31024b2d91b2cac714bb2a15`.

The149 frozen files include126 actual source snapshots, complete working-tree
patch, native, ncnn archive/cache, runtime indices and RED/GREEN/build/audit logs.
`verify_freeze.py` verifies those149 hashes and current source bytes, then
recomputes all three original real-device archive gates. The native binary was
built from the current tree including preserved unfinished R4 changes; clean
HEAD alone is not asserted to reproduce it. All31 preexisting dirty tracked
files still match the Task2 preedit ledger. Caches and old outputs are retained.

Exact verification/build commands, from the worktree:

```powershell
pwsh -NoProfile -File tools/test/run_native_tests.ps1 -Filter 'Ncnn|Yolo|GpuTrack|TopDown|UnityVulkanBridge|AhbSlotRing|GpuAbi'
py -3 -m unittest discover -s tests/reference -p 'test_yolo*.py' -q
py -3 -m unittest tools.test.test_stage_android_yolo_eval -q
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build build/android-yolo-rectangle576 --target humanvision -j 4
py -3 tools/test/verify_android_native.py --library out/android-yolo/rectangle576-integration-frozen/libhumanvision.so
py -3 tools/maintenance/check_architecture_boundaries.py
py -3 out/android-yolo/rectangle576-integration-frozen/verify_freeze.py
py -3 tools/test/stage_android_yolo_eval.py --runtime out/android-yolo/runtime-rectangle576x352-arm-verified --size 576 --verify-only
```

The fresh Android configure uses Ninja/Release, BUILD_TESTING=OFF, Unity's
2022.3.61t4 NDK android.toolchain.cmake, arm64-v8a/android-26 and normal pinned
`out/ncnn-20260526/android-arm64-api26/install`. The exact cache and Ninja recipe
are frozen above. Runtime creation was:

```powershell
py -3 tools/models/ncnn/yolo_stage_runtime.py --size 576 --destination out/android-yolo/runtime-rectangle576x352-arm-verified
```

That destination already exists and must not be overwritten. Root may use the
verified runtime/native to prepare a fresh review-approved scratch project:

```powershell
pwsh -NoProfile -File tools/test/stage_android_yolo_eval.ps1 -Size 576 -Kernel default -RuntimeDirectory out/android-yolo/runtime-rectangle576x352-arm-verified -NativeLibrary out/android-yolo/rectangle576-integration-frozen/libhumanvision.so -OutputDirectory out/android-yolo/eval-rectangle576-reviewed-20261001
```

Software staging was actually run successfully into
`out/android-yolo/eval-rectangle576-task3b-full-stage-v2/UnityProject`, including
copied native/dependencies and both root/embedded runtime selections. It contains
a genuine20-file `modelpacks/precision-t-26-ncnn-fp16/SHA256SUMS.txt`: all actual
legacy param/bin and evidence hashes are verified before any stage write, then
the copied files are verified again before indexing. This satisfies the existing
editor validator; the selected runtime remains576 YOLO. Tests reject tampered
legacy assets. No Unity editor/build/install or ADB operation occurred in3b.

## Same-native640 baseline for Root's controlled comparison

Use two scratch copies of the reviewed576 project, preserving the exact native
and dependency bytes. For the640 baseline only, verify the existing frozen
`out/android-yolo/runtime-rectangle640x384-arm-verified` index and all four files
with `verify_runtime(root,640)` plus its historical `INDEX_SHA[640]`. Copy exactly
those four files to the scratch project's root and embedded Runtime directory;
write the embedded index using `unity_index(index,files,640)` and verify it again.
Record both selected index/file hashes and the unchanged new native hash in a
comparison receipt. Switch each scratch Android runtime asset/scene to its
explicit selected profile through Root's established workflow. Retain the exact
same capacity8, video1/start37s, orientation, HUD, warmup and sample window.

The reviewed native supports both old640 and new576 default execution contracts.
This runtime-only scratch comparison isolates resolution from native build
identity. Do not pass the new native to historical640 staging: its old native
allowlist intentionally remains unchanged. Root owns these scratch copies,
Unity builds, APK/installed identity verification, ABBA capture and thermal/load
reporting after independent Code review. No performance gain or default promotion
is claimed by this integration. >=25 fresh complete observations/s and target30
remain pending/unmet; video1's25FPS cannot certify30. No main merge or Release.
