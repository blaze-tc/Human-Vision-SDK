# Android AHardwareBuffer bridge

The runtime owns a three-slot AHardwareBuffer ring shared by Unity's Vulkan producer and the native inference worker. A slot carries a sync-fd payload and stays leased until the consumer proves GPU completion. Reconfiguration drains the old generation before replacing cached images and synchronization objects.

The consumer API is internal: `ClaimConsumer` borrows the newest slot, `ClaimDropped` borrows a superseded slot for GPU-only drain, and `RetireConsumer` requires `GpuQuiescent` proof. Unity render callbacks never wait for inference.

The ncnn worker holds one `ConsumerFrame` for the whole observation. A backend session may call `Run` repeatedly for the detector or pose ROIs while its imported image remains acquired. A successful run installs a one-shot internal completion callback. The worker calls `CompleteGpuRole(frame, false, error)` between model sessions to finish the role's GPU work and return external image ownership while keeping the slot claimed; the next session reacquires the same image without importing the producer sync fd again. The final role calls `CompleteGpuRole(frame, true, error)` to release the image and retire the slot. The worker must keep the borrowed frame alive through that final call and complete it before destroying the backend session.
