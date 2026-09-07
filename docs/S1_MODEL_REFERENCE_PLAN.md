# S1 wholebody model reference implementation plan

**Goal:** Establish reproducible ONNX/PyTorch agreement and measured CPU cost
for small/medium wholebody models with real hand and foot outputs.
**Architecture:** Offline Python experiment only; retain native D0 and Unity
runtime unchanged. Reuse locked MMPose preprocessing and decoding for golden
comparison. Keep model conversion and production result ABI outside S1.
**Spec:** `SDK_SKELETON_REQUIREMENTS.md`; candidate evidence:
`validation/S0_MODEL_FEASIBILITY.md`.

## Files and tests

- Create `tools/reference/wholebody_contract.py`: validate 133-joint SimCC
  outputs and extract semantic hand/foot samples with confidence/provenance.
- Create `tests/reference/test_wholebody_contract.py`: malformed 17-joint or
  mismatched-batch output rejection, finite values, thumb/tip indexing, palm
  derivation from actual hand landmarks and invalid-constituent suppression.
- Create `tools/reference/compare_wholebody.py`: run official PyTorch and ONNX
  on the same preprocessed real image, preserve raw and decoded error metrics,
  time serial 1/2/4/8-ROI workloads after warmup, and save JSON/visual evidence.
- Create `models/wholebody/candidates.json`: exact published URLs, source commit,
  downloaded ONNX/checkpoint hashes and input/output contract; candidates are
  evaluation-only and not approved release assets.
- Save experiment outputs under `out/validation/s1/` and the compact measured
  report under `docs/validation/S1_MODEL_REFERENCE_REPORT.md`.

## Ordered execution

1. Write the contract tests before implementation. Run
   `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference
   -p test_wholebody_contract.py -v`; expect missing contract implementation.
2. Implement `validate_simcc(x, y)` for float32 finite arrays shaped
   `[N,133,384]` and `[N,133,512]`, matching positive N. Implement
   `semantic_endpoints(points, scores, threshold)` for exactly 133 source-space
   points and scores, preserving each output's source indices and validity.
3. Run the focused tests and existing reference contract tests. Reject wrong
   shapes/NaNs; never quietly reinterpret the COCO-17 model as wholebody.
4. Obtain publisher ONNX archives/checkpoints listed in S0. Record SHA-256,
   inspect archive paths before extracting only ONNX and publisher metadata,
   and validate the ONNX graph. Keep binaries Git-ignored. Pin source config
   and dataset keypoint metadata from the existing MMPose checkout.
5. Use `init_model`, the official `Compose`/`PoseDataPreprocessor` pipeline and
   `model.head.forward` with flip testing disabled. Compare ONNX raw SimCC
   outputs and the official decoder's source-space result on the same input.
   Initial acceptance limits: raw max absolute error <= 0.002, score error <=
   0.005 and coordinate error <= 0.5 pixels, matching D0. Document any genuine
   export-contract disagreement instead of silently weakening limits.
6. Save all 133 keypoints and semantic hand/foot samples from the real image.
   Render an annotated crop for visual inspection. Numerical agreement does
   not prove hand anatomical accuracy; record obscured/small hands as limitations.
7. Benchmark serial N=1/2/4/8 model runs on one real preprocessed crop, after
   warmup. Label workloads as repeated-ROI compute probes, not actual N-person
   accuracy or end-to-end throughput. Capture runtime/provider, thread settings,
   mean/p50/p95/max timings and hashes. Test actual batched input separately only
   if the graph supports it; do not change graph shapes to force acceptance.
8. Re-run reference tests and affected golden comparisons, inspect the diff,
   update development status and commit declared verified files only.

## Advancement

S1 completes when the experiment is reproducible and at least one selected
candidate passes the documented numerical contract, with actual hand/foot
outputs and compute measurements recorded. A candidate may be rejected on
accuracy/performance grounds. Only a documented passing contract may feed S2.
Failure to meet 8-person 30 FPS is reported explicitly and does not turn a
reference-model pass into an SDK performance pass.

RK3588 board conversion, runtime throughput and sustained thermal behavior are
not executed by this CPU reference experiment. They remain S4 acceptance gates.
