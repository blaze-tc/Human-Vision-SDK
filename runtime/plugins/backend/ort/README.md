# ORT backend plugin

Current implementation: `backend.ort.cpu`, registered with `HV_QueryOrtCpuPlugin`.
This is the CPU portion of the backend migration; accelerated plugins and profile
fallback orchestration are still pending. It never claims accelerator execution.

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
