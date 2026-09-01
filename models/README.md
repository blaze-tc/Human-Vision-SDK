# models/

Runtime model binaries belong under:

```text
models/detector/rtmdet_tiny_640.onnx
models/detector/model_info.json
models/pose/rtmpose_s_256x192.onnx
models/pose/model_info.json
```

If binary models are excluded from git, keep deterministic download/export scripts and SHA-256 checksums under `tools/reference/`.
