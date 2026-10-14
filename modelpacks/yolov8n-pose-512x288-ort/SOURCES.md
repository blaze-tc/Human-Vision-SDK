# Source and local evaluation boundary

The graph is the existing `out/rknn-validation-20261009/recovered-low.onnx`,
recovered from the pinned local NCNN YOLOv8n pose weights with
`tools/models/rknn/recover_pinned_onnx.py`. This change does not download, convert
or re-export weights. The local CPU stage copies these exact graph bytes to
`body.onnx`:

`6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3`

The same source graph is identified by the unchanged RKNN conversion evidence in
the restored baseline. Its NCNN parameter hash is
`908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905`;
the weight hash is
`6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9`.

Input is FP32 `[1,3,288,512]`, `in0`, RGB with mean zero and norm `1/255`,
centered letterbox and RGB padding 114. Raw FP32 `out0` and `out1` contain 3024
rows with 65 and 51 columns, respectively. Their maximum byte counts are 786240
and 616896. The `yolov8_pose_dfl17_v1` pipeline owns decoding and coordinate
restoration; common skeleton services receive semantic observations only.

The existing source identifies upstream Ultralytics licensing. No applicable
commercial authorization or model redistribution clearance is established by
this integration. All local receipts retain `local_evaluation_only=true`,
`distribution_qualified=false` and `hardware_fps_acceptance=false`. Weight bytes
are excluded from Git and public release tooling. RKNN vendor runtime terms are
a separate unchanged distribution boundary.
