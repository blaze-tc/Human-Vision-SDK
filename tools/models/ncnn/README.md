# Local Android ncnn model eligibility

`prepare_rtmpose_eval.py` is the bounded Revision3 Body26 conversion entry.
`build_local_eval_pack.py` produces only the hash-pinned local evaluation pack.
The reusable C1 graph audits remain strict; they do not accept MMDeploy custom
operators or symbolic ONNX outputs. This one official graph uses the separate
fixed-graph proof, runtime output-size checks and four-image device golden.

The path is pinned official MMDeploy FP16 export, zero fourth input channel in
the first Conv, first ScaleNorm `ReduceL2`, pinned MMDeploy converter and ncnn
optimizer, then the existing twelve shape-only `Reshape` substitutions.
Only the first normalization is fused; every other ONNX node is unchanged.
The ncnn weight file remains byte-identical to the padded baseline. The model
has 166 Vulkan layers after replacing four arithmetic layers with one.

The profile keeps ID `android-ncnn-vulkan` and its original hash. Model records
carry `backend_options`: detector uses subgroup and FP16 arithmetic; body uses
neither. Both use FP16 packed/storage, detector pack1 and body pack4. The
schema-2 capability `fp16-arithmetic` describes the required device capability;
it does not require every model to perform arithmetic in FP16. Missing or
different role options fail before model loading. Profile/Host scheduling is
outside Task2; no ABI table changes are needed for this model contract.

From the repository root (all assets remain under ignored `out/`):

```powershell
.venv-reference/Scripts/python.exe -m tools.models.ncnn.prepare_rtmpose_eval --export --source-onnx out/c3-local-runtime/official-replay/model.onnx --checkpoint out/c1-source-cache/rtmpose-t_body26.pth --vendor-root out/c2-vendor --image out/c2-detector/golden/official/image.png --converter out/c3-local-runtime/official-converter-build-v2/onnx2ncnn/Release/mmdeploy_onnx2ncnn.exe --optimizer out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe --output out/c3-local-runtime/body26-replay
```

The converter executable must match its hard-coded SHA-256; its upstream source
build, compiler/protobuf hashes and original command are recorded in
`docs/validation/RTMPOSE_NCNN_CONVERSION_GATE.md`. An already exported official
ONNX may be passed without `--export`; its exact hash is still mandatory.

Build `pose_golden` and `first_norm_probe` from `pose_golden/CMakeLists.txt` with
the audited ncnn Android installation. The pose runner is a standalone test
tool; its explicitly named CPU/layer-dump modes are diagnostics and are never
eligible through the golden harness. Eligibility requires its Vulkan layer
audit, explicit GPU conversion to FP16 pack4 `VkMat` input, FP32 arithmetic
and subgroup=false route. Only the two small outputs are converted on GPU to
FP32 pack1 and downloaded. This is a fixed-fixture model test, not AHB import
or the Task4 camera path:

```powershell
.venv-reference/Scripts/python.exe -m tools.models.ncnn.run_pose_golden --checkpoint out/c1-source-cache/rtmpose-t_body26.pth --vendor-root out/c2-vendor --image out/c2-detector/golden/official/image.png --onnx out/c3-local-runtime/body26-replay/model.onnx --param out/c3-local-runtime/body26-replay/vulkan.param --weights out/c3-local-runtime/body26-replay/model.bin --runner out/c3-local-runtime/first-norm-reducel2/production-runner-build/pose_golden --output out/c3-local-runtime/body26-replay/golden --adb D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe --runner-mode strict-vkmat > out/c3-local-runtime/body26-replay/four-case.log 2>&1
```

Use a new golden directory. The builder additionally requires the detector
regression index and all eight raw tensors under `detector/`; the reproduction
script and pinned hashes are in the validation report. It rechecks the immutable
four-case pose index, every image/crop/reference/candidate/repeat hash, original
strict metrics, eight unique runtime audit records and original logs, model hashes, exact conversion
provenance and audited patch chain before producing a pack. The runtime route
manifest binds the pinned runner/source/model hashes to each input/output hash;
pack verification rechecks each original log and its exact VkMat/options/shape
contract against the immutable golden index. Mat-only evidence is ineligible:

```powershell
.venv-reference/Scripts/python.exe -m tools.models.ncnn.build_local_eval_pack --pose-dir out/c3-local-runtime/strict-vkmat --output out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v
pwsh -NoProfile -File tools/test/run_native_tests.ps1
.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py
```

Cache-dependent model tests skip when the local model evidence is absent. The
recorded Task2 verification ran them with real assets present; native model
resolution and tampered hash/path rejection also ran. No hardware FPS or
trained-weight redistribution acceptance follows from these tests.
