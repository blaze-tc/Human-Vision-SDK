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
