# RTMO-t416 ncnn model gate: failed numerical eligibility

The bounded official-ONNX-to-ncnn route failed on 2026-09-27. Do not integrate,
package, or benchmark these candidate graphs as a valid model. This result does
not establish that RTMO cannot work with another export route. No runtime
fallback, public API change, or physical performance acceptance is authorized
by this report.

## Final device evidence

Corrected diagnostic runner, pinned ncnn20260526, Snapdragon888/Adreno660:

| Graph | Vulkan layers | CPU-only layers | Execution | Numerical result |
|---|---:|---:|---|---|
| raw.repair | 269 | 0 | Exit0; all8 outputs | All180,830 output values nonfinite |
| dcc8.shape | 135 | 0 | Exit0; [8,17,3] output | Effective slot0 XY max error382.3392px, mean162.3832px |

Raw nonfinite counts: cls16=676, bbox16=2704, vis16=11492, pose16=129792,
cls32=169, bbox32=676, vis32=2873, pose32=32448. Every expected raw element is
present, but none is finite. DCC produces408 finite values; valid-slot confidence
max error is0.00046575. Shape and exit-code success therefore do not prove parity.
No tolerance relaxation can accept nonfinite model outputs.

Evidence root: `out/android-rtmo/model-gate/`. Corrected logs are
`android-raw-allocator-fixed-executable.log` and `android-dcc8-shape.log`.
Pulled files are in `android-raw-corrected/one/` and
`android-dcc-shape-corrected/one/`. `android-numerical-comparison.json` records
flat element counts, finite-safe metrics and each actual/reference tensor SHA-256.
The comparator does not verify full tensor shapes; shape information above comes
from the runner logs.
`gate-report.json` records source/tool/artifact hashes and versions.

## Source, contract, and validated transformations

All learned weights came from the existing official model-pack ONNX, SHA-256
`20aad6e2e42359cac1c5b4a0b2da00e29bfe91a72a782fdcf287d273a04c1b24`.
Its upstream archive is recorded in `modelpacks/rtmo-t-416/SOURCES.md`.
No checkpoint, replacement model or dependency was downloaded. Canonical model
pack and runtime pipeline were left unchanged.

Input: batch1 NCHW, BGR0–255, mean0/norm1,416x416 centered affine letterbox,
bilinear interpolation, border114. Offline golden uses OpenCV warpAffine.
Raw output maps use strides16/32 with sizes26x26/13x13:1 class logit,4 bbox
values,17 visibility logits and192 learned pose features per anchor,845 anchors.
The second learned DCC graph has fixed8 inputs pose[1,8,192], boxes[1,8,5],
grids[1,8,2], visibility[1,8,17], output keypoints[1,8,17,3]. Learned features
are not coordinates. COCO17 is measured; Hand/Handtip/Thumb remain unavailable.

The initial legacy converter reported unsupported Focus slice steps and negative
Split axes. The one initial repair replaced Focus with an exact one-hot2x2
stride2 Conv in the original channel order and normalized Split axes. Four
contract tests pass, including pixel/channel equality and source-hash rejection.
Offline ORT golden preserves official NMS selections on real one-person,
two-person, and empty fixtures: accepted counts1,2,0 at unchanged0.35. Maximum
raw ONNX delta is0.00003242493; DCC delta is0. These are ONNX results only.

The first GPU runner lacked Vulkan allocators and crashed before upload. That
crash is **invalid evidence of a model failure**. The runner correction gives
blob/workspace/staging allocators lifetimes longer than Net/Extractor/VkMat,
explicit pack1 FP16 input conversion, and completion waits before temporary
GPU storage is released. Host build passed. Root reran the corrected runner.
Storage16/compute32 and disabled subgroup operations are explicit diagnostic
settings; this does not validate FP16 arithmetic or the future pack4 producer.

After corrected raw execution reached all outputs, root explicitly authorized
one additional narrow, maximum10-minute shape-only specialization: seven DCC
Unsqueeze nodes became static Reshape. All38 original initializers and89 other
nodes remain byte-identical; seven INT64 shape constants were added. Offline
outputs remain exactly equal on all three fixtures. `dcc8-shape-identity-proof.json`
and `dcc8-shape-golden.json` bind those claims. Both corrected GPU graphs execute,
but the final numerical failures above stop this route. No further converter or
model repair was attempted. CPU diagnostic candidates also exited0xC0000005;
no stack trace or exact inference fault root cause is claimed.

## Reproduction

From the worktree using the existing Python environment:

```powershell
.venv-reference/Scripts/python.exe tools/models/ncnn/rtmo_contract.py
.venv-reference/Scripts/python.exe tools/models/ncnn/rtmo_contract.py --repair
.venv-reference/Scripts/python.exe tools/models/ncnn/rtmo_contract.py --static-dcc-shapes
out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe out/android-rtmo/model-gate/raw.repair.onnx out/android-rtmo/model-gate/raw.repair.param out/android-rtmo/model-gate/raw.repair.bin
out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe out/android-rtmo/model-gate/dcc8.shape.onnx out/android-rtmo/model-gate/dcc8.shape.param out/android-rtmo/model-gate/dcc8.shape.bin
.venv-reference/Scripts/python.exe tools/models/ncnn/rtmo_golden.py
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_rtmo_ncnn_contract.py -v
```

Build with the tracked `tools/models/ncnn/rtmo_runner/CMakeLists.txt`:

```powershell
cmake -S tools/models/ncnn/rtmo_runner -B out/android-rtmo/runner-build-tracked -G "Ninja Multi-Config" -Dncnn_DIR="$PWD/out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/lib/cmake/ncnn"
cmake --build out/android-rtmo/runner-build-tracked --config Release
```

Use the installed MSVC developer environment for host builds. For Android,
configure another build directory with the existing NDK toolchain,
`-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26`, and the pinned
`out/ncnn-20260526/android-arm64-api26/install/lib/cmake/ncnn`. Root owns device
installation/invocation. After staging graphs, executable and `one/` fixtures:

```text
rtmo_golden_runner raw raw.repair.param raw.repair.bin one gpu
rtmo_golden_runner dcc8 dcc8.shape.param dcc8.shape.bin one gpu
```

After pulling outputs into the paths above:

```powershell
.venv-reference/Scripts/python.exe tools/models/ncnn/rtmo_compare.py
```

Expected comparison exit is1, status `FAIL_NONFINITE_OR_ELEMENT_COUNT`. The script emits
strict JSON with null metrics for invalid tensors; it never accepts an exit0
runner as a golden pass. Tracked-recipe host build passed independently in
`build-runner-tracked.log`.

## Boundary for any later work

This is two GPU graphs, not one forward. A naive bounded-output bridge downloads
723,320 bytes of raw model outputs and uploads6,912 bytes for fixed8 DCC. It does
not download the image/preprocessed input, but it does roundtrip learned output
features. The current generic backend lacks this second-graph GPU tensor handoff;
a reviewed internal capability design would be required before integration.
Dense845 DCC multiplies decoder capacity105.625 times versus8 and is not an
assumed performance solution. Model-specific decoding must stay in the pipeline.

No runtime/Unity/API/profile/model-pack files were changed, and no commit was
made. The route is closed as failed numerical eligibility, with no25/30FPS claim.
