# Android AHardwareBuffer bridge

The runtime owns a three-slot AHardwareBuffer ring shared by Unity's Vulkan producer and the native inference worker. A slot carries a sync-fd payload and stays leased until the consumer proves GPU completion. Reconfiguration drains the old generation before replacing cached images and synchronization objects.

The consumer API is internal: `ClaimConsumer` borrows the newest slot, `ClaimDropped` borrows a superseded slot for GPU-only drain, and `RetireConsumer` requires `GpuQuiescent` proof. Unity render callbacks never wait for inference.
