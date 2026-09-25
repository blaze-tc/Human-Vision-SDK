# RTMPose-t Body26 ncnn conversion gate — blocked (2026-09-25)

## Bounded shape-equivalent Vulkan diagnostic (2026-09-25)

The 12 unsupported shape-only operators in the official optimized graph were
checked against pinned ncnn `ExpandDims`, `Squeeze` and Vulkan `Reshape` source.
For this fixed 192×256 Body26 graph, all affected input/output shapes were
measured with pinned ncnn CPU; the 2D outputs have `h=26`, and the 3D outputs
have `c=1` or `c=26`, giving pack1 at every shape boundary. The original CPU
operators call `Mat.reshape`, and the Vulkan Reshape uses the same fixed
`(w,h[,c])` shape and flat element order. A hash-pinned finalizer, preserved at
`out/c3-local-runtime/blocked-official-task2/finalize_rtmpose_vulkan_shapes.py`,
rejects any changed source graph or layer layout. Its preserved test failed
before implementation and passed afterward. Original and rewritten CPU probes
produced identical 48-line shape/value-hash logs (SHA-256
`473f11db8cc7124f67c2240cba137ce594f6984d0264129ce238a6b7bdadcd8d`).
The generated Vulkan-only param SHA-256 is
`7f85743fbc5d76c5e1823f686302171e363e4af7618ac1d4fdf11b644a674b26`;
the official model bin stayed
`e2271fe80ae7b8f424d011253617077f704ed7e58429ddb10277dddb2ff8b613`.

The real OnePlus 9 Pro / Snapdragon 888 / Adreno 660 run passed a runtime
`net.layers()` audit: **169 layers, 0 without Vulkan support**, requested
Vulkan/FP16 packed/FP16 storage/FP16 arithmetic `1/1/1/1`, input FP16 pack4.
The first full-body case nevertheless fails. Fixed input crop SHA-256 is
`b4fce3c8d5583546062aa2a7eecb5aec103460e1c15feac21c3ebb7ce565891e`.
The official graph on pinned ncnn **CPU** closely matches the PyTorch Body26
reference: SimCC X/Y P95 absolute error `0.001402/0.001529`, x/y argmax
agreement `25/26` and `24/26`. Vulkan output has X/Y P95 absolute error
`17.4789/19.9902`, with **0/26** argmax agreement on both axes. Raw Vulkan
X/Y SHA-256 are
`91526fb765c3c49eedc85e420dc0ef39e6cbfc16ff767de2b4bb707e2fdd7f64`
and `b5c87e907a1656b88358abb5a8e977fa4711a3ff02379e87364f80765f94c8ac`;
CPU X/Y are
`e09c88eb8e7df9bc6dac42d836e80f494445481fec67f9ebc652ed8dce218627`
and `ec72a48e6d3e238029ec2ba861637c3deca9ca51a8f26b4b19ca5987fb7155bc`.
The final runtime-audit log SHA-256 is
`1fa234088da6d931c9b197b681e9ffbf983b8a95540df25ab0bcf5d8a7bb3f45`.

The earliest checked divergent layer is the **first convolution**, before any
of the 12 shape substitutions: `/backbone/stem/stem.0/conv/Conv_output_0`.
Against the same input crop, Vulkan versus ncnn CPU first-conv P95 absolute
error is `6.9510`, mean absolute error `2.8411`, and correlation `0.4625`;
CPU/Vulkan tensor SHA-256 are
`81b1897fb1da484490f5fc10b08d68360992086df123da82c35d9e717ae4ee7f`
and `b9f4fd55289d26c3a023497a23e245ef6046664295629d6732a8db1e4801ba31`.
This localizes the failure to the Vulkan/FP16 input or first convolution path,
but does not yet prove which one. The four-case golden stops at its first
failed case; no ModelPack is eligible. No additional graph surgery was tried.

Reproduce the finalizer, focused test and device first-case run from the
worktree root after restoring the three diagnostic sources from ignored
`out/c3-local-runtime/blocked-official-task2/` to their original paths.
The `shape-proof` source and all device files remain ignored:

```powershell
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_rtmpose_shape_finalizer.py -v
.venv-reference/Scripts/python.exe -c "from pathlib import Path; from tools.models.ncnn.finalize_rtmpose_vulkan_shapes import finalize; print(finalize(Path('out/c3-local-runtime/official-preset/model.param'), Path('out/c3-local-runtime/official-preset/vulkan.param')))"
& 'out/c3-local-runtime/shape-proof/build.cmd'
& 'out/c3-local-runtime/shape-proof-build/Release/shape_probe.exe' 'out/c3-local-runtime/official-preset/model.param' 'out/c3-local-runtime/official-preset/model.bin' 'out/c3-local-runtime/pose-golden-quarter/full-body/input.fp32' 'out/c3-local-runtime/shape-proof/names.txt'
& 'out/c3-local-runtime/shape-proof-build/Release/shape_probe.exe' 'out/c3-local-runtime/official-preset/vulkan.param' 'out/c3-local-runtime/official-preset/model.bin' 'out/c3-local-runtime/pose-golden-quarter/full-body/input.fp32' 'out/c3-local-runtime/shape-proof/names.txt'
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build out/c2-ncnn-runner-android --target c3_pose_golden --config Release
$adb = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe'
& $adb push out/c2-ncnn-runner-android/c3_pose_golden /data/local/tmp/hv-c3-shape/runner
& $adb shell chmod 755 /data/local/tmp/hv-c3-shape/runner
& $adb push out/c3-local-runtime/official-preset/vulkan.param /data/local/tmp/hv-c3-shape/model.param
& $adb push out/c3-local-runtime/official-preset/model.bin /data/local/tmp/hv-c3-shape/model.bin
& $adb push out/c3-local-runtime/pose-golden-quarter/full-body/input.fp32 /data/local/tmp/hv-c3-shape/input.fp32
& $adb shell /data/local/tmp/hv-c3-shape/runner /data/local/tmp/hv-c3-shape/model.param /data/local/tmp/hv-c3-shape/model.bin /data/local/tmp/hv-c3-shape/input.fp32 /data/local/tmp/hv-c3-shape/x.fp32 /data/local/tmp/hv-c3-shape/y.fp32
```

The instrumented runner used for this first device check SHA-256 is
`6d9cd2fa9bf28d995f354893c8b5ddc69c58abcddba2082b8d32b8e14bce68f4`.
It rejects any `net.layers()` entry without Vulkan support before inference.

## Official MMDeploy FP16 preset follow-up (2026-09-25)

One bounded follow-up used the **actual pinned ncnn backend preset**, rather
than the earlier ONNX Runtime backend copy of that preset. Pinned MMDeploy
`bc75c9d6c8940aa03d0e1e5b5962bd930478ba77` supplies
`configs/mmpose/pose-detection_simcc_ncnn-fp16_static-256x192.py` (SHA-256
`bd9dae769f415d736b8f6ee875ba4d2c8e5bcafbf63243a656f664165187bcf2`)
and its ncnn RTMCC rewrite (SHA-256
`a61a05d96d46342cda0c1b625a7f9f530e7d419c655ec4818f58bb8961f6cdd6`).
The preset resolves to `backend_config={type: ncnn, precision: FP16,
use_vulkan: False}` with static `[192,256]` input and `simcc_x/y` outputs.
Its backend rewrite calls `torch.norm` at `ScaleNorm`, intending to avoid the
previous FP16 reduction overflow, but this export still has two
`Pow`/`ReduceSum` normalization pairs. The preset's `use_vulkan: False` controls the
MMDeploy wrapper default; it does not authorize a CPU runtime path here.

The ignored reproduction script `out/c3-local-runtime/official-preset-export.py`
loads the pinned vendor checkout, changes only the input name to the required
`in0`, and calls `mmdeploy.apis.torch2onnx` with the same official Body26
checkpoint (`6020f8a6746639c0144eb979df0be0baa707af428d0e20a2db8991cf7452e5d6`).
Run from the worktree root with
`$env:PYTHONPATH=(Get-Location).Path; .venv-reference/Scripts/python.exe
out/c3-local-runtime/official-preset-export.py`. It exited 0 and wrote
`out/c3-local-runtime/official-preset/model.onnx` (SHA-256
`afb78fb13754e0cb4ada4d757c73674d84316f96a39bcfc10eced545ade6c59e`).
The exported graph has named `simcc_x/y` outputs and four
`mmdeploy::AdaptiveAvgPool2d` custom operators, plus 7 Unsqueeze and 5 Squeeze
ONNX operators. These operators must be assessed after conversion; an ONNX
export alone is not Vulkan layer evidence.

The pinned checkout contains the complete modified `mmdeploy_onnx2ncnn`
source in `csrc/mmdeploy/backend_ops/ncnn/onnx2ncnn/`. It was built without
source edits in ignored `out/c3-local-runtime/official-converter-build-v2/`
using the ignored wrapper CMake file and `build-official-converter.cmd`.
The build used MSVC 19.51 `cl.exe` SHA-256
`e6d57100c82ae0310c18b16abfe52bc0df8fbb272ca6c5fb287d485807cfce91`,
Protobuf 3.21.12 `protoc.exe` SHA-256
`02f493efb54922f8c343da87f78d70483353757880f5b7b319e61ba2c3d05735`,
and the pinned source files `onnx2ncnn.cpp`, `fuse_pass.cpp`,
`shape_inference.cpp` SHA-256
`a30f5f089e3e49689a80bfba5c2fd97496730fbb93769a46749869b234dec5bc`,
`7e5f2b87e3848b07cf357ca30541a6a4d73529bc37f6f1a08da8a9fca650cc2a`,
`48d02b4d70cdcdc2929db67b762e6834d8406be22921b04b636de8a0ec8bd02a`.
The resulting official converter SHA-256 is
`fc356c134ac02493ef2d4160dcbff09fdd9a8ed2af35067206f51a9f6897f408`.

The official converter and pinned Tencent optimizer both exited 0:

```powershell
& 'out/c3-local-runtime/build-official-converter.cmd'
& 'out/c3-local-runtime/official-converter-build-v2/onnx2ncnn/Release/mmdeploy_onnx2ncnn.exe' 'out/c3-local-runtime/official-preset/model.onnx' 'out/c3-local-runtime/official-preset/mmdeploy.param' 'out/c3-local-runtime/official-preset/mmdeploy.bin'
& 'out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe' 'out/c3-local-runtime/official-preset/mmdeploy.param' 'out/c3-local-runtime/official-preset/mmdeploy.bin' 'out/c3-local-runtime/official-preset/model.param' 'out/c3-local-runtime/official-preset/model.bin' 65536
```

Final `model.param` and `model.bin` SHA-256 are
`63fdeee444dcdb2371ac72860c7050a06474b32e7d318c30bd68a1b453d426db`
and `e2271fe80ae7b8f424d011253617077f704ed7e58429ddb10277dddb2ff8b613`.
The optimized graph has 169 layers. An exhaustive type-to-pinned-Vulkan-source
audit found 12 unsupported layers: 7 `ExpandDims` and 5 `Squeeze`; all other
non-Input layer types have Vulkan implementations. The layer list is retained
at `out/c3-local-runtime/official-preset/vulkan-unsupported.log` (SHA-256
`fa329598722ff3b6861600b80f7174f0058231dcb8bb4b88737720ac9403cdf8`).
The pinned `net.cpp` host-download path applies when `support_vulkan` is false.
Thus the converter's unmodified graph fails the no-CPU-fallback requirement.
The later bounded, shape-proven `Reshape` finalizer above passed the runtime
Vulkan-layer gate, but its first GPU golden case failed. No four-case golden
or ModelPack promotion followed.

For comparison only, the general Tencent `onnx2ncnn.exe` (SHA-256
`b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb`)
exited `-1073741819` (`0xC0000005`, access violation) on the official ONNX
and left zero-byte param/bin. Its exact command was:

```powershell
& 'out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe' 'out/c3-local-runtime/official-preset/model.onnx' 'out/c3-local-runtime/official-preset/raw.param' 'out/c3-local-runtime/official-preset/raw.bin' 2>&1 | Tee-Object -FilePath 'out/c3-local-runtime/official-preset/convert.log'
```

That comparison log SHA-256 is
`12c3c739553a6843cbf5fb9da7c7bd3871d8f062838822394219f7fa52aaba34`.

Revision 3 Task 2 has **not** passed eligibility. The official RTMPose-t
Body26 checkpoint exported to fixed 192×256 ONNX, but the Snapdragon 888
device did not pass the required four-case pose golden. The first case failed
for every bounded candidate. No schema-2 runtime ModelPack was promoted, no
Task 2 completion commit was made, and Task 3 remains closed. This document
records failure evidence only; it is not approval to distribute trained
weights. The only local draft is ignored at
`out/c3-local-runtime/invalid-draft-pack/`.

## Pinned inputs and retained diagnostics

| Input | SHA-256 / revision |
| --- | --- |
| Official [RTMPose-t Body26 checkpoint](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/rtmpose-t_simcc-body7_pt-body7-halpe26_700e-256x192-6020f8a6_20230605.pth) | `6020f8a6746639c0144eb979df0be0baa707af428d0e20a2db8991cf7452e5d6` |
| Official person image used for the pose crop | `7a090e3befceef2fe0db7e8b8a2a2ec03782e8e133a4738c027e3f6f23afac31` |
| Pinned `onnx2ncnn.exe` | `b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb` |
| Pinned `ncnnoptimize.exe` | `40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7` |
| [MMPose](https://github.com/open-mmlab/mmpose) | `5408bc76f5b848cf925a0d1857899011d8c5b497` |
| [MMDetection](https://github.com/open-mmlab/mmdetection) | `fe3f809a0a514189baf889aa358c498d51ee36cd` |
| [MMDeploy](https://github.com/open-mmlab/mmdeploy) | `bc75c9d6c8940aa03d0e1e5b5962bd930478ba77` |

The earlier three candidates used the pinned Tencent ncnn 20260526 converter;
the later official preset used the pinned MMDeploy-modified converter described
above. MMDeploy's static
pose deployment pattern and ncnn's [Vulkan precision notes](https://github.com/Tencent/ncnn/wiki/vulkan-notes#control-storage-and-arithmetic-precision)
guided the attempts. The checkpoint's model/dataset redistribution rights
remain unverified. All model bytes, raw SimCC tensors, source snapshots and
device logs are ignored under `out/c3-local-runtime/`; the six uncommitted
Task 2 diagnostic scripts/tests were copied with identical SHA-256 into
`out/c3-local-runtime/failed-task2-source/`. The tracked-code diff is retained
as `out/c3-local-runtime/failed-task2-source.patch`. Restoring those files in
an isolated checkout is required to rerun the commands below; they are not
part of this documentation-only commit.

## Reproduction and observed gates

From the Android ncnn worktree, after restoring the retained Task 2 diagnostic
sources, the original 1/16 candidate was generated with:

```powershell
.venv-reference/Scripts/python.exe -m tools.models.ncnn.export_rtmpose --checkpoint out/c1-source-cache/rtmpose-t_body26.pth --vendor-root out/c2-vendor --reference-image out/c2-detector/golden/official/image.png --output out/c3-local-runtime/rtmpose-t-body26.onnx --expected-checkpoint-sha256 6020f8a6746639c0144eb979df0be0baa707af428d0e20a2db8991cf7452e5d6 --expected-image-sha256 7a090e3befceef2fe0db7e8b8a2a2ec03782e8e133a4738c027e3f6f23afac31
pwsh -NoProfile -File tools/models/ncnn/convert_to_ncnn.ps1 -Role body -Checkpoint out/c1-source-cache/rtmpose-t_body26.pth -Onnx out/c3-local-runtime/rtmpose-t-body26.onnx -ExportManifest out/c3-local-runtime/rtmpose-t-body26.export.json -Fixture out/c2-detector/golden/official/image.png -FixtureSha256 7a090e3befceef2fe0db7e8b8a2a2ec03782e8e133a4738c027e3f6f23afac31 -Onnx2Ncnn out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe -Onnx2NcnnSha256 b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb -NcnnOptimize out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe -NcnnOptimizeSha256 40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7 -NcnnToolVersion 'ncnn 20260526' -OutputDirectory out/c3-local-runtime/pose-conversion-final
.venv-reference/Scripts/python.exe -m tools.models.ncnn.finalize_rtmpose_eval --folder out/c3-local-runtime/pose-conversion-final --export-manifest out/c3-local-runtime/rtmpose-t-body26.export.json --checkpoint out/c1-source-cache/rtmpose-t_body26.pth --onnx out/c3-local-runtime/rtmpose-t-body26.onnx --fixture out/c2-detector/golden/official/image.png --onnx2ncnn out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe --ncnnoptimize out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe
.venv-reference/Scripts/python.exe -m tools.models.ncnn.run_pose_golden --checkpoint out/c1-source-cache/rtmpose-t_body26.pth --vendor-root out/c2-vendor --image out/c2-detector/golden/official/image.png --onnx out/c3-local-runtime/rtmpose-t-body26.onnx --param out/c3-local-runtime/pose-conversion-final/model.param --weights out/c3-local-runtime/pose-conversion-final/model.bin --runner out/c2-ncnn-runner-android/c3_pose_golden --output out/c3-local-runtime/pose-golden --adb D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe
```

The converter generated real raw and optimized files, then its immediate
audit failed because the pinned converter omitted static `Input` dimensions.
The hash-pinned finalizer changed only that param line; the graph audit then
passed `[1,26,384]` `simcc_x` and `[1,26,512]` `simcc_y`. This checks format
and provenance, not numerical parity or per-layer Vulkan support.

| Bounded attempt on OnePlus 9 Pro / Snapdragon 888 / Adreno 660 | Exact failure |
| --- | --- |
| Unscaled static ONNX → ncnn FP16 | Both SimCC outputs entirely NaN; first nonfinite layer `/mlp/mlp.0/ReduceSum`, after finite squared inputs up to 10,736. Original first-nonfinite log SHA-256 `f694804b644b1c8507e23f8290d1d25341d9043ea1538dcc3df43fbd621ab0e1`. |
| Algebraic L2 scale 1/16 at both normalizations, compensated FP32 scalar | Finite SimCC X -311 to 767.5 versus ONNX -0.741 to 0.972; P95 absolute error about 97.64. First large finite mismatch `/gau/ln/ReduceSum`: ONNX 0.002399–0.004294, device 0.000094–0.000756. ONNX/param/bin SHA-256: `59b3ef917988c88dd44fe6595c62dee3c64727c55421beb27e0e4fae233a977b` / `3cc6d17ada3b7b715371898e80ac4f19d8820d8b661c40a8f7c8b580029d1fcf` / `df2733b9e3e1b85fe31a8d42857f112c17ee9ce3f25bb1207829e4424624a8ba`. |
| Algebraic L2 scale 1/4, compensated scalar; then FP16 packed/storage with FP32 arithmetic | Scale-only Vulkan first case failed valid-joint mask, SimCC X P95 absolute error 53.994 and 1/26 x-argmax matches. With FP32 arithmetic and explicit FP16 pack4 input, formal golden still failed the first case: X P95 25.2915, Y P95 15.1415, x/y argmax 1/26 and 0/26. Requested options logged as Vulkan/FP16 packed/FP16 storage/FP16 arithmetic `1/1/1/0`; no layer mask was used. Quarter ONNX/param/bin SHA-256: `93920e8efe4f7144bb5e4131e0b75b5c92fb2c35056a11b3c46b0aeacddab413` / `cf4afa0a21b4bd0f18d322d29a28efea7b82c29efadc38b53d2e5fae67b7ffb8` / `df2733b9e3e1b85fe31a8d42857f112c17ee9ce3f25bb1207829e4424624a8ba`. |

The last attempt's exact command appended
`--runner-mode diagnostic-vulkan-fp32-arith` to the `run_pose_golden` command
above, changed `--param`/`--weights` to
`out/c3-local-runtime/pose-conversion-quarter/model.{param,bin}`, and changed
`--output` to `out/c3-local-runtime/pose-golden-fp32arith`. The quarter ONNX
was produced by temporarily changing the retained export's two L2 scales and
compensating multipliers from 1/16 and 16 to 1/4 and 4. The formal device log
SHA-256 is `fe297149f34c158d07ba738dfb97691059ecbfc7ac7956221f903eadb938311c`;
output X/Y SHA-256 are `9988e93d808f3afe79bbee3cd41004fe52f9bc6874fcece3d53316d2f1543b1e`
and `4e28bebba86042400ff2d064651ab6905f3d9df4aea73799630efc4d5ac674d6`.
The runner reported 94.745–96.471 ms for two output extractions in these
single runs. This is diagnostic timing, not a warmed FPS benchmark.

An independent **no-CPU-fallback gate** also fails. The optimized graph
contains one `ExpandDims` and two `Squeeze` layers (param lines 138, 142–143),
but the pinned Tencent ncnn source has no Vulkan implementations for those
types. Pinned `src/net.cpp` downloads to host when `support_vulkan` is false.
The 155-layer static allowlist passed, but cannot establish Vulkan-only
execution. Thus even a numerically matching output from this graph would not
meet the approved contract. The four-case index was never produced for any
candidate. The narrower next work is to validate both a Vulkan-only graph
conversion and correct normalization numerics before composing a ModelPack.

The original RED tests and complete device command/output trail remain in the
ignored Task 2 report at
`.superpowers/sdd/2026-09-25-android-ncnn-topdown-cadence-revision-3/task-2-report.md`.

## Revision 3 official graph and input-route diagnosis (2026-09-25)

The follow-up used the complete official MMDeploy static FP16 preset and its
own pinned converter. The exported ONNX, optimized param, and bin SHA-256 are
`afb78fb13754e0cb4ada4d757c73674d84316f96a39bcfc10eced545ade6c59e`,
`63fdeee444dcdb2371ac72860c7050a06474b32e7d318c30bd68a1b453d426db`,
and `e2271fe80ae7b8f424d011253617077f704ed7e58429ddb10277dddb2ff8b613`.
Its 7 `ExpandDims` and 5 `Squeeze` operators were replaced only after
per-boundary ncnn shape and value proof by hash-pinned finalizer; the resulting
param SHA-256 is
`7f85743fbc5d76c5e1823f686302171e363e4af7618ac1d4fdf11b644a674b26`.
The real Snapdragon 888 runtime audit passed 169/169 Vulkan-supported layers.

For the same fixed full-body crop (FP32 input SHA-256
`b4fce3c8d5583546062aa2a7eec103460e1c15feac21c3ebb7ce565891e`),
ncnn CPU final SimCC closely matched PyTorch (X/Y P95 absolute errors
0.001402/0.001529). Two device input routes isolated the remaining failure:

| Device input route | First Conv versus ncnn CPU | Final SimCC versus PyTorch |
| --- | --- | --- |
| Original FP32 pack1 `ncnn::Mat` fed to Vulkan `Extractor::input` | P95 absolute error 0.00605035, correlation 0.99999958 | Both X and Y entirely NaN. This route does not satisfy the required explicit GPU FP16 pack4 input. |
| FP32 RGBA input uploaded to GPU, then `VulkanDevice::convert_packing(..., 4, 2, ...)` to FP16 pack4 `VkMat` | P95 absolute error 6.95104, correlation 0.462504 | X/Y P95 absolute errors 17.478883/19.990153; argmax 0/26 on both axes. Byte-identical to the prior manually packed FP16 route. |

GPU readback of the second route after FP32 pack1 unpack showed ncnn dims=3,
w=192, h=256, c=4, channel order RGB plus a zero fourth lane, and expected FP16
rounding. Its first border values were R -2.117188, G -2.035156, B -1.803711,
fourth channel 0. The dynamic center-row samples and the full device log are
retained under `out/c3-local-runtime/shape-proof/`. This rules out the manual
CPU staging path as the explanation for the pack4 result; it does not establish
the exact cause of the pack4 Conv mismatch. The FP32 pack1 route's final NaNs
show that first-Conv agreement alone is insufficient for eligibility.

SHA-256: final runner source
`6b379ab73de6a8dc1b94979031c0993ba50e102c40fb0b8464c49d8881078fd7`;
route comparison log
`a2e1790c1b079487087ee7af22b3e7f89c96ff74469f0050cd47e359af736180`;
GPU upload/readback log
`9448f85b743c8dcf149eb43d75bbb48207add65884d3395b93697ebe1cbbb651`.
The first case failed, so the four-case device golden was not run; no eligible
ModelPack or Task 2 completion commit was produced. The next architecture
decision must identify a production GPU FP16 pack4 input/first-Conv path with
CPU/PyTorch parity and finite final SimCC before the four-case gate can be
repeated. Further unproven graph surgery is outside this bounded attempt.
The finalizer, test, runner and exact draft documentation patch were copied
with verified matching hashes to ignored
`out/c3-local-runtime/blocked-official-task2/`; the runner comparison and
GPU upload/readback logs remain under ignored
`out/c3-local-runtime/shape-proof/`. They are failure diagnostics, not an
eligible runtime model or public API.
