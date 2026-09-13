# ORT backend plugin

Current implementation: `backend.ort.cpu`, registered with `HV_QueryOrtCpuPlugin`.
`HV_QueryOrtAcceleratedPlugin` additionally registers `backend.ort.directml` in
Windows DirectML builds or `backend.ort.nnapi` on Android. CPU-only builds reject
that query. BackendFactory now handles ordered creation fallback and the legacy
pipeline requests model sessions through HostServices.

`HV_QueryOrtXnnpackPlugin` registers `backend.ort.xnnpack` only on Android. It
uses the pinned ONNX Runtime Android 1.23.0 library, sequential execution, ORT
intra/inter-op thread counts of one, disabled ORT thread spinning, and an explicit
XNNPACK EP with `intra_op_num_threads=4`. Provider append/session failure rejects
creation. `actual=XNNPACK` is reported only after session creation succeeds.

Profiles with `backend.allow_fallback=false` pass the selected plugin identity as
the requested provider and stop after its first creation failure. This prevents a
forced NNAPI or XNNPACK benchmark from being silently measured as CPU. The default
`auto` profile retains ordered provider fallback.

`HV_QueryOrtQnnPlugin` is optional (`HV_USE_QNN`). It registers `backend.ort.qnn`
only in enabled Android builds, priority 200. It requests HTP, disables CPU EP
fallback, and lets BackendFactory handle creation failure. See
`docs/QNN_ANDROID_BUILD.md` for dependency and verification limitations.

Session diagnostics report the requested provider, the provider configured in the
current session and any NNAPI registration/session/run failure that led to CPU
fallback. `accelerated` means an accelerator provider was configured, not that all
graph nodes executed there; partial CPU partitions remain possible. Hardware
coverage and speed are not inferred from successful session creation.

Consumes one named float32 input tensor, positive dimensions (rank 1-8), and exact
byte count. Rejects overflow, incompatible types and provider requests. Produces
borrowed float32 output views valid until the next run or destruction. Instances
own model sessions and reusable copy buffers; callers own view arrays. Use one
worker per instance. C entry points contain exceptions.

Depends on plugin ABI and the existing native ORT implementation. No body/hand
decoding, identities, regions, Host implementation or Unity dependencies.

`BackendPlugin.*` tests use the checked-in add-one ONNX fixture to verify real
execution, dimensions and unsupported-provider handling. These are automated
functional checks, not device performance measurements. General multi-input and
non-float models are not supported by this transitional implementation.
