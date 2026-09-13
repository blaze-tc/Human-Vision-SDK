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
