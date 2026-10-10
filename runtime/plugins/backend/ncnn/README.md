# ncnn Vulkan backend

`backend.ncnn.vulkan` is the strict Android ARM64 API 26 GPU backend. It matches Unity's physical device by device and driver UUID, imports the bridge's cached AHardwareBuffer slots with a GPU wait on the producer sync fd, performs crop, bilinear resize, color and normalization on Vulkan, and explicitly converts tensor packing and precision before ncnn extraction.

ModelPack schema 2 supplies the named input/output contract, asset hashes, and per-output byte bounds. Creation fails with a diagnostic when any required Vulkan, AHB, external semaphore, device identity, model, or declared FP16 capability is unavailable. The backend does not select another provider.

The OnePlus dispatch experiment is deliberately separate from shipping NCNN.
`HV_ANDROID_NCNN_DISPATCH_EXPERIMENT` requires Android stage tracing and a
`dispatch_experiment` receipt with kind `hv-ncnn-private-dispatch-experiment-v1`,
256K bounded budget and shipping_eligible=false. It excludes layer timestamp
instrumentation. Normal builds reject the private dependency. Preparation uses
`tools/benchmark/ncnn_dispatch_experiment.py`; numerical outputs and real
SettingsDemo fresh-result throughput must both pass before any production
promotion. All native/managed ABI, FP32 model bytes and synchronization waits
remain intact. A shell model timing is not skeleton FPS or RK3588 validation.

Use `--adreno-only` for the actual device APK experiment. The copied budget
policy limits the change to the exactly named Adreno660 and the qualified
packed FP32 option combination. Unknown GPUs, Mali and other precision options
retain upstream budget selection. `NcnnDispatchBudget.*` tests this boundary;
the fixed pinned dependency also compiles it as C++11. The source manifest pins
the copied policy header independently of the audited original archive.
