# Private Android model execution probes

These standalone executables measure real models and retain every final FP32
output for numerical comparison. They do not initialize Unity, decode a video,
track people or count fresh skeleton FPS. Run them serially with device capture:
concurrent Unity inference, background GPU clocks, thermal state and profiler
traffic can change timings. Absolute shell GPU timings are not app predictions.

## Build

Use the project's pinned Android ARM64/API26 toolchain, ORT Android dependency
and audited static NCNN install; pass their actual paths, never a substitute
model/runtime. A PowerShell example (variables are caller-defined paths):

```powershell
& $taskCmake -S tools/benchmark/mobile_ort_probe -B out/mobile-model-probe -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=$taskNinja" "-DCMAKE_TOOLCHAIN_FILE=$taskNdk/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release `
  "-DORT_ROOT=$taskOrtRoot" "-Dncnn_DIR=$taskNcnnRoot/lib/cmake/ncnn"
& $taskCmake --build out/mobile-model-probe --parallel 4
```

Push the selected executable, identical model bytes, libonnxruntime.so for ORT,
and prepared input to a private /data/local/tmp directory. chmod the executable
755 after each push. Supply LD_LIBRARY_PATH for ORT. Do not replace app libraries.

## ORT input and invocation

```
hv_mobile_ort_probe model.onnx input.fp32 side threads iterations prefix [cpu|xnnpack]
```

Input is exactly 1x3xside x side little-endian float32 NCHW, prepared with the
model's declared color order/normalization/letterbox. RTMO416 uses the existing
BGR, unnormalized0..255 contract and114 letterbox. Wrong byte length, nonfinite
input, incompatible positive dimensions and nonfloat input are rejected. Dynamic
ONNX dimensions bind to the supplied shape. One input is required. Warmup10
runs precede bounded measurements; all final outputs must be finite float32.

CPU threads set the ORT intra-op pool. XNNPACK is an explicit provider experiment:
its pool receives this thread count while ORT has one thread; both inter-op and
spinning remain disabled. Provider append failure is an error, never silent CPU
substitution. Successfully appending XNNPACK does not prove every node uses it.
Output files are prefix-NAME.fp32; reported shapes determine their lengths.

## NCNN input and invocation

```
hv_mobile_ncnn_probe model.param model.bin input.fp32 width height mode threads iterations prefix
```

This probe is limited to the existing raw YOLO pose contract, input in0 and
out0/out1, columns65/51. Exact aligned CHW RGB float32 input uses the model's
1/255 normalization and114 letterbox. Modes cpu, baseline, sgemm and
no-local-memory keep FP32 storage/arithmetic, disable subgroups, and preserve
internal packing. GPU measurements include output download; CPU measurements
use the same tensors. If the pinned NCNN build disables OpenMP, the CPU thread
parameter cannot create an OpenMP pool; report that limitation explicitly.
GPU allocator/tensor/network lifetime is owned and retired on every return.

## Acceptance and results

Compare output shapes and every saved value before interpreting speed. Keep
model/runtime SHA, input bytes, warmup/run count, temperature, process foreground
state and scheduling limitations with the result. Same fixture parity is not
whole-model pose/precision qualification. Do not promote failed FP16/INT8 gates.

The2026-10-09 OnePlus experiment is recorded in
[the report](../../../docs/reports/2026-10-09-oneplus-performance.md).
Existing device session analyzer tools/benchmark/analyze_settings_device_log.py
measures actual fresh results separately from these raw model probes.
