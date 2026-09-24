# ncnn Vulkan backend

`backend.ncnn.vulkan` is the strict Android ARM64 API 26 GPU backend. It matches Unity's physical device by device and driver UUID, imports the bridge's cached AHardwareBuffer slots with a GPU wait on the producer sync fd, performs crop, bilinear resize, color and normalization on Vulkan, and explicitly converts tensor packing and precision before ncnn extraction.

ModelPack schema 2 supplies the named input/output contract, asset hashes, and per-output byte bounds. Creation fails with a diagnostic when any required Vulkan, AHB, external semaphore, device identity, model, or declared FP16 capability is unavailable. The backend does not select another provider.
