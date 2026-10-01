# YOLO-M2 local GPU adapter — reviewed implementation

Scope: existing GPU host/AHB/backend consumes one YOLOv8n-pose network per
accepted source frame and returns up to configured capacity1..8 canonical
COCO17 observations. Common services retain tracking and region assignment;
hands remain invalid. Public C/Unity API is unchanged. This is a local-only
FP32 candidate, not a production-default change or FPS acceptance.

Independent GPT-6.1 Sol medium spec compliance and code quality review: PASS,
no remaining actionable findings. M1 prerequisite commit de08263. Existing
incomplete R4 changes were preserved and excluded by selective shared-file
staging; no main merge or Release.

## Verified checks

- Tests-first RED: missing decoder header C1083 before implementation.
- Release native targets built; filter
  `Yolo*:Ncnn*:TopDownGpu.*:TopDownTensorContract.*:RuntimeGpuComposition.*`
  67/67 PASS; `NcnnModelPack.*` 2/2 PASS. Reviewer independently repeated both.
- Real immutable square416 seven-person CPU/GPU output decoder golden passed
  boxes, scores, joint coordinates/confidences, source geometry/timestamps,
  canonical indices and capacity1..8. Wrong shapes/nonfinite values fail;
  valid empty output returns zero people. Plumbing doubles do not establish
  inference acceptance. Warmed decoder/pipeline allocation probes report zero.
- `tools/maintenance/check_architecture_boundaries.py`: public surface and
  architecture/documentation guards PASS.
- Separate API26 ARM64 Android Release target
  `build/android-yolo-m2/bin/Release/libhumanvision.so` built. SHA256
  `fefaad0ae7186febcba781880bb0cc38a37aad5254e347671505249ca6aeeeb5`.
  `tools/test/verify_android_native.py --library` on that target PASS: ELF64
  AArch64, API26, static ncnn/AHB/Vulkan symbols, 1813 strong dynamic imports
  resolved against packaged dependencies and API26 stubs.

Explicit schema2 `local_evaluation_only`/`raw_tensor_fp32_v1` enables FP32pack1
with internal packing, no FP16 packed/storage/arithmetic and no subgroup ops.
Production TopDown FP16 validation remains strict. No automatic fallback.
Square320/416 RGB/255, centered pad114, shorter resize extent truncation are
declared. No full-frame/input-tensor CPU readback or per-frame JSON added;
AHB slot caching, physical-device matching, sync-fd and completion ownership
remain unchanged. Only bounded model outputs reach CPU.

## Remaining gate

M1 compares the same converted graph on CPU/GPU, not original export accuracy.
M2 validates decoding and plumbing, not actual AHB input parity. M3 must build
the existing Unity Demo into the authorized open project, install on e7c07019,
inspect upright real overlay alignment and measure fresh complete7-person
observations/s, partial/empty coverage, P50/P95 age and drops/errors. Minimum25,
target30; the current25fps video cannot prove30 distinct input frames/s. No
performance or physical acceptance is claimed here. Third-party weights remain
ignored local evaluation artifacts and are not redistributed.
