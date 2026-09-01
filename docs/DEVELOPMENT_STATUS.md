# DEVELOPMENT_STATUS.md

# HumanVisionSDK Development Status

**Status date:** 2026-09-01  
**Current stage:** D0 - Windows Local Video Vertical Slice  
**Current milestone:** D0.1 Python/OpenMMLab Reference + ONNX Contract  
**Current implementation state:** D0.0 repository/build bootstrap completed and verified.

## Immediate user-visible target

The next meaningful checkpoint is **D0.4 Unity Local Video Demo**:

```text
MP4 -> Unity VideoPlayer -> HV_SubmitFrame -> RTMDet -> Tracker -> RTMPose
    -> Unity video + BBox + TrackId + COCO17 skeleton + performance HUD
```

Do not start RTSP until this local-video path is visibly working.

## Milestone state

- [x] D0.0 Repository & Build Bootstrap
- [ ] D0.1 Python/OpenMMLab Reference + ONNX Contract
- [ ] D0.2 Native ONNX Runtime + RTMDet
- [ ] D0.3 RTMPose + Tracker + Native Video Benchmark
- [ ] D0.4 Unity Local Video Demo
- [ ] D1.0 RTSP IPC Input
- [ ] D1.1 Real 1~4 Person Field Validation
- [ ] D1.2 Demo Stabilization & Decision Report

## Scope lock

Until D1.2 is accepted:

- no Android
- no RKNN
- no segmentation/matting
- no TensorRT/CUDA optimization
- no action recognition

## Latest verification

### D0.0 Repository & Build Bootstrap - PASS

- Date: 2026-09-01
- Implementation commit: `5114449e8d331485fdae4daaacc942cd44ccc093`
- Host: Windows x64
- Generator: Ninja Multi-Config
- Compiler: MSVC 19.44.35228.0 from VCTools 14.44.35207 (v143)
- CMake: 4.3.1-msvc1 (project minimum remains 3.24)
- GoogleTest: pinned commit `b514bdc898e2951020cbdca1304b75f5950d1f59` (`v1.15.2`)

Developer environment command used before configure/build:

```bat
call "D:\Microsoft Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
```

Expected RED verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug
```

- Configure result: exit 0.
- Build result before implementing `HV_GetVersionString`: exit 1 while linking the smoke test because the DLL had no exported implementation/import library (`LNK1104` for `native\Debug\humanvision.lib`).
- This confirmed that the smoke test could not pass without the production version function.

Fresh Debug verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
ctest --preset windows-debug
```

- Configure: PASS, exit 0.
- Build: PASS, 8/8 build steps, exit 0.
- CTest: PASS, 1/1 tests, 0 failures (`SdkVersion.IsNonEmpty`).

Fresh Release verification:

```powershell
cmake --preset windows-release --fresh
cmake --build --preset windows-release --clean-first
ctest --preset windows-release
```

- Configure: PASS, exit 0.
- Build: PASS, 8/8 build steps, exit 0.
- CTest: PASS, 1/1 tests, 0 failures (`SdkVersion.IsNonEmpty`).

Artifact checks:

- Debug: `build/windows-debug/bin/Debug/humanvision.dll` (52,224 bytes).
- Release: `build/windows-release/bin/Release/humanvision.dll` (9,728 bytes).
- `dumpbin /headers`: `8664 machine (x64)`, PE32+ DLL.
- `dumpbin /exports`: one D0.0 export, `HV_GetVersionString`.
- Model/runtime dependency: none.
- Integration/golden test: not applicable to D0.0.
- Benchmark media/settings/FPS/latency: not applicable to D0.0.

Known issues / environment notes:

- The installed Visual Studio host is Visual Studio 2026 18.9.1, while `TOOLCHAIN.md` names Visual Studio 2022 as the baseline. The verified build uses the installed v143 compiler through `VsDevCmd` plus Ninja because the VS 2026 MSBuild host does not include the v143 MSBuild platform targets.
- The currently running UnitySkills instance reports Unity 2021.3.45f1 and project `Human-Vision-SDK-Test`. D0.0 is native-only, so this did not affect acceptance. Before D0.4, the active project must use the required Unity 2022.3 LTS baseline.

Next milestone: D0.1 Python/OpenMMLab Reference + ONNX Contract.

## Advancement rule

Only mark a milestone complete when its acceptance criteria in `CODEX_DEMO_EXECUTION_PLAN.md` are met with real outputs. Then update `Current milestone` to the next item before implementing it.
