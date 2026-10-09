# 2026-10-09 Settings Demo device telemetry and video measurements

The requested folder copy, hardware UI/logging and detailed stage costs are
implemented in the actual `Human-Vision-SDK-Test/HumanVisionSettingsDemo`, and a
fresh diagnostic APK is installed on the connected OnePlus 9 Pro LE2120 / Android
14 / Adreno 660. The qualified Low profile gives approximately 23.7 fresh
notifications containing bodies/s on the bundled 25 FPS video. This approaches
the requested 20–25 FPS range; it does not guarantee every one-second window or
complete 32-joint/per-person output. No RK3588 is connected to this host.

## User delivery

- APK: `E:\UnityProject\Human-Vision-SDK-Test\Builds\HumanVisionSettingsDemo-HardwareDiagnostics.apk`.
- Choose **Video → video-1.mp4 → Low (512×288) → Apply** in **HumanVisionSettingsDemo**.
- **Copy log folder** returns `/storage/emulated/0/Download/HumanVisionLogs/<session>/`
  on Android 10+. The folder is automatically mirrored about every five seconds,
  including Unity, skeleton, performance, hardware, six-stage native and SDK logs.
- Local full evidence: `E:\UnityProject\Human-Vision-SDK-Test\DiagnosticsVerification\Performance-20261009`.
  Final session: `hardware-low-final-300s/grade-2/{session,public-session,analysis.json,public-analysis.json,screen.png}`.
- Final phone session: `session-20261009-014621-254-326b23cd`.
- Original phone preferences were restored byte-for-byte; the new APK remains
  installed and the measured app was force-stopped after capture. Original Fix2
  and older per-layer trace APKs are preserved as separate files.

## Controlled video evidence

Same phone, same genuine `StreamingAssets/video-1.mp4`, source 1024×576/25 FPS,
MaxBodies4, regions disabled. Normal Fix2 baseline uses the original uninstrumented
core; final diagnostics enable only sparse native stage logs, not GPU per-layer
queries. No model bytes, precision, synchronization semantics or SDK ABI were
changed. Low is a resolution/accuracy tradeoff, not a new faster Medium kernel.

| Run | Source FPS | Completed FPS | Fresh notifications containing bodies | Timing |
|---|---:|---:|---:|---|
| Normal Fix2 Medium, 120s | 24.96 | 15.93 | older logger has no separate body-notification counter | Mean total62.39ms |
| Normal Fix2 Low, 120s | 24.97 | 23.09 | older logger has no separate body-notification counter | Mean total41.78ms |
| Final Low diagnostics, 300s, exclude first30s | 25.00 | 23.87 | **23.70/s** | Mean total41.06ms |

The final warmed interval is273.507s, with271 statistics samples and270 windows
whose current SDK body count is positive. Body-notification windows: minimum19.693,
P05=22.629, median23.671, P95=25.580, maximum25.763; **97.407% reach20/s**. Brief
bursts may exceed the nominal input rate due to pending result completion/window
boundaries; cumulative source rate remains25.00/s. Do not impose an artificial
display clamp or call these render FPS. Pipeline source/adapter/bridge/manager/SDK
error sets are empty. Five minutes does not establish indefinite sustained speed.

The first diagnostic candidate also ran300s and measured23.599 completed/s,
23.441 body notifications/s. Its public-mirror JNI `byte[]` marshalling generated
obsolete-type warnings. The final uses a reused `sbyte[]` buffer and removes that
warning flood: raw own-process logcat and `unity-0.log` confirm zero such warnings.
These are separate runs; small differences do not establish a precisely measured
speed gain from removing warnings. The final has no observed material slowdown
relative to the normal Low baseline under the tested conditions.

## Where time is spent

102 distinct complete warmed native samples; all six serial intervals have the
same native frame ID. Reported means below belong to that set. These sparse frames
can differ from the current SDK aggregate snapshot; do not subtract across them.

| Native interval | Mean ms | Share of same-sample backend sum |
|---|---:|---:|
| Image import + preprocess command recording | 0.068 | 0.17% |
| Producer synchronization + preprocess submit/wait | 1.850 | 4.59% |
| **Model Extractor + internal GPU submissions/waits + download recording** | **33.222** | **82.36%** |
| Subsequent inference/output submit/wait | 3.685 | 9.13% |
| Completed dense output copy | 0.526 | 1.30% |
| Ownership release/synchronization | 0.988 | 2.45% |
| Sum of the six intervals | 40.339 | 100% |

The SDK current-result total averages41.060ms; backend alias `PoseMs` averages
39.036ms. YOLO Runtime Host postprocess includes output decode and GPU-role
completion. `timings-*.jsonl` retains that SDK postprocess value separately as
`sdkTrackingMs`, matching the compatibility API mapping. Extractor wall time
is neither pure CPU nor pure GPU time. Unity GPU render frame time does not
include or replace all NCNN GPU work and is not utilization.

Priority follows the measured cost: model/backend execution and its internal
waits first; producer/output synchronization next if a separate device trace
confirms a large cost. Dense output copy/import command recording offer much
less headroom here. Mandatory GPU waits remain intact. The frozen FP16 input
gate is still failed; no unqualified FP16 or graph variant was enabled.
RTSP network/decode/exposure are not individually timed by these six intervals;
publication FPS, queue depth and local result age identify input-side limits.

## Hardware observations

133 warmed hardware samples, about2s apart, from a JNI-attached worker:

| Metric | Observed |
|---|---|
| App CPU, denominator8 logical cores | Mean12.313%, P9512.841% |
| App CPU, one core =100% | Mean98.501%; aggregate of app threads, not proof of main-thread headroom |
| Device GPU busy/total, Adreno KGSL | Mean75.864%, P9578.899%; includes other device GPU activity |
| App PSS | Mean333.563MB, range330.25–336.82MB |
| System available memory | Mean4993.88MB |
| Battery temperature | 37.3–41.1°C; not CPU junction temperature |
| Android thermal status | 0 then3; first3 at01:51:06.190Z / elapsed284.933s |
| System CPU | Unavailable: app lacks permission to read `/proc/stat` |
| GPU clock / device NPU load | Unavailable; `-1`, not fabricated idle. Current NCNN Vulkan profile does not use NPU. |

Android defines [thermal state3 as severe throttling](https://developer.android.com/reference/android/os/PowerManager#THERMAL_STATUS_SEVERE).
This real transition is a reason to retain heat/frequency evidence in longer
runs. It does not prove which subsystem throttled or that it caused every FPS
dip; GPU frequency was unavailable in this app. CPU all-core12% is not a
guarantee that one thread is unsaturated. No CPU/GPU/NPU metric is inferred from
model ms, memory capacity or rendering FPS.

## Verification and source preservation

- Hardware/stage tests: genuine RED13/13 failed for missing implementation ->
  final GREEN13/13. Session extension RED11/12 -> final GREEN12/12.
- Layout acceptance initially detected hardware overlap with original bottom
  action buttons. Final Editor API install, scene/prefab bindings and actual
  phone screenshot verify separate preview/status/log/hardware/action areas.
  Log folder text, copy button and four hardware/stage lines are visible.
- Actual Editor video probe: genuine sequence279/users2, main/warning/background
  Unity logs, redaction, skeletons, CSV, clipboard and ZIP PASS; ZIP contains
  hardware/timings/current-result scope fields. Final editor errors0.
- Final Android IL2CPP/ARM64/Vulkan build: Succeeded0errors8warnings,
  duration00:02:06.8545741. APK272366077bytes,
  SHA256 `73636c1ef885c1e5b573b5a74ae9b1bde42271790275bde7af8faea0e82c4cd4`.
- Stage native SHA256 `eaf4b1297b498605a95e7ff7625ca8debd360f239b35832775549fff287f05f1`.
  API26 closure506 strong imports; all5081 dynamic export names unchanged,
  including original48 C/Unity entry points (46 HV + UnityPluginLoad/Unload).
  Earlier379/379 native suite belongs to Fix1; it is not misrepresented as a
  fresh run for the instrumentation flags.
- All39 SDK StreamingAssets entries, including10 model files and profiles,
  match normal Fix2 byte-for-byte; eight other packaged native libraries
  unchanged. All187 embedded SDK hash-index entries verified against files.
- Public plain folder has every private-session file plus current SDK logs.
  Every public file is a byte-exact prefix of its private original; native,
  hardware, metadata, both skeleton rotation files and Unity log are exact.
  Three still-changing streams omit the last idle snapshot within refresh delay.
  JSON parses, CSV parses and public/private analyzers give identical active-run
  metrics. MediaStore appends `.txt` to text logs; analyzer accepts both names.
- Optional tested source snapshot: `tools/setup/unity-device-diagnostics/`.
  Folder analyzer: `tools/benchmark/analyze_settings_device_log.py`.
  Guide: [DEVICE_PERFORMANCE_LOGS](../user-guide/DEVICE_PERFORMANCE_LOGS.md).

Native reproduction: build target `humanvision` in
`build/android-hardware-stages`, Release ARM64 API26, qualified static NCNN root
`out/rk3588-stride-20261008/deps/ncnn/install`, `HV_ANDROID_TOPDOWN_EVAL_TRACE=ON`,
`HV_ANDROID_NCNN_EXECUTION_TRACE=OFF`. Configuration/build logs are
`out/{configure,build}-hardware-stages.log`; audit receipt
`out/hardware-stages-native-audit.txt`. Dependency closure uses the existing
read-only ORT/FFmpeg inputs in `.worktrees/android-ncnn-vulkan/out/live-deps`;
default audit paths in this worktree are absent, so the audit module's dependency
root was explicitly set to that real dependency directory. No gate was bypassed.

SDK/UPM public API, native processing policy and production package versions are
unchanged. This is a local diagnostic/test-project delivery, not a new production
Release. RK3588 fresh six-stage logs, sustained device performance and genuine
complete hand/per-person30FPS acceptance remain open.
