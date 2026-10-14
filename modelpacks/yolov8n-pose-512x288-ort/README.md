# yolov8n-pose-512x288-ort

Purpose: data-only local ModelPack for `pipeline.yolo.cpu`. It consumes the pinned
FP32 NCHW RGB graph and produces one complete frame of up to eight COCO-17 body
observations. The pipeline restores centered letterbox coordinates to the source
image, including source aspect ratios different from 512x288. Hand inference is
disabled in `android-cpu-nohands`.

Allowed dependencies: `pipeline.yolo.cpu` and the manifest-declared tensor
contract. Forbidden dependencies: Unity, Host, tracking and executable source.
Primary files: `manifest.json`, `component.json`, local `body.onnx`, `SOURCES.md`.

Focused tests: native `YoloCpuPipeline.*` and `YoloTensorPipeline.*`, and
`py -3.13 tools/package/test_main_cpu_yolo_stage.py`. Native tensor tests and
package checks establish contracts; they do not establish live accuracy, eight
person throughput, 30 fresh FPS, thermal behavior or real hand endpoint acceptance.

To replace this graph, create a new pack ID, record actual graph SHA-256 and
provenance, and run the selected decoder's numerical tests. The stage tool accepts
only the pinned current graph. It preserves the original GPU/NPU models,
profiles, evidence and vendor bytes. Unknown model, output shape, backend or
normalization failures should be investigated here before changing gameplay.

Weights remain untracked local evaluation assets. Distribution and hardware FPS
qualification remain false. See `SOURCES.md` before any redistribution decision.
