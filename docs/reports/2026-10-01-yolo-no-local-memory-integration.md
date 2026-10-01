# Explicit FP32 no-local-memory GPU integration

The reviewed candidate now has a separate local evaluation contract,
`raw_tensor_fp32_no_local_memory_v1`. Eight required boolean options bind the
actual `ncnn::Option.use_shader_local_memory=false`, retaining Winograd=true,
SGEMM=true, FP32 and internal packing. The original two-, five- and seven-option
contracts retain their valid behavior and defaults. Geometry, decoder thresholds,
public API, Tracker, Regions and GPU synchronization are unchanged.

Host resolution rejects this contract outside local evaluation or with false
FP16 capability claims. Independent review found a combined-mutation gap:
`local_evaluation_only=false` plus both FP16 capabilities could resolve before
the pipeline rejected it. A regression with actual copied model assets first
failed, then passed after the scoped Host guard. Missing files cannot satisfy
this regression. No automatic fallback is introduced.

The runtime builder rechecks all eleven actual numerical archives, their
comparison equality and provenance,22 historical CPU hashes and CPU/GPU seven
raised left arms before writing. The new model pack is
`yolov8n-pose-rectangle640x384-fp32-no-local-memory-local`; the explicit profile
is `android-ncnn-vulkan`, with fallback=false. Its640x384 input,640x360 resized
image and twelve-pixel top padding remain the accepted geometry.

Staging accepts `--convolution-kernel no-local-memory` only with size640. It
pins the exact native binary, source and transformed Unity index, four selected
files, eligibility index and runner/source/recipe identities. Both current-project
copy checks retain the selected mode. Legacy staging identities stay unchanged.

Fresh verification:

```powershell
py -3 -m unittest discover -s tests/reference -p 'test_yolo*.py' -q
ctest --test-dir build/windows-test -C Release --output-on-failure -R 'Ncnn|Yolo|GpuTrack|TopDown|UnityVulkanBridge|AhbSlotRing|GpuAbi'
py -3 tools/test/verify_android_native.py --library out/android-yolo/no-local-memory-integration-frozen/libhumanvision.so
py -3 tools/maintenance/check_architecture_boundaries.py
py -3 tools/test/stage_android_yolo_eval.py --runtime out/android-yolo/runtime-rectangle640x384-no-local-memory-verified --size 640 --convolution-kernel no-local-memory --verify-only
```

Native tests161/161,
YOLO Python95/95, architecture/public-surface checks and API26 ARM64 audit with
1,813 resolved strong imports pass. Independent reviewer also reran20 focused
native tests and all95 Python tests. Root freshly reran161 native tests and the
exact frozen native audit. The normal Android build has TRACE, GPU_GATE and R4
diagnostics OFF; existing RTSP/dependency packaging remains unchanged.

Frozen native SHA256:
`8a1ab8d250c8d214e3fd5b976c4a55043a5de655d760e7688bb604d5470b8b98`.
Raw runtime index:
`9e7df19ee235e85061f3703d1dd4a02bc740fddc02c9c29d743c834fa9f78d65`.
Unity index:
`64e3ea59c79cfdae0430b54b981daeebe630f8751c35d23765c5b7ada6c11f3c`.
Freeze manifest:
`29a93527c5d6b587d10527dc7dd8ac3d4f179014bcd9e0a245ab0f1408a624b9`.

`out/android-yolo/no-local-memory-integration-frozen` preserves239 hash-bound
files, all ten owned sources, complete native source snapshots, cache and logs,
the full working-tree patch and binary. Existing unfinished R4 work is preserved
and represented in the source snapshot; a clean HEAD alone is not asserted to
reproduce this binary. Candidate source/evidence identities are independently
checked before staging.

SDK integration review passes. Actual current Unity import/build/installed APK
and continuous seven-person FPS/age/coverage are next; no hardware performance
or default-promotion claim is made by these software checks. Offline eligibility
uses the same converted ncnn CPU graph, not PyTorch. The25FPS video cannot prove
30 fresh source frames/s. No main merge or Release is authorized.

## Actual current Unity and device check

After the first full PowerShell stage found Windows CRLF serialization, the new
mode alone now writes the transformed Unity index with explicit LF. Its unchanged
reviewed index hash is preserved rather than accepting the differing bytes.
The zero-mock full-stage regression uses the actual frozen runtime/native/video
and SDK files: RED before the fix, GREEN after it. All96 YOLO tests and
architecture checks pass; independent spec/quality review confirms all239 original
freeze files unchanged. The separate stage-only freeze SHA256 is
`abba51e7b1a6c08c9a4e975913fbe069fd7aa690788dd583835abacff35d25f4`.
Commit `6d7d8eb` contains only this writer correction and regression.

The fresh `eval-rectangle640-20261001-m3-no-local-memory-v2` PowerShell stage
passes native audit and strict source/transformed runtime closure. The authorized
open project `E:/UnityProject/Human-Vision-SDK-Test` was idle, not compiling and
had a saved clean scene. Twelve replacements, project settings and both diagnostic
scenes were backed up before copying. Live refresh and post-build Console report
zero errors. Editor.log records the exact new APK output and
`HV_R4_EDITOR_SETTINGS_RESTORED`. The APK's four runtime files, explicit profile,
640x384 shape and native identity match the reviewed pins.

APK SHA256:
`a7d4bffd737e2c77d0cf91d2e4a869749ecd278bfd1195136a5a3bc52f7ef1a3`.
Installed base APK identity was checked on device `e7c07019`, Snapdragon888.
The actual continuous VideoPlayer -> GPU AHB -> ncnn -> Canonical Skeleton
run uses the unchanged hash-bound25FPS `video-1.mp4`, starting at37seconds,
capacity8. It performs no full-frame CPU readback.

PID24588, requested75-second capture, warmed observed window82.302000046s:

| Metric | Actual result |
| --- | ---: |
| Unique fresh complete observation records |1408 |
| Fresh observation FPS |17.107725 |
| Records with exactly seven bodies |1397 |
| Exactly-seven subset FPS |16.974071 |
| Partial / empty records |0 /0 |
| Eight-body records |11 |
| Observed end-to-end age P50 / P95 |101.149478 /131.918611ms |
| GPU worker errors |0 |
| Copy / import errors |0 /0 |
| Full-frame CPU readbacks |0 |

Evidence: `out/android-yolo/eval-rectangle640-20261001-m3-no-local-memory-v2/device-yolo640-no-local-memory-seven-75s/`.
The count-seven subset is not independently validated person identity or joint
accuracy on every frame. The60-second screenshot displays seven upright skeletons
on the seven people; this is one visual sample, not temporal physical acceptance.
The panel was hidden after the30-second screenshot. No long prediction or altered
acceptance limits were used.

The previously repeated accepted640 baseline measured15.417513 fresh and
15.289212 count-seven FPS, with age113.081697/135.164946ms. This candidate's
measured run is approximately11% higher, but duration, panel/render/load and
thermal conditions were not controlled as a causal benchmark. It is a modest
observed difference, not proof of a repeatable11% optimization.
The25FPS all-person gate remains FAIL and the25FPS source cannot establish30
fresh results/s. This is an explicit local candidate, not a promoted production
default or completed milestone. No main merge or Release has occurred.

Independent spec/quality and device-facts review PASS: all metrics were recomputed
from the raw PID-filtered log, APK runtime/native/index pins rechecked, and every
current ProjectSettings file compared with the preflight backup. The phone was
then restored to the accepted640 APK
`9cc3b87f8b824fe9819b85d1163c15b0e4d1241e59e223d9dca099905710caae`,
its installed hash verified and the app force-stopped for cooling. The open Unity
project remains the explicit no-local-memory evaluation stage; public/default
SDK behavior has not been promoted.
