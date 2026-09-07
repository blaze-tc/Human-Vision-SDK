# MODEL_MANIFEST.md

# D0 Model Baseline

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
