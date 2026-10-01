# Bounded production VkMat execution diagnostic (2026-10-01)

The existing stage capture measures extraction/download wall time around56ms,
including internal ncnn submissions. It does not measure pure CPU recording.
This task prepares one diagnostic of that same640x384 FP32 production route;
actual Unity/AHB capture now has validated layer timings below. Diagnostic
throughput is not an optimization result; accuracy, default promotion and
physical acceptance are not established by this capture.

## Official source finding

Pinned ncnn20260526 commit `e54f7b1f88434e1d844ea0551b880a1cfb079ce1`
records official layer timestamps in `NetPrivate::forward_layer` (net.cpp).
Its `extract(Mat)` overload creates the query pool and reads the final batch;
production uses `extract(VkMat, VkCompute&)`, which does neither. Simply enabling
NCNN_BENCHMARK therefore does not establish valid layer timings in this route.
The official reader also treats VK_NOT_READY as success without availability.

The separate source copy retains official timestamp placements, original
dispatch thresholds, submits/fence waits, resets, producer synchronization and
AHB ownership. It disables legacy benchmark printing/read loops in that copy.
A lazily created8192-query pool lives with each existing VkCompute and is reset
by its original reset/submit paths. Every existing successful submission reads
only its recorded layer pairs after the fence, using64-bit availability results
without adding a wait. Queue-family timestampValidBits, timestampPeriod,
VkResult, both availability flags and raw masked ticks are logged. Unsupported
capabilities, failed creation and malformed pairs emit explicit error records.
The8192-query bound permits4096 model layer indices; exceeding it fails analysis.

Three hidden context functions identify sampled frames and phase0(preprocess),
phase1(existing internal extraction submissions), phase2(existing outer
inference/download submission). Frames sample first3 and every64 successful raw
calls using the existing trace cadence. End records identify unsuccessful runs.
Frame/segment/command/layer identities prevent replay or double counting.

`HV_ANDROID_NCNN_EXECUTION_TRACE` defaults OFF and requires Android, existing
stage trace and a separate diagnostic receipt. CMake rejects a tagged diagnostic
package when the execution flag is OFF. Its library hashes still pass the
original ncnn/glslang package receipt verification. No dependency pin, cached
official source, normal library or binary allowlist was altered.

## Identities and build

```powershell
pwsh -NoProfile -File tools/test/ncnn_execution_build.ps1 -Destination out/android-yolo/execution-diagnostic-20261001-final
pwsh -NoProfile -File tools/test/ncnn_execution_runtime_build.ps1
py -3 tools/test/verify_android_native.py --library build/android-yolo-execution-trace/bin/Release/libhumanvision.so
```

Final copied source metadata:
`out/android-yolo/execution-diagnostic-20261001-final/diagnostic-source.json`.
All7608 copied files have verified hashes in `source-manifest.json`; manifest SHA:
`baa4c7a0edeeffc05c6f2c807b4114d900b385e83fe7acb7b7c6bf89bc269d19`.
Generator SHA:
`bc5afcc066872d42dfd5c7e7f81c2cea17b80e67360acffd41a4b9b776f472d3`.
Copied net.cpp SHA:
`b51541842ab37fe88b23132d7b10210669f3902db158b46e1f427530cbf6183a`.
Copied command.cpp SHA:
`decfda98e85dbee22aef2f83af27d2b616da9f9e9d605e508f340be9085dc49f`.
Archive SHA remains:
`754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b`.
Diagnostic libncnn.a SHA:
`393ee7dcfc5f5cfaf63f401dbbea63d4407f419d663d92a953d82f9d1504290d`.
Diagnostic build receipt SHA:
`47a8cb3563291bb9c16e1628d9ff9857146e161e14c13a88070a57a173e4c826`.
Diagnostic native SHA:
`a9d5c3e726f8c7ebbfda3df4dd2e50822ef99fd14ef374fcc7370edb3d4afbdc`.

API26 ARM64 audit passes1811 strong imports. Full original DT_NEEDED includes
FFmpeg, ORT, Vulkan, Android, JNI, log and system C/C++ dependencies; RTSP is ON.
All three diagnostic hooks link as LOCAL HIDDEN and are absent from dyn-syms.

Rebuilding `build/android-yolo-no-local-memory` with diagnostic flag OFF produces
the exact accepted prebuild SHA:
`8a1ab8d250c8d214e3fd5b976c4a55043a5de655d760e7688bb604d5470b8b98`.
Its normal API26 audit still resolves1813 imports. The two-import count difference
in the diagnostic is not evidence of changed backend availability: the full
original DT_NEEDED closure is present; diagnostic logging replaces legacy paths.
Both binaries include the preserved preexisting dirty R4 workspace baseline;
the task-only commit does not represent every byte of those builds. Root must
retain its source snapshot/receipt when staging or reproducing the diagnostic.

## Verification

The validator was initially permissive: the real malformed/invalid/missing/replay,
overlap and patch-drift cases produced11 failures, plus one positive-case error.
After implementation,14 focused tests pass. They reject unavailable/failed
timestamps, zero/invalid capability, reversed/wrapped ticks, missing queries/end,
duplicate layers/segments, wrong commands, missing phases and patch hash drift;
exact-source patch tests preserve submit/reset/dispatch decision tokens.

```powershell
py -3 -m unittest discover -s tools/test -p test_ncnn_execution_trace.py
# In the pinned VS x64 developer environment:
py -3 -m unittest discover -s tests/architecture -p test_ncnn_*.py
py -3 tools/maintenance/check_architecture_boundaries.py
py -3 tools/package/check_public_surface.py
```

All41 ncnn architecture/host boundary tests pass with the VS environment,
including actual production-body producer wait/crop failure cases. Public API
and architecture guards pass. Windows Release build passes. Broader CTest
result is341/342: `UnityVulkanAndroidAdapter.ColorSourceViewsWarmOnceAndTypedBgraDoesNotSwap`
fails because pushed_swap is2 vs expected0. That adapter/test was not edited;
the shader/plugin source hashes exactly match the preserved baseline-files.json.
This unrelated dirty R4 test discrepancy is reported, not repaired by this task.

## Root capture and interpretation

Root stages only the diagnostic libhumanvision.so into fresh scratch
`out/android-yolo/exec-trace-20261001-v1/UnityProject`, copied from the reviewed
`out/android-yolo/eval-rectangle640-20261001-m3-safe-timing/UnityProject` template.
Keep model/profile/video/config640x384 FP32 and existing waits unchanged; run
the actual Unity GPU/AHB route and collect streaming logcat from launch.
Record APK/runtime/native/source/video identities and retain full logs.

```powershell
py -3 tools/test/ncnn_execution_trace.py --log <full-logcat.txt> --output <execution-summary.json>
```

This strict analyzer rejects the whole capture on truncated, failed, unavailable,
replayed or inconsistent records. Repeated model-layer execution within a sampled
frame is conservatively rejected rather than silently averaged. Wrapping ticks
and overlapping per-layer intervals are rejected; a device exposing them needs
separate investigation. Sparse sampling is diagnostic and instrumentation/query
reset/logging overhead prevents treating diagnostic FPS as an optimization.

`gpu_layer_us` sums validated compute-stage layer intervals. `submit_wall_us` and
`phase_submit_wall_us` include recording/submission/fence handling/post-copy work
inside existing submits. GPU timestamps exclude inter-layer gaps, transfers and
CPU recording outside those calls. Subtracting them does not yield pure CPU
overhead. Phase1 shows internal submission count/wall cost; compare it with the
existing extraction phase and per-layer GPU distribution, preserving these limits.


## Actual device interpretation and Task2 closure

**Task2 CLOSED: no new execution candidate selected.** The permitted next step
is Task3 intermediate-resolution eligibility, after independent review of this
closure. No backend, model, default, synchronization or precision change was made.

Root captured the actual Unity GPU/AHB video route on OnePlus9Pro/Snapdragon888,
serial `e7c07019`, with explicit640x384 FP32 and capacity8. Evidence directory:
`out/android-yolo/exec-trace-20261001-v1/device-yolo640-execution-trace-45s/`.
The report labels its observation window45seconds; metadata started/ended UTC
also covers capture setup/collection and must not be used as a45second FPS
numerator/denominator. This diagnostic is sparsely sampled and instrumented.
Its HUD samples/body counts do not establish joint accuracy or sustained FPS.

APK SHA256: `d168b945c9dc6a3a3aba20598420fcf416abb427d5b8b6e0b52c3f99152f3360`.
Native SHA256: `a9d5c3e726f8c7ebbfda3df4dd2e50822ef99fd14ef374fcc7370edb3d4afbdc`.
Video SHA256: `e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`.
Logcat SHA256: `cd755bf0c907c3dd18c6268695750000758a60ec24980b78d3fb02455261324c`.
Capture report SHA256: `f2e932f07cda3069253b58c33770190c00c5e68ac925e4c2402a7dbc8d08450f`.
Strict analysis SHA256: `e01023e04fbf44a49c9f0849a75c3a2a312d602afb852d6280803e1caf03c82d`.
Staged model param SHA256:
`908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905`.

Fresh strict reanalysis accepts all14 sampled frames and reproduces the original
analysis exactly. Every frame contains the204 non-input layers of the staged
205-layer graph with unambiguous zero-based param indices. Exclude first three
sampled frames3,5,56 as warm-up; remaining frame IDs are172,291,415,510,601,693,
784,884,1003,1123,1244. This exclusion applies only to layer-cost interpretation,
not to throughput acceptance. Type sums reproduce each frame's GPU-layer total.
Derived values are in `task2-layer-distribution.json` beside the strict analysis.

| Measured quantity, eleven warm samples | Minimum | Mean | Maximum |
|---|---:|---:|---:|
| Sum of layer GPU intervals |39.888ms|43.914ms|50.893ms|
| Sum of existing submission wall intervals |48.606ms|57.536ms|66.004ms|
| Internal extraction phase1 wall intervals |41.876ms|¡ª|57.639ms|
| Existing submissions per frame, all phases |7|¡ª|8|

| Layer type | Mean summed GPU time/frame | Share of summed GPU time |
|---|---:|---:|
| Convolution |37.263ms|84.854%|
| Swish |2.386ms|5.434%|
| Reshape |1.878ms|4.276%|
| Concat |1.307ms|2.977%|
| Slice |0.343ms|0.781%|
| Permute |0.241ms|0.549%|
| BinaryOp |0.220ms|0.501%|
| Pooling |0.158ms|0.360%|
| Interp |0.110ms|0.251%|
| Split |0.008ms|0.018%|

The dominant cost is distributed across convolutions. Highest mean intervals
include layer1 `conv_0`1.766ms,143 `conv_45`1.569ms,3 `conv_1`1.521ms,
59 `conv_20`1.501ms,38 `conv_13`1.384ms,17 `conv_6`1.360ms and
145 `conv_46`1.301ms. Several are3x3 stride2; pose heads45/46 have51 output
channels. This is not one anomalous operator with an evidenced single fix.
Layer timestamps enclose the complete layer implementation; they do not
identify individual shaders, transform-vs-GEMM cost or active Winograd23/43.
GPU-layer sums omit transfers and inter-layer gaps. The13.621ms mean difference
between summed wall and layer intervals cannot be labelled pure CPU overhead,
saved wait time or a removable optimization budget.

The official pinned [Vulkan convolution implementation](https://github.com/Tencent/ncnn/blob/e54f7b1f88434e1d844ea0551b880a1cfb079ce1/src/layer/vulkan/convolution_vulkan.cpp)
and [execution submission logic](https://github.com/Tencent/ncnn/blob/e54f7b1f88434e1d844ea0551b880a1cfb079ce1/src/net.cpp)
were inspected together with their hash-checked diagnostic copies. Convolution
source lines178,861 and1690 select Winograd/SGEMM by geometry/options; the
cooperative-matrix branches require FP16 storage or packing. Net's rough-score
pending-dispatch threshold explicitly avoids driver timeout; its existing
submit-and-wait decisions must remain. These findings do not authorize FP16,
forced Winograd variants, subgroup toggles or threshold/wait changes.

Global Winograd-off/SGEMM-on already achieved13.8766 fresh observations/s in the
[prior actual integration trial](2026-10-01-yolo-sgemm-integration.md), so it is
not a new evidence-selected candidate. [No-local-memory](2026-10-01-yolo-no-local-memory-integration.md)
already has a qualified explicit contract and measured17.107725 observations/s,
but its comparison was uncontrolled. This trace cannot establish its advantage
or justify promotion. It also cannot select an unmeasured Winograd23/43 variant
merely because convolution dominates. No new runtime contract or speculative
execution sweep is justified by this bounded evidence.

Fresh Task2 verification:

```powershell
py -3 tools/test/ncnn_execution_trace.py --log out/android-yolo/exec-trace-20261001-v1/device-yolo640-execution-trace-45s/logcat.txt --output out/android-yolo/exec-trace-20261001-v1/device-yolo640-execution-trace-45s/task2-revalidated-analysis.json
py -3 -m unittest discover -s tools/test -p test_ncnn_execution_trace.py
```

Strict analyzer exits0;14 focused tests pass. An independent Python calculation
maps all204 indices, compares original/revalidated JSON, checks each type sum and
recomputes the table. Task2 changes documentation only, so it introduces no new
build artifact or numerical/semantic eligibility claim. The earlier Task1 build
and broader test limitation remain as reported above. Root owns restoration of
the accepted APK; this task did not operate the phone or Unity. Target30 and
at-least25 complete observations/s remain unmet;25FPS video cannot certify30.
