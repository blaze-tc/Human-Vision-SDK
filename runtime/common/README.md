# Shared configuration utilities

`config_io.h` provides bounded JSON reads and canonical root-confined paths for
initialization. It contains no runtime orchestration, model schema decoding or
Unity code. Host managers and plugins may consume these utilities without depending
on one another. File/hash work must stay outside the per-frame path.

Dependencies: C++ filesystem and vendored nlohmann JSON. Verified by PackTest path,
schema and model-integrity cases in `humanvision_plugin_tests`.

`plugin_backend.h` adapts the legacy internal tensor interface to HostServices and
the C tensor ABI. It copies borrowed output data into reusable legacy buffers and
supports at most 16 float32 outputs. It must not select concrete backend IDs.
Real-model integration is verified by LegacyPlugin in humanvision_native_tests.

`pipeline_diagnostics.h` is an internal fixed-capacity registry used by statically
linked pipelines to publish process metrics without extending the versioned plugin
ABI. Registration happens during pipeline creation, updates reuse an existing slot,
and RuntimeHost copies the metrics with the matching observation result.

`backend_diagnostics.h` uses a fixed registry to publish optional runtime/driver
identity, core mask and initialization/input-set/execute/output-get/release times.
The host reads generic snapshots without depending on vendor implementations.
Counters and stage snapshots are internal and preserve the existing plugin ABI.

`image_transform_contract.*` implements explicit packed-image validation, stable
plan/profile fingerprints, inverse geometry and three-slot completion ownership.
`cpu_image_service.*` is the reference RGB/BGR/RGBA/BGRA transform provider. It
honors source row/pixel stride, supports crop/rotation/mirror and nearest/bilinear
sampling, and applies mean/norm only when the plan assigns normalization to the
service. Float RGB output is explicit; a pipeline-owned normalization stays out
of image preparation. Unknown layouts and pending fences are Unsupported.

CPU output is copied into service-owned storage. A slot's vector can grow during
warm-up/source-size changes up to its configured maximum (default64MiB per slot),
then is reused; no worker pool is created. `PreparedLeasePool` prevents cancelled
pending work from releasing a slot and uses generation tokens to reject stale or
double releases. Focused tests compare stride/rotation/normalization pixels and
exercise pool pressure, identity mismatches and true-completion retirement.
