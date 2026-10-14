# Complete local Android CPU resources

This local addon extends the restored preview.7 runtime with the explicit Android
CPU YOLO body profile. It contains 52 indexed runtime resources, index and seal,
the pinned RKNN runtime, the CPU stage delta receipt, and a matching deployment
checker. Original GPU/NPU models, profiles and evidence remain byte-identical.
SDK and Input native libraries remain in their own separate packages.

Install the matching Input package first, compile it, then install the matching
SDK package and this complete local addon. Use only one installation form for
SDK/Input. Do not mix different native binaries with this resource index. If a
previous local setup script remains, use the CPU addon menu below for this stage.

Select **HumanVision > Enable Local Android CPU Resources**. The generated
checker verifies this exact runtime index, every indexed file, the current SDK,
Input, ONNX Runtime and RKNN ARM64 libraries, and the preserved Windows SDK.
It then enables the existing `android-dual-vulkan-npu` build mode. The API's
`AndroidCPU` selection uses only `android-cpu-nohands`, `pipeline.yolo.cpu`,
`yolov8n-pose-512x288-ort` and `backend.ort.cpu`, with no fallback. GPU/NPU
selections retain their original compositions. CPU accepts centered letterbox
for arbitrary source aspect ratios; this does not alter GPU/NPU qualification.

The body limit remains configurable from one through eight, hand inference is
disabled, and the target body/output settings remain 30/60. These configuration
values and package integrity checks do not establish achieved live FPS, person
count accuracy, thermal behavior or complete hand endpoint acceptance.

The graph and vendor library remain local evaluation resources. This addon has
`distribution_qualified=false`, `hardware_fps_acceptance=false` and
`public_upload_allowed=false`. No commercial model authorization or vendor
redistribution clearance is established by exporting or importing it. Inspect
`manifest.json`, `cpu-yolo-stage.json` and `SHA256SUMS.txt` in the local export
directory for the exact native/model/index hashes and allowed file delta.
