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

## D0.4 Unity local-video Demo

The verified Unity sources and scene are under `unity/HumanVisionDemo/`. The
current validated editor is Unity `2021.3.45f1`; this is an explicit project
owner-approved exception to the original 2022.3 planning baseline.

Before opening the Demo, build the native Release target and place these files
in the Unity project:

```text
build/windows-release/bin/Release/humanvision.dll
    -> Assets/Plugins/x86_64/humanvision.dll
build/windows-release/bin/Release/humanvision_onnxruntime.dll
    -> Assets/Plugins/x86_64/humanvision_onnxruntime.dll
models/detector/rtmdet_tiny_640.onnx
    -> Assets/StreamingAssets/HumanVision/Models/rtmdet_tiny_640.onnx
models/pose/rtmpose_s_256x192.onnx
    -> Assets/StreamingAssets/HumanVision/Models/rtmpose_s_256x192.onnx
```

HumanVision deliberately uses the private runtime filename
`humanvision_onnxruntime.dll`. Do not add a second public `onnxruntime.dll`
under `Assets/Plugins/x86_64/`; that conflicts with the older runtime bundled
by AzureKinectExamples.

Open `Assets/Scenes/HumanVisionD04Demo.unity` and enter Play Mode. The `-` and
`+` HUD buttons change `MaxBodies` at runtime; the other buttons switch between
the included one-person and two-person clips. Use
`HumanVision > Build Windows x64 Demo` for a standalone build under
`Builds/HumanVisionD04/`.
