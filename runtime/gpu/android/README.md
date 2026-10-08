# Android AHardwareBuffer bridge

The runtime owns a three-slot AHardwareBuffer ring shared by Unity's Vulkan producer and the native inference worker. A slot carries a sync-fd payload and stays leased until the consumer proves GPU completion. Reconfiguration drains the old generation before replacing cached images and synchronization objects.

The consumer API is internal: `ClaimConsumer` borrows the newest slot, `ClaimDropped` borrows a superseded slot for GPU-only drain, and `RetireConsumer` requires `GpuQuiescent` proof. Unity render callbacks never wait for inference.

RK3588 field logs can report `AHardwareBuffer_describe().stride=0` for GPU-only
RGBA allocations. Preserve that measured opaque value. Neither the candidate
probe nor persistent slot creation may require a CPU row pitch before querying
Vulkan. Admission still requires the correct dimensions, layers, format and usage,
and successful properties, image-format capability, image creation, memory import,
bind and view creation on both logical devices. Nonzero strides below width remain
rejected. No CPU mapping, readback, fake stride or provider fallback is introduced.
The [Vulkan AHB image contract](https://docs.vulkan.org/spec/latest/chapters/memory.html#memory-external-android-hardware-buffer)
defines image compatibility through dimensions, format, usage and real import.

Focused checks: `AhbCapabilities`, `AhbAndroidProbe`, `UnityVulkanAndroidAdapter`
and `UnityVulkanBridge`. Host fixtures verify opaque descriptors, actual probe and
persistent-slot paths, Vulkan failures and resource cleanup. They do not establish
physical RK3588 import support or recognition FPS; that requires the test APK and
fresh device logs with advancing submitted/processed/result counters.

The ncnn worker holds one `ConsumerFrame` for the whole observation. A backend session may call `Run` repeatedly for the detector or pose ROIs while its imported image remains acquired. A successful run installs a one-shot internal completion callback. The worker calls `CompleteGpuRole(frame, false, error)` between model sessions to finish the role's GPU work and return external image ownership while keeping the slot claimed; the next session reacquires the same image without importing the producer sync fd again. The final role calls `CompleteGpuRole(frame, true, error)` to release the image and retire the slot. The worker must keep the borrowed frame alive through that final call and complete it before destroying the backend session.
