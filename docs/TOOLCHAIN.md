# TOOLCHAIN.md

# D0/D1 Toolchain Baseline

## Windows native build

- OS: Windows 10/11 x64
- Compiler: Visual Studio 2022, MSVC v143
- Language: C++17
- CMake: 3.24 or newer
- Generator: Visual Studio 17 2022 / x64
- Test framework: GoogleTest via CMake FetchContent or a pinned package
- Native output: `humanvision.dll` + import library + public headers

## Unity

- Recommended baseline: Unity 2022.3 LTS
- Target: Windows x86_64 Standalone
- Editor scripting backend may be Mono; Windows player must also be checked with IL2CPP before D0 final acceptance if practical
- Rendering/API choice must not be hard-wired into the native inference core

## ONNX Runtime

- D0 baseline: ONNX Runtime 1.29.0 CPU x64
- Start with CPU provider to validate correctness and portability
- Do not add GPU provider work until the performance HUD shows a concrete need
- Keep ORT behind `IInferenceBackend`

## Python reference environment

Use an isolated virtual environment. Recommended Python baseline: 3.10 x64.

The OpenMMLab reference/export environment is not part of the shipped Unity runtime. Its job is to:

- run official MMDetection/MMPose reference inference
- export/validate ONNX
- save exact environment metadata

After the first successful D0.1 reference run, commit:

```text
tools/reference/environment.lock.txt
```

containing the exact `python --version` and `pip freeze` output. Do not silently upgrade packages during the Demo milestone.

## FFmpeg / RTSP

D1 only.

- Prefer LGPL-compatible FFmpeg build settings for the intended commercial SDK distribution.
- Keep demux/decode behind `IRtspClient` / `IVideoDecoder` abstractions.
- D1 correctness target is H.264 RTSP over TCP first.

## Directory conventions

```text
third_party/
  onnxruntime/
  googletest/          # if not FetchContent
  ffmpeg/              # D1

models/
  detector/
  pose/
```

## Build presets target

Codex should provide CMake presets or equivalent commands for:

```text
windows-debug
windows-release
```

Minimum verification commands must be written to `DEVELOPMENT_STATUS.md` after each milestone.
