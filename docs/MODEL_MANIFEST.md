# MODEL_MANIFEST.md

# D0 Model Baseline

## Revision3 Task2 local Body26 eligibility (2026-09-26)

The bounded first-ScaleNorm `ReduceL2` candidate passes the original four-image
Body26 gates on Snapdragon 888 using explicit GPU conversion to FP16 pack4
`VkMat` at `Extractor::input`. All four cases passed twice with byte-identical
repeats; all 166 layers support Vulkan. Only SimCC outputs are converted on GPU
to FP32 pack1 for download. This fixed-fixture model test does not implement
AHB import or the Task4 camera path. Mat-only evidence cannot promote a pack.
The pack includes all eight route logs and binds runner, source, model,
input/output hashes and exact geometry/options to the pinned golden index.
Reproduction starts at
[`tools/models/ncnn/README.md`](../tools/models/ncnn/README.md); exact hashes,
RED/GREEN and numerical results are in
[`RTMPOSE_NCNN_CONVERSION_GATE.md`](validation/RTMPOSE_NCNN_CONVERSION_GATE.md).
The local schema-2 pack has `local_evaluation_only=true`; source licenses and
conversion provenance are included, while trained-weight and dataset public
redistribution rights remain unverified.

Only the first norm's Abs/Pow2/ReduceSum/Pow0.5 becomes ReduceL2, preserving its
input/output names, axes, keepdims and all downstream consumers. The second norm
is unchanged. Its output is stored in FP16 only after the FP32 square-root;
intermediate squared sums no longer saturate at 65504. First-Conv zero padding
and the twelve previously proven shape rewrites remain identical. The weight
file is unchanged from that padded baseline. Fusion reduces 169 layers to 166;
all 166 were Vulkan-supported in every golden execution.

**Task2 contract ruling:** keep the existing profile bytes/ID and V1/V2 ABI.
The selected ModelPack's `models[i].backend_options` binds detector
`use_subgroup_ops=true,use_fp16_arithmetic=true` and body
`use_subgroup_ops=false,use_fp16_arithmetic=false`; both retain FP16 storage.
The runtime validates these role options before loading the Net. The profile
still selects this pack and requires Vulkan/FP16 capabilities with fallback
false. Task5 owns later profile-to-Host scheduling/composition validation.

The official ONNX has symbolic output declarations and MMDeploy custom pooling;
it does **not** pass the unchanged generic C1 ONNX audit. This single pinned
candidate uses its own fixed-graph proof: batch1 input, 26 preserved rows,
384/512 classifier lengths, exact graph hashes, and actual GPU output shape,
packing and byte-count checks. The pack declares `[1,26,384]`/`[1,26,512]`
FP32 downloads bounded at 39936/53248 bytes. No ONNX output metadata, generic
operator allowlist or golden threshold was changed to obtain eligibility.

## Android ncnn C1 conversion contract (2026-09-25)

The Android production candidate uses a **separate** `precision-t-26-ncnn-fp16`
ModelPack; it does not replace the D0 RTMDet-tiny/RTMPose-s baselines below.
`tools/models/ncnn/` pins the RTMDet Nano checkpoint SHA-256
`05d8511e7b3fabc62e27d2f624179e004ad14ee63a86ca9d9d22c88f3db0eee1`
and the existing OpenMMLab source revisions. The RTMPose-t Body26 source is
the official checkpoint linked in MMPose's pinned model card; its full SHA-256
is `6020f8a6746639c0144eb979df0be0baa707af428d0e20a2db8991cf7452e5d6`
(verified from the downloaded 14,130,769-byte `.pth`). Both exporters check the
full checkpoint SHA-256 before loading the model.
Each exporter also requires a clean, exact-revision vendor checkout before
adding it to Python's import path; tracked, untracked and ignored files fail
closed so an unpinned module cannot shadow the source. C1 Python tools accept
both `python -m ...` and direct script invocation from the repository root.

The candidate detector input is static NCHW `[1,3,320,320]` RGB with bilinear
letterbox/pad RGB 114 and explicit RGB mean `[123.675,116.28,103.53]`, norm
`[1/58.395,1/57.12,1/57.375]`. Its named `cls` output is raw, objectness-fused
logits and `bbox` is stride-scaled LTRB distance, before TopDown grid decode,
thresholding and person-only NMS. The candidate pose input is static NCHW
`[1,3,256,192]` RGB, bbox affine crop with factor 1.25, the same RGB mean/norm,
and named `simcc_x` / `simcc_y` outputs for 26 joints. The detector's
three-channel ncnn graph requires FP16 pack1 at `Extractor::input`; device
comparison against FP16 pack4 diverges at its first convolution. The pose
input contract remains FP16 pack4. Input `VkMat` conversion is explicit in
the native backend; the converter does not prove runtime packing. The ncnn
graph audit rejects unsupported/custom or cast layers,
dynamic input/output shapes, and unnamed outputs. The ONNX image input and
numeric outputs must be FLOAT32; FP16 pack1 for detector and FP16 pack4 for
pose are separate ncnn runtime input contracts. Detector outputs must share a
batch-1 candidate axis, with one person
logit and four LTRB coordinates per candidate. Body26 SimCC outputs must have
26 joints and static x/y lengths of 384/512 (split ratio 2).
ONNX models with external tensor sidecars are rejected before loading those
bytes; the manifest's ONNX SHA-256 therefore binds the complete graph data.
In the ncnn param audit, negative dimension rejection is limited to declared
Input/Reshape dimensions; a `BinaryOp` scalar value of `-1` is valid.

The C1 tools require checkpoint/ONNX/tool/fixture hashes, capture export and
conversion commands, source revisions, opset and output hashes. Golden comparison
uses identical post-NMS candidate counts, bbox IoU ≥0.95, score error ≤0.01,
no missed reference person above threshold. Detector goldens are person-only:
an explicit nonperson entry is an error even if its score is below threshold.
Pose uses equal valid masks,
normalized joint distance P95 ≤0.01 and max ≤0.03 of bbox diagonal, confidence
error P95 ≤0.02, with at least three valid Body26 joints. Repeated ncnn outputs
must pass against both the candidate run and the reference, preventing cumulative
drift. Conversion provenance requires canonical top-level SHA-256 hashes for
both `onnx2ncnn` and `ncnnoptimize`. The PowerShell converter verifies the actual
tool files before execution; Python audit/comparator CLIs verify those files when
their optional paths are supplied. All conversion/golden checks require the
hashes even without local tool paths. Unit fixtures only exercise these checks. Real converted
graphs and golden parity remain C2/C3 gates;
they are not established by C1.
The pose comparator reads image dimensions from the SHA-bound fixture image and
accepts only a positive, ordered bbox inside that image. Its bbox comes from
the separately SHA-bound reference output; candidate/repeat bbox fields, when
present, must match. This prevents an inflated reference bbox from reducing
normalized joint error. The ncnn and ONNX graph audits also require each
terminal output to depend on image input `in0`; constant branches may contribute
to an output but cannot replace the image-dependent path. `build_provenance`
checks any caller-supplied input contract, output names and output contract,
including the latter against the actual static ONNX graph before recording it.
The detector golden threshold is fixed to the production profile's
`detector.person_score_threshold = 0.35`, matching the existing TopDown
`Detect(..., .35F, ...)` call and the reference export's runtime threshold.
The golden manifest records the canonical `android-ncnn-vulkan` profile ID and
its SHA-256. Its comparator reads that checked-in profile, rejects a changed
hash or threshold, and offers no CLI threshold override. Revision 2's
profile-owned production behavior takes precedence over C1's original file map;
adding this one field to `profiles/android-ncnn-vulkan.json` is the C1 ruling.
The C3 GPU pipeline must consume the same profile field before production
acceptance; C1 tooling alone does not change runtime inference behavior.

Revision 2 §8.3 requires each downloaded tensor's static shape and maximum byte
count in the production ModelPack before runtime starts. C1 derives
`output_contract` entries (`shape`, `download_dtype=fp32`, `max_bytes`) from each
actual static ONNX output, checks ONNX shape inference against its declaration,
and caps each FP32 pack1 download at 16 MiB. No production output dimensions are
pre-filled here: C2/C3 must export and audit the real detector/pose graphs, then
copy their verified entries into the schema-2 ModelPack. Conversion refuses a
different pinned input contract, checkpoint, source revision, or output shape.
Both golden comparison commands recheck checkpoint, ONNX, param, bin, fixture,
reference, candidate and repeat SHA-256 values before accepting a result.

The goal of this file is to stop model selection from becoming an open-ended Codex task.

## Detector - locked for D0

- Family: RTMDet
- Config identifier: `rtmdet_tiny_8xb32-300e_coco`
- Input target: `640x640`
- Dataset/schema: COCO
- Runtime classes: keep `person` only
- Canonical source: MMDetection official config/checkpoint
- Deploy target: ONNX, then ONNX Runtime
- Precision baseline: FP32

The official MMDetection documentation uses the same config identifier in its installation/inference example. RTMDet-tiny uses 640 input in the official RTMDet model table.

## Pose - locked for D0

- Family: RTMPose
- Config identifier: `rtmpose-s_8xb256-420e_coco-256x192`
- Input: `256x192` (model codec input size `(192,256)`)
- Dataset/schema: COCO-17
- Decoder: SimCC
- `simcc_split_ratio`: `2.0`
- Preprocess mean: `[123.675, 116.28, 103.53]`
- Preprocess std: `[58.395, 57.12, 57.375]`
- BGR-to-RGB: true according to official config
- Precision baseline: FP32

## Export/reference rule

Do not invent preprocess/postprocess from memory.

For D0.1:

1. run the official PyTorch model on reference media
2. export ONNX using the official MMDeploy/MMPose deployment path when practical
3. save a `model_info.json` next to each exported model containing:
   - source config identifier
   - checkpoint filename/source
   - export command
   - opset
   - input/output tensor names
   - tensor shapes
   - preprocessing parameters
   - postprocessing/decoder parameters
   - sha256 of ONNX file
4. compare ONNX Runtime output against Python reference before using the model in the C++ pipeline

## Expected local paths

```text
models/
  detector/
    rtmdet_tiny_640.onnx
    model_info.json
  pose/
    rtmpose_s_256x192.onnx
    model_info.json
```

Model binaries do not need to be committed if repository policy avoids large files. In that case commit deterministic download/export scripts and checksums.

## D0 fallback policy

If RTMDet-tiny ONNX export is blocked by a toolchain incompatibility, Codex may use an official MMDeploy-produced ONNX artifact for the **same model config**, but must document exact origin and tensor contract. Do not silently switch to YOLO or another detector.

If RTMPose-s is too slow on the target Windows test machine, record the benchmark first. `RTMPose-t` may be evaluated only after D0 correctness is established; do not change the baseline preemptively.

## S1 wholebody evaluation — 2026-09-07

The active S-series plan permits a separate wholebody experiment. Official
RTMPose-s/m wholebody FP32 artifacts and exact hashes are pinned in
`models/wholebody/candidates.json`; reproduction and numerical/compute results
are in `validation/S1_MODEL_REFERENCE_REPORT.md`. Both passed the reference
contract. Small FP32 is selected for the S2 native adapter experiment.

Input is float32 `[N,3,256,192]`; actual SimCC outputs are `[N,133,384]` and
`[N,133,512]`. These are distinct from the locked 17-joint D0 pose model and
must never be silently interpreted using the existing ABI. No D0 model was
replaced in S1. The artifacts are evaluation-only, not cleared release assets.
Hand confidence/parity is not anatomical accuracy or eight-person acceptance.

## Segmentation / cutout (deferred)

Not part of D0/D1.

Future POC candidates include:

- RTMDet-Ins-tiny for instance masks
- dedicated human segmentation models for higher mask quality

The decision will be made after the skeleton/RTSP Demo is usable. Do not couple mask model choices to the D0 public API.

## References

- MMDetection RTMDet config: `configs/rtmdet/rtmdet_tiny_8xb32-300e_coco.py`
- MMDetection RTMDet docs/model table: `configs/rtmdet/README.md`
- MMPose RTMPose config: `configs/body_2d_keypoint/rtmpose/coco/rtmpose-s_8xb256-420e_coco-256x192.py`
- MMPose deployment guide: `docs/en/user_guides/how_to_deploy.md`
