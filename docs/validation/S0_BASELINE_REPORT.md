# S0 reproducible baseline report

Date: 2026-09-07. This is baseline/feasibility acceptance, not the new skeleton
quality or eight-person 30 FPS acceptance.

## Host and unchanged runtime

- Windows development machine: AMD Ryzen 7 4800H, 8 cores / 16 threads,
  approximately 31.36 GiB OS-visible RAM; RTX 2060 with 6144 MiB VRAM,
  NVIDIA driver 592.82.
- Unity 2021.3.45f1, project `E:/UnityProject/Human-Vision-SDK-Test`, instance
  `HumanVisionSDKTest_F988EAA7`. This is the previously user-approved host.
- Native ONNX Runtime remains the isolated CPU runtime. No GPU provider,
  model, native ABI, or Azure sensor integration was changed in S0.

## Scene-media regression

Added `DemoSceneStartupVideoExistsInStreamingAssets` to the existing EditMode
scene contract tests and copied the source test into the active Unity project.
It opens the saved scene and checks the configured startup media on disk.

The first run, job `9a77360d`, passed 1/1. The imported scene already contained
`HumanVision/Media/4859224-uhd_3840_2160_25fps.mp4` by test time, whereas the
earlier audit had found a path missing `Media/`. No production path fix was
performed by S0. An expected RED was therefore not applicable; the pass must
not be represented as a red-green bug fix. The evidence file named
`out/validation/s0/unity-red.json` contains this initial PASS.

After `asset_refresh` and compilation completion, full EditMode job `363c338e`
passed 38/38 tests, 0 failures, 19 seconds. The source test in the repository
and active project matches. An attempted temporary private-field change via
`component_set_property` was rejected as `SEMANTIC_INVALID`; no change occurred.
The saved scene hash before and after runtime checks is identical:
`3C3D9C2573AC4EAFFF6981057A651B76E7C64C3A2BCE99F88D6C772934C430E8`.

## Native builds and tests

Commands (after `VsDevCmd.bat -arch=x64 -host_arch=x64 -vcvars_ver=14.44`):

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-release
```

The CMake/CTest executables are under
`D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/`.
Both incremental builds succeeded (`ninja: no work to do`). Debug CTest passed
29/29 in 12.04 seconds; Release passed 29/29 in 8.07 seconds. These include real
detector/pose golden checks, async queue/snapshot tests, runtime body limits,
tracking and private ONNX Runtime dependency isolation.

Reproduction wrapper and full output:
`out/validation/s0/native-regression.cmd` and `native-regression.log`.

## Sequential real-video benchmark

```powershell
foreach ($bodyLimit in @(1,2,8)) {
  & build/windows-release/bin/Release/hv_video_benchmark.exe `
    --input tests/testdata/d0_3_two_people.mp4 --width 436 --height 346 `
    --fps 5 --frames 10 --max-bodies $bodyLimit `
    --detector-model models/detector/rtmdet_tiny_640.onnx `
    --pose-model models/pose/rtmpose_s_256x192.onnx `
    --output-prefix "out/validation/s0/two_people_max$bodyLimit"
}
```

Values below are averages from the JSON reports, not the CLI's latest-frame
total. All three runs processed 10/10 frames; each returned body had at least
17 valid joints.

| MaxBodies | Observed body count | Detection ms | Pose ms | Total ms | 1000 / total ms |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 | 173.097 | 19.244 | 192.348 | 5.199 |
| 2 | 2 | 172.489 | 36.791 | 209.285 | 4.778 |
| 8 | 2 | 168.176 | 34.133 | 202.313 | 4.943 |

This sequential fixture benchmark measures processing cost, not realtime
end-to-end FPS. MaxBodies=8 on two visible people is not eight-person validation.

## Unity build and runtime

Executed menu `HumanVision/Build Windows x64 Demo`. Unity Editor.log records:

```text
HumanVision Windows x64 build succeeded:
E:\UnityProject\Human-Vision-SDK-Test\Builds\HumanVisionD04\HumanVisionD04.exe
(438964244 bytes, 00:00:09.2805484).
```

`Library/LastBuild.buildreport` was updated on 2026-09-07. The build request
result is saved in `out/validation/s0/unity-build-request.json`.

Editor playback of the existing dynamic clip ran at 1280x720 analysis resolution
and 25 source FPS. A component snapshot returned two bodies, sequence 70 and
source frame 331; the source was playing and both error strings were empty.
GPU readback drops/errors were 0/0. `debug_get_errors` returned zero errors.
The later screenshot reported sequence 136, 4.8 inference FPS, 173.2 render FPS,
163.5 ms detector, 38.7 ms pose and 202.1 ms total.

Important visual limitation: the two captured dynamic-video screenshots show
no skeleton lines. One reports two result bodies and an eight-frame result age;
the other reports zero result bodies despite people in the presented image.
These samples do not establish the precise cause or continuous overlay
correctness. Preserve this as a timestamp/display/quality regression case for
the following-quality milestone, rather than claiming Kinect-like following.

Screenshots are under the active project's `Assets/Screenshots/`:
`humanvision_s0_runtime.png` and `humanvision_s0_runtime_second.png`.
Component snapshots are under `out/validation/s0/runtime-manager.json` and
`runtime-source.json`. Play mode was stopped after observation.

## Outcome

S0 baseline and model-feasibility deliverables are complete with the existing
visual limitations recorded. `S0_MODEL_FEASIBILITY.md` selects small/medium
133-point wholebody models for the next measured experiment, not production
deployment. Hand precision, real 8-person throughput and all RK3588 hardware
claims remain unverified. S1 is the next milestone; no S2 implementation is
authorized by an S0 completion label alone.
