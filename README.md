# Human Vision SDK

Windows x64 / Unity-first multi-person RGB camera vision SDK. The current implementation stage is tracked in [`docs/DEVELOPMENT_STATUS.md`](docs/DEVELOPMENT_STATUS.md); later roadmap features are intentionally out of scope until the local-video vertical slice is verified.

## D0 native setup

1. Follow [`tools/reference/README.md`](tools/reference/README.md) to create the locked Python environment and export the RTMDet/RTMPose ONNX files.
2. Install the plan-locked ONNX Runtime 1.29.0 CPU x64 package:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup/download_onnxruntime.ps1
   ```

3. From an MSVC v143 x64 developer environment, configure, build, and test:

   ```powershell
   cmake --preset windows-debug --fresh
   cmake --build --preset windows-debug --clean-first
   ctest --preset windows-debug
   ```

The native public boundary is the C ABI under `native/include/humanvision/`. Model/runtime-specific types remain internal.

## D0.3 native video benchmark

The build also produces `hv_video_benchmark.exe`. It invokes FFmpeg only as an
isolated MP4 reader and sends decoded BGR24 frames through the same asynchronous
public C ABI used by Unity. See [`tools/benchmark/README.md`](tools/benchmark/README.md)
for regression media generation and benchmark commands.
