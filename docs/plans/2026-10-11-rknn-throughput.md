# RKNN throughput follow-up

User authorizes further optimization toward 20–25 true skeleton FPS, with best
available method and no repeated field APK changes. No RK3588 is attached.

Target remaining measured costs: input_set ~14.24 ms, output_get ~4.45 ms;
model run ~27.74 ms. Do not add overlapping/cross-sample stage measurements or
claim simulator/OnePlus execution proves RK3588 throughput.

1. Add tested native FP16 input packing with explicit pipeline-supplied mean/norm,
   actual queried native layout/stride, bounded reusable memory and cache sync.
   Never assume PRIVATE decoder buffers are linear or bypass source identities.
2. Add reusable output bindings to the independent candidate context, retaining
   full FP32 ABI outputs. Compare complete same-frame output tensors; select only
   candidates with a measured end-to-end backend improvement. Unsupported native
   input/output or failed numerical qualification must try the conservative path.
3. Test normalization/stride/bounds and candidate failure/fallback first. Run real
   offline model checks where supported; explicitly record simulator limitations.
4. Build native host/Android, run regressions and architecture guards. Package one
   private APK and verify CPU/Vulkan regression on connected OnePlus if available.
5. Record exact evidence, unresolved RK3588 gates and conditional frame budgets.
   Do not change original model bytes, weaken precision gates or enable RGA.

Primary vendor references:
- https://github.com/airockchip/rknn-toolkit2/blob/master/rknpu2/examples/rknn_api_demo/src/rknn_create_mem_demo.cpp
- Pinned RKNN 2.3.2 rknn_api.h: native tensor queries, pass_through and mem_sync.
