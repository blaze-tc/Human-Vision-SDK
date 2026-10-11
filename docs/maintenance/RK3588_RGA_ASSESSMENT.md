# RK3588 decoder-to-NPU RGA assessment (2026-10-10)

Status: evaluated, not enabled. No connected RK3588 can validate this route.

Current `native/input/src/android/mediacodec_source.cpp` requests an
`AIMAGE_FORMAT_PRIVATE` ImageReader with GPU-sampled AHardwareBuffers. The
Vulkan input path imports the actual buffer, waits on the decoder acquire fence,
converts into the bounded preview texture, and retains source ownership until
GPU completion. A PRIVATE buffer is not a promise of linear NV12 or an ordinary
RGB dma-buf. Do not extract an arbitrary native_handle fd and assume its layout.

Rockchip documents external buffer import and Android AHardwareBuffer examples
in its [RGA guide](https://github.com/airockchip/librga/blob/main/docs/Rockchip_Developer_Guide_RGA_CN.md).
The official [RKNN memory example](https://github.com/airockchip/rknn-toolkit2/blob/master/rknpu2/examples/rknn_api_demo/src/rknn_create_mem_demo.cpp)
uses reusable input memory, NHWC, and explicit width-stride handling. These APIs
establish a candidate route; they do not establish compatibility with this
device's decoder/gralloc/driver combination or this model's numerical output.

Proposed later route: retain decoded AImage/AHB and acquire fence; import a
supported buffer into RGA; resize/color-convert into a reusable RGB destination
with the exact RKNN dimensions and row stride; wait for the real RGA completion
fence; synchronize memory for NPU access; run RKNN; retire the image when both
preview and inference consumers have finished. Keep a bounded latest-frame pool
and per-buffer source/generation/frame/timestamp/transform metadata.

Required hardware gates, before selecting this route:

- Record Android/gralloc, librga and kernel driver versions; query supported
  format/modifier/stride limits. Reject unsupported PRIVATE/compressed layouts.
- Verify fd ownership, import/release ordering, decoder acquire and RGA release
  fences; reconnect/cancel/error must not reuse buffers still owned by hardware.
- Compare complete RGB tensors and real pose outputs on held frames, including
  4:3, 16:9, mirror, range/matrix conversion and all four capture settings. RGA
  interpolation is not assumed identical to the frozen integer resize oracle.
- Compare steady per-person fresh and complete COCO-17 FPS, local result age,
  input conversion/copy/sync/model/output times, and preview publication rate
  against the same APK's baseline after warm-up and over a sustained run.

This change instead uses owned Unity GPU snapshots, exact model-size readback
with device-local pixel qualification, and separately qualified RKNN input
memory. RGA is absent from the default backend selection and has no success or
performance claim. If zero-copy import fails, preserve the proven route and log
the actual reason; never silently reinterpret buffer format or tensor layout.
