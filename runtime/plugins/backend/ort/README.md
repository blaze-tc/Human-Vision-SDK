# ORT backend plugin

Current implementation: `backend.ort.cpu`, registered with `HV_QueryOrtCpuPlugin`.
`HV_QueryOrtAcceleratedPlugin` additionally registers `backend.ort.directml` in
Windows DirectML builds or `backend.ort.nnapi` on Android. CPU-only builds reject
that query. BackendFactory now handles ordered creation fallback and the legacy
pipeline requests model sessions through HostServices.

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
