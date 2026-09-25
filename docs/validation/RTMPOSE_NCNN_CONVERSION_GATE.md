# RTMPose-t Body26 ncnn conversion gate — blocked (2026-09-25)

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

The converter is the pinned Tencent ncnn 20260526 release. MMDeploy's static
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
