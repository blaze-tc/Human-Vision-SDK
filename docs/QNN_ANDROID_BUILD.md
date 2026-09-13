# Optional Android QNN backend

`HV_USE_QNN` defaults OFF. Normal Windows and Android NNAPI/CPU builds do not need
QAIRT. The optional plugin is `backend.ort.qnn` with automatic priority 200,
above platform NNAPI (100) and CPU (0). Host code contains no QNN-specific selection.

The fixed next-stage route is:

```text
QAIRT/QNN SDK
+ custom ONNX Runtime Android
+ --use_qnn static_lib
+ --qnn_home <QAIRT_ROOT>
+ arm64-v8a
```

Use an authorized QAIRT installation and an Android ARM64 ONNX Runtime build with
the QNN EP statically linked. Its headers must match its `libonnxruntime.so`. The
ordinary Maven `onnxruntime-android` dependency used by the standard package does
not contain a QNN-enabled ORT and must not be treated as one. Upstream's build
accepts `--android --android_abi arm64-v8a --build_shared_lib --use_qnn static_lib
--qnn_home <QAIRT_ROOT>` in addition to SDK/NDK paths.

Configure this SDK's Android build with `-DHV_USE_QNN=ON`,
`-DHV_QNN_HOME=<QAIRT_ROOT>` and `-DHV_ONNXRUNTIME_ROOT=<CUSTOM_ORT_ROOT>`.
The configure check requires `include/QNN/QnnInterface.h` and ARM64. A directory
check cannot prove that ORT contains the provider: registration failure remains an
actionable creation failure, and BackendFactory tries the next configured backend.

The target device is Snapdragon 888 / SM8350. Start device validation with an FP32
model on HTP and optionally test HTP FP16 precision. Do not start with INT8/QDQ;
quantization adds a separate accuracy and operator-coverage variable before the
provider baseline is known.

The adapter registers provider `QNN`, `backend_path=libQnnHtp.so`,
`htp_performance_mode=balanced`. CPU EP fallback is disabled for this session, so
unsupported graphs fail initialization instead of being labeled QNN while running
entirely on CPU. `QNN_HTP` is reported only after session creation. This policy may
reject models requiring unsupported operations; compatible ModelPacks and device
execution still require validation.

Package the authorized matching ARM64 QNN runtime and device-specific HTP support
libraries with the Android runtime artifact according to QAIRT instructions. No
Qualcomm binary is included by this change. Existing standard packaging does not
automatically provision QNN dependencies.

Do not execute this route for 0.4.0-preview.3. First compare the forced CPU,
XNNPACK and NNAPI profiles using `diagnostics/ANDROID_0403_DEVICE_BENCHMARK.md`.
Enter the QNN stage only if the best RTMO provider remains below 15 raw body FPS or
above 180 ms result age. QNN-enabled linking and phone execution require the
optional dependencies; default-build success is not QNN proof.

Primary references:
- https://onnxruntime.ai/docs/build/android.html
- https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html
