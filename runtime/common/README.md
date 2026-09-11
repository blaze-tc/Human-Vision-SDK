# Shared configuration utilities

`config_io.h` provides bounded JSON reads and canonical root-confined paths for
initialization. It contains no runtime orchestration, model schema decoding or
Unity code. Host managers and plugins may consume these utilities without depending
on one another. File/hash work must stay outside the per-frame path.

Dependencies: C++ filesystem and vendored nlohmann JSON. Verified by PackTest path,
schema and model-integrity cases in `humanvision_plugin_tests`.
