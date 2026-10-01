# FP32 GPU convolution without shared local memory: offline eligibility

The accepted640x384 FP32 path follows arms but produces about15.4 fresh
observations/s. Sampled timings locate about86% of elapsed cost in extraction,
including internal ncnn waits. The separate `gpu-fp32-no-local-memory` candidate
changes only official `use_shader_local_memory=false`. Model, input geometry,
FP32 precision, internal packing, disabled subgroup operations, Winograd=true,
SGEMM=true and CPU oracle remain unchanged. It is not the earlier SGEMM-only
or failed FP16 candidate.

Pinned ncnn20260526 `convolution_vulkan.cpp:1331-1339` selects an optimal
workgroup instead of fixed8x8 when local memory is disabled. The corresponding
`convolution_packed_gemm.comp` excludes its shared arrays and workgroup barriers,
using its alternate GPU load path. This trades shared-memory/barrier cost for
direct loads; source inspection does not establish a speed improvement.
No ncnn shader patch or synchronization change is included.

The separate API26 ARM64 diagnostic build and independent spec/quality review
pass. Its input/output boundary is FP32 pack1, with explicit official GPU
conversion, allocation/shape/dtype checks before download, and command drains
while tensors remain alive on failure. Internal packed rows are checked as
height*elempack; converted output is checked as scalar pack1. Guard tests do
not constitute device OOM injection. Initial11 expected RED errors and final
78 passing YOLO tests are archived. Independent reviewer reran78 tests in7.645s;
root reran11 focused new tests in0.559s. Maintenance architecture checks pass.

Binary/source/recipe SHA256:

```
4cb46cb5ad25bc7e4da97e318dd35a34e6ac30d939f0bf75091041ff86d58537
62bb3d5e4acdc15709a04f41bee21a2fe78c14e41f7ec835b30dfe71d1f00a02
6fd87d16b50461cdd53597c59e2b7e7d1bcd86e69091e7b4395b79c99f576e70
```

All five source files were UTF8 LF before compilation and freezing. Exact built
source/proof snapshots, configure/build/test logs and frozen runner are retained
under `out/android-yolo/no-local-memory-gate`. Historical runner bytes are
unchanged. Git checkout line ending policy must still be checked before reusing
an exact built-source hash; no clean-checkout artifact reproduction is claimed.

```powershell
py -3 tools/models/ncnn/yolo_device_gate.py --adb "D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe" --serial e7c07019 --runner out/android-yolo/no-local-memory-gate/runner-frozen --gpu-mode gpu-fp32-no-local-memory --fixtures seven-416 one-416 seven-320 one-320 seven-640 one-640 empty-416 seven-square320 one-square320 seven-square416 one-square416
py -3 -m unittest discover -s tests/reference -p 'test_yolo*.py' -q
py -3 tools/maintenance/check_architecture_boundaries.py
```

All eleven actual Snapdragon888 executions and unchanged numerical/person/
annotation gates PASS. Maximum raw absolute error is0.0001869202, below the
frozen0.2 budget. All22 CPU output hashes exactly match the original historical
oracle. Independent archive recomputation equals each archived comparison and
checks source/recipe/binary, effective options, input/output and log bindings.
Both CPU and GPU seven640 frame1500 outputs have7/7 real left wrists valid,
inside the1024x576 source and above their corresponding left shoulders.

The UTF8 LF evidence index `tools/models/ncnn/yolo_no_local_memory_gate_evidence.json`
has SHA256 `211a59e74cfe3d8d0d96b836f0bbe626d844e863e283acc955b2c3dfd7af5680`;
its source-bound archive identities and per-fixture limits are rechecked before
runtime construction. This proves offline eligibility against the same converted
ncnn graph CPU oracle, not original PyTorch/export ground truth. One-frame arm
semantics do not prove continuous tracking or other gestures.

SDK option binding, actual Unity GPU-AHB pipeline FPS/age/coverage and physical
acceptance remain pending. No speed claim, default promotion, >=25/30 FPS pass,
CPU input readback, fallback, main merge or Release is included.
