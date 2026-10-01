# Explicit FP32 SGEMM SDK integration

The local evaluation ModelPack selects `raw_tensor_fp32_sgemm_v1` with packing
enabled, subgroup and all FP16 modes disabled, Winograd disabled and SGEMM
enabled. Existing five-option FP32 and ordinary two-option contracts keep their
previous defaults. This changes no public Unity API, AHB synchronization,
ownership transfer, frame admission, decoder, Tracker or Region behavior.

The profile remains `android-ncnn-vulkan` with fallback disabled. Only the
reviewed 640x384 local ModelPack can select this new execution contract. Its
builder recomputes all eleven numerical archives before producing runtime data.
Unity staging binds the native binary, four selected files and runner provenance;
both pre-copy and post-copy validation receive the explicit kernel selection.

Fresh verification commands:

```powershell
py -3 -m unittest tests.reference.test_yolo_sgemm_runtime tests.reference.test_yolo_rectangle_stage tests.reference.test_yolo_sgemm_gate tests.reference.test_yolo_packed16_gate tests.reference.test_yolo_pose_gate tests.reference.test_yolo_sgemm_stage -q
ctest --test-dir build/windows-test -C Release --output-on-failure -R 'Ncnn|Yolo|GpuTrack|TopDown|UnityVulkanBridge|AhbSlotRing|GpuAbi'
py -3 tools/test/verify_android_native.py --library out/android-yolo/sgemm-integration-frozen/libhumanvision.so
py -3 tools/test/stage_android_yolo_eval.py --runtime out/android-yolo/runtime-rectangle640x384-sgemm-verified --size 640 --convolution-kernel sgemm --verify-only
```

Results: 46 Python tests and 154 native tests pass. The Android artifact passes
ARM64/API26 validation with 1,813 resolved strong imports. Trace, parity and GPU
gate instrumentation are disabled. Reviewer findings for omitted final kernel
selection and inconsistent pack/index execution contracts were reproduced and
fixed before staging the open project.

Frozen native SHA256:
`12cf4e1a5146b7368e86d513609fa9a3724211427b4eaa5c91351051f0e04928`.
Runtime index SHA256:
`dd397a0e2cf8e25e28dd65b46cb7b14ed779eb181ad82de10bfc81065a5bc346`.

This is integration evidence only. Actual Unity GPU-AHB device performance is
pending; neither the >=25 FPS acceptance gate nor the 30 FPS target is passed.
The source video is 25 FPS and cannot certify 30 fresh observations per second.
Existing unfinished R4 changes remain preserved, and no main merge or Release
is authorized. Offline accuracy limitations and exact source-byte provenance are
described in the [eligibility report](2026-10-01-yolo-sgemm-eligibility.md).
