# YOLOv8n-pose local model eligibility

Status: **ELIGIBLE_FP32_OFFLINE_ONLY**, pending final independent review/commit.
Storage16 fails frozen raw numerical limits on all seven initial fixtures.
The separately authorized storage32/compute32 Vulkan candidate passes11/11
fresh execution-bound fixtures under the unchanged limits. No production
integration, release, merge or FPS acceptance.

This is the bounded YOLO-M1 candidate authorized by the route adjustment in
`docs/superpowers/plans/2026-09-27-android-rtmo-t-integration.md`. Existing R4
changes are preserved. The failed RTMO route remains closed.

## Provenance and distribution boundary

Repository: [nihui/ncnn-android-yolov8](https://github.com/nihui/ncnn-android-yolov8/tree/f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca),
clean detached revision `f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca`.
No repository `LICENSE` file exists in that revision. The decoder source carries
a BSD-3-Clause notice. Its export instructions name Ultralytics
`yolov8n-pose.pt`; a decoder notice does not establish trained-weight rights.
Weights remain ignored local evaluation data. No redistribution or commercial
license determination is claimed.

| Upstream asset under `app/src/main/assets` | SHA-256 |
|---|---|
| `yolov8n_pose.ncnn.param` | `908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905` |
| `yolov8n_pose.ncnn.bin` | `6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9` |

`out/android-yolo/provenance.json` binds these hashes and the exact authoritative
`app/src/main/jni/yolov8_pose.cpp` decoder hash. No conversion was attempted.

## Observed tensor and decoder contract

The authoritative upstream source describes dynamic target sizes320/640 and
the checked graph has dynamic reshape/permute operations. Input name `in0` is
RGB CHW divided by255. Resize the longer source dimension to target, truncate
the other dimension to an integer, and pad each dimension to a multiple32.
Padding114 is split with integer floor on left/top. This is a rectangle, not
a forced square.

The offline oracle uses OpenCV `INTER_LINEAR` uint8 resize and publishes one
identical FP32 tensor to CPU/GPU. It does not prove byte parity with upstream
`ncnn::from_pixels_resize`, production GPU preprocessing or VideoPlayer/AHB.

Raw `out0` has65 columns: four16-bin DFL distributions plus one person logit.
Raw `out1` has51 columns:17 triples of raw x/y/visibility. Candidate rows are
row-major grids for strides8,16,32 concatenated in that order. Box distances
are softmax expectations times stride, centered at `(grid+.5)*stride`.
Joints use `(grid + raw_xy*2)*stride`; visibility and person scores use sigmoid.
NMS uses0.25 score threshold and0.45 IoU, matching upstream. Source restoration
subtracts the integer pad and divides by scale. Boxes are clipped; joints are
not clipped or synthesized. Tie sorting is deterministic by anchor index.

Frozen pre-device budgets: raw maximum absolute error0.2/mean0.01 for both
outputs; exact expected person count; unique CPU/GPU association IoU>=0.95;
person/joint confidence error<=0.01; all17 joint coordinate error<=3 source
pixels. Seven independent manual person rectangles additionally require unique
coverage with IoU>=0.3. This loose manual-presence threshold is separate from
the strict CPU/GPU box threshold. No threshold may be relaxed to obtain a pass.

The tracked runner uses acquired blob/workspace/staging Vulkan allocators that
outlive Net/Extractor/VkMat. Commands wait before allocation reclamation. Input
is explicitly pack1 (three RGB channels), output explicitly FP32pack1. Mode
`gpu` uses storage16/input16/compute32; mode `gpu-fp32` separately uses
storage32/input32/compute32. Both explicitly enable internal network packing
(`use_packing_layout=true`), disable subgroup operations and FP16 arithmetic,
and set2 CPU threads. Boundary pack1 does not disable internal network packing. It
fails any layer lacking Vulkan support; no CPU fallback is permitted in GPU
mode. CPU fixture upload is confined to this offline research executable.

## Bounded precision adjustment

Root's seven416 Adreno660 run executes all205 Vulkan-supported layers and
returns finite correctly shaped outputs, but storage16 raw `out0` maximum
error0.2607879639 exceeds the frozen0.2 limit. Mean0.0082514955 passes.
The worst value is person logit row1818,column64, CPU-16.963913 versus
GPU-16.703125; DFL-only maximum0.11084843 passes. The decoded seven people have
IoU>=0.99845, maximum score error0.008504 and joint error0.491352px. These
semantic successes do not waive the raw FAIL. All seven initial storage16
fixtures fail at least one frozen numerical limit; original logs/outputs remain.

The exact cause is unproven. Storage/intermediate quantization is a candidate
cause, not a measured diagnosis. Root authorized one explicit FP32 execution
candidate retaining the unchanged CPU reference, graph, weights and limits.
The initial seven416 FP32 diagnostic has raw maxima9.82e-5/2.10e-5 and seven
matched people. These initial manual executions predate the fresh execution
binding below and remain diagnostic only.

Future integration must declare storage32/input32/compute32 and internal packing
truthfully. The existing Android backend has hard-coded detector/body shape
and FP16 role validation, and sets network packing from boundary elempack.
It requires a deliberate generic contract extension in M2; this offline runner
does not establish that the current backend already supports this candidate.
There is no runtime precision fallback. Performance is unmeasured.

Separate seven/one-square320 and square416 fixtures now use the same original
RGB images, symmetric114 padding, and their own manifests/geometry/hashes. They
do not replace upstream rectangle fixtures. Seven-square416 is416×416 with
top91; seven-square320 is320×320 with top70. The fixed-square candidate must
pass its own numerical/person gate before an adapter uses that contract.

## Fresh execution binding

`yolo_device_gate.py` creates a new UUID-named archive and remote directory per
fixture. It archives exact fixture/input/source pixels and ELF; records real adb
serial/fingerprint/model; binds runner source+CMake+ELF, pinned model hashes,
input/geometry/options, CPU/GPU exit codes, logs and output hashes. Execution
state is invalidated to RUNNING before each invocation and marked SUCCESS only
after both successful runs and complete output pulls. Failure never consumes
earlier output files. `yolo_pose_gate.py` requires matching SUCCESS execution
evidence, modes, device, options and hashes before PASS. Manual loose output
files alone no longer satisfy eligibility.

## Host evidence

Host runner linked against pinned ncnn20260526. All seven fixture executions
exit0, have finite outputs and match the checked dynamic output shape contract:

| Fixture | Input W×H | Rows | Detected/expected |
|---|---|---|---|
| seven320 |320×192|1260|7/7|
| seven416 |416×256|2184|7/7|
| seven640 |640×384|5040|7/7|
| one320 |224×320|1470|1/1|
| one416 |288×416|2457|1/1|
| one640 |416×640|5460|1/1|
| empty416 |416×256|2184|0/0|

Seven source: exact saved sequential frame1500 from `video-1.mp4`, video SHA
`e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`,
1024×576 RGBA SHA
`83db08727c2db6aafe2369e71f689428c3e9197176f2d10933a839b141ab9b67`.
The source frame is reused from the hash-verified R4 manifest, including all
seven existing manual annotations. Seven416 tensor SHA
`377e6cac23559bbef87e06dfc95dcd71f0fa91de78703a4ed16bfd7c863d1c39`.
One source: existing `tests/testdata/d0_3_one_person.mp4`, OpenCV seek1000ms,
same convention as earlier RTMO eligibility; source/frame hashes are captured.
One416 tensor SHA
`c174a3e1342af9c244485c0e5ce57dce0858cf2133161971470b3f7eb1da8044`.

Seven416 uniquely associates all seven annotations (IoU0.383–0.762). Visual
inspection of its host overlay shows heads, trunks and legs attached to all
seven people; several raised arms are missing or mislocated by the model.
This is not complete-joint accuracy or physical acceptance. Seven320 has one
person score0.2549 near the0.25 decision boundary; seven416 minimum0.4801.

Evidence is ignored under `out/android-yolo/`: provenance, per-fixture metadata,
raw CPU outputs, logs, decoded reference JSON and overlays. The parent task owns
Android build/device execution and its evidence hashes.

## Final Android numerical eligibility

Root executed both CPU and Vulkan FP32 modes for every fixture on device
`e7c07019` (Adreno660) with the same freshly built archived ELF. Every runner
returns0, all205 layers support Vulkan, input/output packing and dtype checks
pass, and all raw outputs are finite. A fresh post-review recomparison validates
all11 execution records, logs, input/model/runner/output hashes and unchanged
numerical/person limits.

The reference is CPU ncnn execution of the same pinned converted graph. This
does not establish PyTorch/Ultralytics export parity or original training
accuracy; shared graph or decoder faults could agree in both execution modes.

| Fixture | CPU/GPU people | Raw out0 max abs | Raw out1 max abs |
|---|---|---|---|
| seven416 rectangle |7/7|0.0000982285|0.0000209808|
| one416 rectangle |1/1|0.0000925064|0.0000200272|
| seven320 rectangle |7/7|0.0000755787|0.0000215769|
| one320 rectangle |1/1|0.0001869202|0.0000234842|
| seven640 rectangle |7/7|0.0001411438|0.0000355244|
| one640 rectangle |1/1|0.0001001358|0.0000431538|
| empty416 rectangle |0/0|0.0001344681|0.0000384450|
| seven320 square |7/7|0.0001163483|0.0000261068|
| one320 square |1/1|0.0001831055|0.0000312328|
| seven416 square |7/7|0.0001058578|0.0000314713|
| one416 square |1/1|0.0000848770|0.0000274181|

Across all matched people, minimum CPU/GPU box IoU0.9999984421, maximum person
score error0.000002845474 and joint-coordinate error0.0003051758 source pixels.
All seven-person fixtures additionally pass unique association with the seven
independent manual rectangles. This verifies implementation parity, not the
trained model's complete-joint accuracy. Raised-arm errors remain; COCO17 has
no Hand/Handtip/Thumb observations. Unavailable or low-confidence points must
remain invalid; this task adds no hand pipeline or fabricated points.

Final runner ELF SHA-256:
`87b20ad8289eb0b5b449683bb6b096297221195307b1d66b59502cf49b96a14d`.
Final runner source SHA-256:
`5d9a2b3ae19239d22f66d77fe6bba52206c700c522fed93810ede4a839d37814`.
The tracked [evidence index](../../tools/models/ncnn/yolo_model_gate_evidence.json)
contains every immutable archive UUID and execution/fixture/input/log/output
hash, actual device fingerprint, model/runner hashes and per-fixture results.
Root's invocation summary is `out/android-yolo/device-fp32-run-summary.log`;
final recomparison summary is `out/android-yolo/final-gate-summary.json`.

Only the explicit FP32 execution contract is eligible for a separately reviewed
M2 adapter. Square320/416 both pass this fixed-fixture gate; selecting a size for
>=25 fresh complete all-person frames/s requires integrated device measurement.
The320 rectangle's near-threshold person score and raised-arm limitations remain
visible accuracy qualifications. No precision/runtime fallback is authorized.

## Reproduction and checks

```powershell
.venv-reference/Scripts/python.exe tools/models/ncnn/yolo_prepare.py
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_yolo_pose_gate.py -v
.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py
```

Decoder/gate tests: missing-module RED, then8/8 GREEN; independent annotation
test missing-symbol RED, then9/9 GREEN; final suite15/15 PASS. Coverage includes analytic DFL, stride
offsets, sigmoid/keypoint equations, NMS, empty output, malformed/nonfinite raw
output, missing people, duplicate annotation credit, numerical drift, rectangular
source restoration, off-image joints, dynamic geometry, missing output evidence
and tampered input hashes. Precision-mode and square geometry changes each had
their own RED before implementation. Regression controls reject stale outputs
after failed/running execution and mismatched mode/model/runner/options/device/
log/output binding. Malformed evidence overwrites the gate with FAIL,
preventing stale saved PASS reports.
Review's array/null metadata case reproduced three stale-PASS failures before
explicit object checks; the repaired15-test suite passes and CLI controls
overwrite old PASS with FAIL for malformed fixture/execution/stage structures.
Architecture/public surface guards PASS.

Configure `tools/models/ncnn/yolo_runner` with the matching `ncnn_DIR`; Android
uses `out/ncnn-20260526/android-arm64-api26/install`. Runner CLI:

```text
yolo_golden_runner PARAM BIN FIXTURE_DIRECTORY cpu|gpu|gpu-fp32 WIDTH HEIGHT
```

Fresh Android execution, owned by the root task:

```powershell
.venv-reference/Scripts/python.exe tools/models/ncnn/yolo_prepare.py --squares-only
.venv-reference/Scripts/python.exe tools/models/ncnn/yolo_device_gate.py --adb ADB_PATH --serial e7c07019 --runner out/android-yolo/runner-android/yolo_golden_runner --gpu-mode gpu-fp32 --fixtures seven-416 one-416 seven-320 one-320 seven-640 one-640 empty-416 seven-square320 one-square320 seven-square416 one-square416
```

After execution, use its printed immutable archive directory:

```powershell
.venv-reference/Scripts/python.exe tools/models/ncnn/yolo_pose_gate.py ARCHIVE_DIRECTORY --gpu-mode gpu-fp32
.venv-reference/Scripts/python.exe tools/models/ncnn/yolo_reference.py ARCHIVE_DIRECTORY --mode gpu-fp32
```

Only raw model outputs are downloaded. The seven/one/negative controls,
dynamic320/640 rectangles and fixed320/416 squares pass actual Vulkan execution,
finiteness, shapes, numerical and person comparisons in the explicit FP32 mode.
Offline fixtures are not25/30 fresh complete all-person FPS evidence.
