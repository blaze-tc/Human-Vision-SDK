# pipeline.yolo.pose

Local evaluation GPU-only YOLOv8n-pose adapter. One full-frame network execution
per accepted source frame publishes up to configured capacity1..8 measured COCO17
joints through the canonical observation ABI. Common services own identity and
regions. Unavailable hand/handtip/thumb remain invalid. No held/predicted result.

The schema-2 pack declares `raw_tensor_fp32_v1`, square320 (default) or square416,
RGB /255, centered pad114, FP32 pack1 boundary, internal packing enabled, subgroup
and all FP16 options disabled. It accepts only explicitly local evaluation packs
and profiles. Generic backend performs inference and GPU preprocessing; the
pipeline validates exact `[anchors,65]` DFL16+person logits and `[anchors,51]`
COCO17 outputs, finite values, threshold0.25, NMS0.45 and source geometry.

The exact rectangular contracts are512x288,576x352,640x384 and960x576;
each requires a16:9 landscape source. The fixed960x576 contract resizes to
960x540 and pads114 by18 pixels top/bottom, producing11340 rows and exact
2948400/2313360-byte outputs. Nearby shapes, portrait sources and unqualified
SGEMM/no-local-memory combinations fail before backend creation. Historical
defaults and original model graph/weights remain unchanged.

960 eligibility is local offline numerical/semantic evidence only. Start at
`tools/models/ncnn/yolo_rectangle960_prepare.py`, then its separate device gate
and runner recipe; use canonical Python3.13. The frozen960 index binds real
seven-person frame1500, a documented portrait-derived114 canvas control and an
analytic empty control. Original shared runner hashes and historical staging
gates remain intact. These CPU tensor uploads do not qualify production GPU/AHB
input, temporal accuracy, motion or the30fresh complete observation FPS target.

Resize truncates the shorter source extent. The existing GPU letterbox shader
quantizes resized pixels to uint8 before normalization; M1 uses a shared OpenCV
offline input oracle. Integrated GPU preprocessing parity remains device work.

Decoder vectors reserve all anchors at creation. Sort is in-place, output and
sidecar storage are fixed ABI arrays, and frame processing performs no JSON parse.
Backend initialization and generation changes may allocate; backend slot/cache,
device UUID, external sync-fd and completion ownership are unchanged.

Run native `Yolo*` tests and architecture guards. Exact ignored immutable M1
square416 fixture is declared by tests/native/CMakeLists.txt. Tests use real CPU
and eligible GPU output bytes; plumbing doubles are not inference acceptance.
Generate a separate ignored runtime root using
`python tools/models/ncnn/yolo_stage_runtime.py --size 320` (or416 explicitly).
Weights are local only and must not be redistributed. Disable R4 parity shaders
for this graph; their old tensor/input contract cannot certify YOLO.

## Private tensor experiment (ABI1)

`yolo_tensor_pipeline.{h,cpp}` exports `HV_QueryYoloTensorPipelineV1` as
`pipeline.yolo.tensor`, with body_pose/multi_person and capacity1..8. It consumes
stride-aware RGB24/BGR24/RGBA32/BGRA32 CPU frames and emits canonical observations;
common services still own identity and regions, and unavailable hand points stay
invalid. This entry point does not advertise GPU input or modify the GPU entry.

Creation checks the private schema-1 512x288 non-quantized ModelPack/profile,
pinned model SHA, actual conversion/simulator receipt hashes and parsed three-case
offline qualification. It synchronously creates only `backend.rknn`; initialization
failure releases partial sessions and returns an error. Use a background candidate
initializer before switching Unity sessions. No production/device qualification
is inferred from the saved PC simulator evidence.

Each complete16:9 landscape input resizes to a fixed uint8 RGB NHWC
`in0[1,288,512,3]`; the RKNN model owns compiled /255 normalization. Fixed storage
and OpenCV INTER_LINEAR-compatible uint8 rounding support input-resolution changes
without frame allocations. The same decoder validates exact finite borrowed FP32
`out0[1,3024,65]` and `out1[1,3024,51]` before copying canonical output. Invalid
frames, short output buffers or backend/output errors publish zero body/hand counts.
`preprocess_ms`, `inference_ms` and `postprocess_ms` measure separate local stages;
the internal fixed diagnostics registry counts backend attempts/executions.

Allowed dependencies are the plugin ABI, config/hash utilities, tensor backend
host services and local decoder. Unity types and vendor runtime headers do not
belong in this pipeline. Run `humanvision_native_tests --gtest_filter=YoloTensorPipeline.*`
and architecture guards. Tests bind actual saved seven/one/empty PC simulator
outputs and input oracles; injectable plumbing is not physical NPU acceptance.
Missing/pin-mismatched assets fail before backend creation; malformed output
shapes, names, bytes or nonfinite values indicate backend/ModelPack mismatch.

The tensor preprocessor's private `yolo_rgb_preprocess.h` specializes exactly
512x288 identity, 1024x576 2:1 and 1280x720 5:2. ARM64 NEON uses bounded loads
and matches the old uint8 staged integer rounding byte-for-byte; other ratios
retain the cached general interpolator. It never changes normalized output,
canonical mapping, model bytes or backend precision. Validate all formats and
padded rows with `YoloRgbPreprocess.*` plus the actual ARM64 pixel/timing probe.
No host/OnePlus preprocess measurement constitutes RK3588 model throughput.
