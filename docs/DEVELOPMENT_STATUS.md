# DEVELOPMENT_STATUS.md

# HumanVisionSDK Development Status

**Status date:** 2026-09-02  
**Current stage:** D0 - Windows Local Video Vertical Slice  
**Current milestone:** D0.3 RTMPose + Tracker + Native Video Benchmark  
**Current implementation state:** D0.2 asynchronous native C ABI, latest-frame queueing, ONNX Runtime 1.29.0 CPU backend, RTMDet-tiny preprocessing/postprocessing, real-image golden comparisons, and runtime `MaxBodies` enforcement completed and verified.

## Immediate user-visible target

The next meaningful checkpoint is **D0.4 Unity Local Video Demo**:

```text
MP4 -> Unity VideoPlayer -> HV_SubmitFrame -> RTMDet -> Tracker -> RTMPose
    -> Unity video + BBox + TrackId + COCO17 skeleton + performance HUD
```

Do not start RTSP until this local-video path is visibly working.

## Milestone state

- [x] D0.0 Repository & Build Bootstrap
- [x] D0.1 Python/OpenMMLab Reference + ONNX Contract
- [x] D0.2 Native ONNX Runtime + RTMDet
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

### D0.2 Native ONNX Runtime + RTMDet - PASS

- Date: 2026-09-02
- Implementation commit: `3d2375c5548bec1a54ee2360efc33cd1a9965427`
- Host: Windows x64, ONNX Runtime CPU execution provider
- Generator/compiler: Ninja Multi-Config, MSVC 19.44.35228.0 (v143)
- Native ONNX Runtime: 1.29.0 Windows x64
- ONNX Runtime release archive SHA-256: `c9b4b7086b529ad814f428c1bad028e20a25d7dc0699836775faace4ab5b78b2`
- ONNX Runtime package commit: `2e2543fbe9fae542f921d47a72d21d5a4ef0b710`
- ONNX Runtime DLL SHA-256: `69d8e6d3879a3b4001cdc74c8ed9ccc7e7f799a5b847059738323404519ec471`

Expected RED verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
```

- Configure result: exit 0.
- Build result before implementation: exit 1 because the acceptance tests required the absent production headers `core/latest_frame_slot.h`, `core/result_snapshot_store.h`, `backend/onnx/onnx_runtime_backend.h`, and RTMDet modules.
- This confirmed that asynchronous latest-frame behavior, immutable complete snapshots, generic ONNX execution, and RTMDet preprocessing/postprocessing could not pass through test-only stubs.

Dependency setup and developer environment:

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup\download_onnxruntime.ps1
```

```bat
call "D:\Microsoft Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
```

- The setup script verifies the existing package commit and runtime DLL hash, verifies the archive hash on download, and leaves ONNX Runtime binaries Git-ignored.

Fresh Debug verification:

```powershell
cmake --preset windows-debug --fresh
cmake --build --preset windows-debug --clean-first
ctest --preset windows-debug
```

- Configure: PASS, exit 0.
- Build: PASS, 22/22 build steps, exit 0.
- CTest: PASS, 15/15 tests, 0 failures, 6.20 seconds.

Fresh Release verification:

```powershell
cmake --preset windows-release --fresh
cmake --build --preset windows-release --clean-first
ctest --preset windows-release
```

- Configure: PASS, exit 0.
- Build: PASS, 22/22 build steps, exit 0.
- CTest: PASS, 15/15 tests, 0 failures, 3.95 seconds.
- A final incremental Release regression after documentation/setup refinements also passed 15/15 tests in 3.60 seconds.

Acceptance and golden results:

- C ABI configuration tests cover invalid config/model errors, create/destroy cycles, unsupported pixel formats, caller-buffer ownership, and runtime `MaxBodies` values 1/2/4/6/8.
- Latest-frame tests use a deterministic slow worker and confirm that the newest frame wins while overwritten frames increment dropped-frame statistics.
- Snapshot capacity tests confirm that insufficient destination capacity reports the required count without a partial copy.
- The generic ONNX backend executes a mathematical add-one graph; detector acceptance never uses fixed boxes or fake joints.
- RTMDet preprocessing matches the committed official golden tensor samples for RGBA32, BGRA32, RGB24, and BGR24 input.
- Official-image native/Python comparison: one real detection, maximum box error `0.075927734375 px`, IoU `0.99961433162530111`, native score `0.91604882478713989`, and ONNX inference time `112.90849304199219 ms`.
- Real two-person composite reference produces two detections. The native result returns one body at `MaxBodies=1` and two at `MaxBodies=2`, proving that body capacity is selected at runtime rather than hard-coded.
- Concurrency stress: latest-frame tests passed 20 consecutive repetitions; asynchronous real-detector C API test passed 5 consecutive repetitions.
- D0.1 Python reference regression: PASS, 4/4 tests, 0 failures.

Artifact checks:

- Release: `build/windows-release/bin/Release/humanvision.dll`, 81,920 bytes, SHA-256 `5bb16b9c9c71d646ea753bbbe071c5617f0519a543c485116a363e4966ffb81c3`.
- Debug: `build/windows-debug/bin/Debug/humanvision.dll`, 366,592 bytes.
- `dumpbin /headers`: x64 PE32+ DLL.
- `dumpbin /exports`: exactly 10 public C ABI exports: `HV_Create`, `HV_Destroy`, `HV_GetBodies`, `HV_GetBodyCount`, `HV_GetLastError`, `HV_GetLatestResultMeta`, `HV_GetStats`, `HV_GetVersionString`, `HV_Reconfigure`, and `HV_SubmitFrame`.
- The Release output contains the required `onnxruntime.dll`; its copied hash matches the verified source DLL hash above. `onnxruntime_providers_shared.dll` is also copied.
- Model ONNX files and ONNX Runtime binaries are not staged or committed.
- Integration/golden test: the official-image and real two-person RTMDet comparisons above are the D0.2 golden tests.
- Milestone video benchmark: not applicable to D0.2; D0.3 owns the native video throughput/latency benchmark.

Known issues / environment notes:

- D0.2 intentionally publishes detector-only bodies with `track_id=-1` and invalid/zero joints. RTMPose, stable tracking, detector interval behavior, and video benchmarking belong to D0.3.
- The installed Visual Studio host remains Visual Studio 2026 with the v143 compiler, rather than the documented Visual Studio 2022 host baseline. Both Debug and Release native outputs are verified.
- The active UnitySkills project still needs to move from Unity 2021.3.45f1 to the required Unity 2022.3 LTS baseline before D0.4 acceptance.

Next milestone: D0.3 RTMPose + Tracker + Native Video Benchmark.

### D0.1 Python/OpenMMLab Reference + ONNX Contract - PASS

- Date: 2026-09-01
- Implementation commit: `3990968cf0420b0bc0f6a53684642cdc34738144`
- Host: Windows x64, CPU reference inference
- Python: 3.10.21 (uv-managed CPython)
- PyTorch / TorchVision: 2.1.0+cpu / 0.16.0+cpu
- OpenMMLab: MMCV 2.1.0, MMDetection 3.2.0, MMPose 1.3.2, MMDeploy 1.3.1
- ONNX / ONNX Runtime Python: 1.15.0 / 1.23.2
- Reference media: official MMDeploy `demo/resources/human-pose.jpg`, SHA-256 `dd25fd8186e9ce27625520e24ac13ec6747316e51d20b124d3271c5764686d4e`

Locked official sources:

- MMDetection `v3.2.0`, commit `fe3f809a0a514189baf889aa358c498d51ee36cd`.
- MMPose `v1.3.2`, commit `5408bc76f5b848cf925a0d1857899011d8c5b497`.
- MMDeploy `v1.3.1`, commit `bc75c9d6c8940aa03d0e1e5b5962bd930478ba77`.
- RTMDet-tiny checkpoint SHA-256: `78e30dcce0c6f594eaff0d6977b84b4103688b4aff0ad1aa16008a8cc854a7fb`.
- RTMPose-s checkpoint SHA-256: `29dcacbb5c5f3ab2f03a67fedcb58cf7287f93b9a8a9d893f42416b63fc304ba`.

Expected RED verification:

```powershell
py -3.13 -m unittest discover -s tests\reference -v
```

- Result before implementing `tools.reference.contracts`: exit 1, 4/4 tests failed because the required D0.1 contract module was absent.
- This confirmed that model metadata completeness and model-file SHA validation could not pass without production contract code.

Final reference/export/comparison verification:

```powershell
.venv-reference\Scripts\python.exe -m unittest discover -s tests\reference -v
.venv-reference\Scripts\python.exe -m tools.reference.run_reference --device cpu
.venv-reference\Scripts\python.exe -m tools.reference.export_models --model all --device cpu
.venv-reference\Scripts\python.exe -m tools.reference.compare_onnx
```

- Contract unit tests: PASS, 4/4 tests, 0 failures.
- Official PyTorch reference: PASS, 1 person detection and 17 COCO joints.
- MMDeploy export: PASS for RTMDet-tiny and RTMPose-s; both graphs pass `onnx.checker` and load in ONNX Runtime CPU.
- Detector comparison: PASS; count 1/1, maximum source-coordinate box error `0.000033021 px`, IoU `0.999999682`, score error `1.19209e-7` (limits: `1.5 px`, `0.99`, `0.01`).
- Pose comparison: PASS; 17 joints, maximum restored coordinate error `0 px`, maximum score error `1.31130e-6`, maximum raw SimCC error `5.82635e-6` (limits: `0.5 px`, `0.005`, `0.002`).
- D0.0 native regression: PASS in Debug and Release, 1/1 CTest each, 0 failures.

Artifacts/contracts:

- Detector ONNX: `models/detector/rtmdet_tiny_640.onnx`, 22,289,445 bytes, SHA-256 `6d0d4e5da97772cbc8d25368f7796b1250fdf12c61861b228d31b321fc519e3d`.
- Pose ONNX: `models/pose/rtmpose_s_256x192.onnx`, 21,916,761 bytes, SHA-256 `9060b6cf176a49ba687feb993830b18293cc06b6675c5231c1e388d4e7a1c3ef`.
- ONNX/checkpoint binaries remain Git-ignored. Reproduction commands, exact environment snapshot, official source/checkpoint locations, model contracts, golden outputs, and numerical comparison results are committed.
- Integration/golden test: the official image PyTorch-to-ONNX comparisons above are the D0.1 golden tests.
- Benchmark media/settings/FPS/latency: not applicable to D0.1.

Known issues / environment notes:

- ONNX Runtime 1.29.0 does not publish a Windows `cp310` wheel, so the Python reference environment uses the last available official Windows `cp310` wheel, 1.23.2. The native D0.2 dependency remains the plan-locked ONNX Runtime 1.29.0 CPU x64 package.
- The MMDeploy `tools/deploy.py` CLI produced a valid detector graph, then failed only in its post-export MMDetection visualization because the exported label tensor is float. `export_models.py` uses the official `mmdeploy.apis.torch2onnx` API to omit that unrelated visualization step; the resulting graph passes structure and numerical verification.
- Pinned OpenMMLab emits registry/deprecation/tracer warnings during export on this Windows stack. The locked source/checkpoint checks, ONNX checker, model contract hashes, and numerical comparisons all pass.

Next milestone: D0.2 Native ONNX Runtime + RTMDet.

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

## Advancement rule

Only mark a milestone complete when its acceptance criteria in `CODEX_DEMO_EXECUTION_PLAN.md` are met with real outputs. Then update `Current milestone` to the next item before implementing it.
