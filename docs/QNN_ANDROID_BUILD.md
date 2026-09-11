# Optional Android QNN backend

`HV_USE_QNN` defaults OFF. Normal Windows and Android NNAPI/CPU builds do not need
QAIRT. The optional plugin is `backend.ort.qnn` with automatic priority 200,
above platform NNAPI (100) and CPU (0). Host code contains no QNN-specific selection.

Use an authorized QAIRT installation and an Android ARM64 ONNX Runtime build with
the QNN EP statically linked. Its headers must match its libonnxruntime.so. The
stock Android ORT dependency in this repository does not provide QNN. Upstream's
build accepts `--android --android_abi arm64-v8a --build_shared_lib
--use_qnn static_lib --qnn_home <QAIRT_ROOT>` in addition to SDK/NDK paths.

Configure this SDK's Android build with `-DHV_USE_QNN=ON`,
`-DHV_QNN_HOME=<QAIRT_ROOT>` and `-DHV_ONNXRUNTIME_ROOT=<CUSTOM_ORT_ROOT>`.
The configure check requires `include/QNN/QnnInterface.h` and ARM64. A directory
check cannot prove that ORT contains the provider: registration failure remains an
actionable creation failure, and BackendFactory tries the next configured backend.

The adapter registers provider `QNN`, `backend_path=libQnnHtp.so`,
`htp_performance_mode=balanced`. CPU EP fallback is disabled for this session, so
unsupported graphs fail initialization instead of being labeled QNN while running
entirely on CPU. `QNN_HTP` is reported only after session creation. This policy may
reject models requiring unsupported operations; compatible quantized ModelPacks
and device execution still require validation.

Package the authorized matching ARM64 QNN runtime and device-specific HTP support
libraries with the Android runtime artifact according to QAIRT instructions. No
Qualcomm binary is included by this change. Existing standard packaging does not
automatically provision QNN dependencies.

Verified scope is recorded in DEVELOPMENT_STATUS.md. QNN-enabled linking and phone
execution require the optional dependencies; default-build success is not QNN proof.

Primary references:
- https://onnxruntime.ai/docs/build/android.html
- https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html
