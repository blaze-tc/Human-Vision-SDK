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

## Integrated Snapdragon 888 measurement: throughput gate failed

The current open Unity project refreshed without errors, built the video APK,
and emitted the settings-restoration marker. Embedded native, four runtime files,
source video and installed APK identity were verified before capture.
APK SHA256: `f8bd4818a6859d7b478505f5635494a615fd704ad0c69c1aa54b19a211e2f74b`.
Evidence: `out/android-yolo/eval-rectangle640-20261001-m3-sgemm/device-yolo640-sgemm-seven-75s/`.

After excluding the first five seconds following active submission, the measured
window is 81.288 seconds. PID15217 reports 1,128 distinct source-frame/result
sequence pairs: **13.8766 fresh observations/s**. Of these, 1,121 have body count7:
13.7905/s; seven have count8, with no empty or partial-count observations.
Observed result age P50/P95 is116.726/147.159ms. GPU worker, copy and import errors
and full-frame CPU readbacks are zero in the recorded counters.

These are result metadata counts, not independently verified seven-person
identity or per-joint accuracy. The60-second screenshot shows seven skeletons.
The phone changed between portrait and landscape during capture; render rate and
orientation were not controlled against the earlier baseline. This is therefore
not a controlled causal comparison, but it clearly fails the >=25FPS gate and
does not demonstrate improvement over the earlier normal15.32FPS capture.

This candidate is not selected as the default optimization. The previously
accepted normal APK was restored by its verified SHA256. Preserve the explicit
SGEMM evaluation path and evidence for reproducibility. M3 remains active; next
is a bounded smaller rectangular-input eligibility check with unchanged accuracy
limits and all seven raised-arm evidence required before any SDK integration.

Neither the >=25 FPS acceptance gate nor the 30 FPS target is passed.
The source video is 25 FPS and cannot certify 30 fresh observations per second.
Existing unfinished R4 changes remain preserved, and no main merge or Release
is authorized. Offline accuracy limitations and exact source-byte provenance are
described in the [eligibility report](2026-10-01-yolo-sgemm-eligibility.md).
