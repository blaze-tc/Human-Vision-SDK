# S1 model reference report — 2026-09-07

## Decision

S1 numerical-reference acceptance passes. Select official RTMPose-s wholebody
FP32 ONNX for the S2 native adapter experiment; retain medium as a comparison.
This is not SDK recognition-quality or 30 FPS acceptance. D0 runtime/model ABI
is unchanged. Model assets remain evaluation-only, with redistribution status
unresolved as described in `S0_MODEL_FEASIBILITY.md`.

## Reproduction and evidence

Run from the repository root with the existing locked reference environment:

```powershell
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_wholebody_contract.py -v
.venv-reference/Scripts/python.exe -m tools.reference.prepare_wholebody
.venv-reference/Scripts/python.exe -m tools.reference.compare_wholebody --threads 1
.venv-reference/Scripts/python.exe -m tools.reference.compare_wholebody --threads 4
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v
```

- Initial RED: missing `tools.reference.wholebody_contract` implementation,
  recorded in `out/validation/s1/contract-red.log`. No behavioral failure is claimed.
- Final GREEN: 9/9 reference tests, 0.029 seconds. Five new tests cover schema,
  dtype/finite values, genuine hand-source indices and invalid-palm suppression.
- Both preparation downloads passed ONNX checker and SHA-256 recording.
- All four actual PyTorch/ONNX comparisons exited zero. Full JSON, PNG and logs
  are in `out/validation/s1/`; portable numerical records are in `tests/golden/s1/`.
- No C++/Unity targets changed in S1. Their fresh S0 builds and regression
  evidence remain recorded separately; no new native rich-skeleton pass is claimed.

`models/wholebody/candidates.json` pins publisher URLs, checkpoint/ONNX/config
hashes, source commit and tensor contracts. Both ONNX graphs use opset 11,
float32 input `[N,3,256,192]` and actual outputs `[N,133,384]`, `[N,133,512]`.
Reference is the locked real `human-pose.jpg` image and D0 person bounding box;
official MMPose preprocessing and source-coordinate restoration are used with
flip testing disabled. Package versions and input/image hashes are in the records.

## Numerical agreement

| Candidate | Maximum raw error | Restored coordinate error | Score error |
| --- | ---: | ---: | ---: |
| Small | 0.0000055581 | 0 pixels | 0.0000017583 |
| Medium | 0.0000060797 | 0 pixels | 0.0000022650 |

Both thread settings passed the unchanged limits: raw <= 0.002, source-space
coordinate <= 0.5 pixels, score <= 0.005. Actual dynamic batches 2/4/8 executed
for both models and matched repeated single-input outputs with zero maximum
raw difference. These batch executions were correctness checks, not timed
batch-throughput measurements or distinct-person tests.

## Measured compute cost

Windows Ryzen 7 4800H; ONNX Runtime 1.23.2 CPUExecutionProvider; PyTorch CPU
reference uses one thread. Five warmup runs, 20 measurements per workload.
Numbers below are mean milliseconds for serial inference on N copies of one
real preprocessed person crop, excluding detection, preprocessing, decode,
tracking, display and transfers. N is a compute workload, not observed people.

| Model / ORT threads | N=1 | N=2 | N=4 | N=8 | N=8 p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small / 1 | 24.465 | 49.093 | 96.288 | 193.888 | 201.980 |
| Small / 4 | 11.620 | 23.700 | 46.429 | 89.512 | 96.360 |
| Medium / 1 | 56.521 | 111.670 | 221.367 | 442.287 | 457.724 |
| Medium / 4 | 25.136 | 46.990 | 91.022 | 187.943 | 197.838 |

Small/four-thread N=8 corresponds to 11.17 model-only cycles/second, already
below 30 before the other stages. This CPU configuration does not meet the
target. GPU/RKNN execution and sustained RK3588 thermal behavior are unmeasured.

## Hand semantics and limitations

Thumb indices are 95/116; Handtip uses middle fingertip 103/124. Foot samples
use big toes 17/20. Hand/palm is the mean of hand root plus four MCP landmarks:
left `[91,96,100,104,108]`, right `[112,117,121,125,129]`. Palm provenance is
explicitly `derived_from_hand_model`; confidence is the minimum constituent
confidence, and any subthreshold constituent suppresses the palm output.
These are adapter conventions, not exact anatomical equivalence to Kinect.

All six hand endpoints passed the sample's 0.3 confidence threshold in both
models. The inspected source image contains gloves and small hands: confidence
and numerical parity do not establish finger accuracy. Real visible-hand,
occlusion, motion and actual eight-person evidence is still required in S3/S5.
No repeated frame, interpolated joint or synthetic wrist offset counts as a
fresh observed hand output.

Next milestone is S2: backward-compatible native rich result, real wholebody
adapter, provenance/validity, same-frame body/hand association and snapshots.
