# Settings Demo project diagnostics

These are the actual helper sources used and tested in
`E:\UnityProject\Human-Vision-SDK-Test`, Unity 2021.3.45f1. They are optional project
components, outside the stable SDK public API/UPM package. They observe runtime
results and add log/hardware UI; they do not replace game input lifecycle.

To reproduce in an SDK/Input project with the generated Settings Demo:

1. Copy `Runtime` and `Tests` into `Assets/HumanVisionSettingsDemo/Diagnostics/`;
   copy the Editor script into `Assets/Editor/`. Let Unity compile.
2. Use **Tools → Human Vision → Development → Install Settings Demo device
   diagnostics**. It edits the existing scene and prefab through Editor APIs,
   binds the diagnostics and preserves the original action buttons.
3. Enter Play Mode or build the single Settings scene. Copy the session folder
   using the permanent button. Android 10+ mirrors ordinary log files into
   Downloads/HumanVisionLogs; private originals remain.

The build helper checks the actual test project's embedded native identity when
`HumanVisionDeviceBuildInfo.json` is present. Do not copy an identity from a
different native build. `native_stage_trace=true` only enables capture; it cannot
make an uninstrumented native library produce stage records. The 2026-10-09 local
diagnostic native is built with `HV_ANDROID_TOPDOWN_EVAL_TRACE=ON` and
`HV_ANDROID_NCNN_EXECUTION_TRACE=OFF`; original qualified FP32 models are retained.
If no stage instrumentation is installed, SDK aggregate statistics and hardware
logs still work, with native stages explicitly unavailable.

Run EditMode classes `HumanVision.TestProject.Tests.HardwareAndStageTests` and
`HumanVision.TestProject.Tests.DeviceDiagnosticSessionTests`, and
`HumanVision.TestProject.Tests.Rk3588DiagnosticsTests`. The real-video probe
checks genuine results, Unity/background logs, redaction, UGUI clipboard and ZIP
contents. Physical Android validation also checks the public folder byte prefixes,
native timings, hardware validity and absence of JNI warning floods.

Use [performance log guide](../../../docs/user-guide/DEVICE_PERFORMANCE_LOGS.md) and
`python tools/benchmark/analyze_settings_device_log.py SESSION OUTPUT --warmup 30`
from the repository root. The analyzer accepts Android's appended `.txt` filenames.
Results count fresh notifications containing bodies, not per-person complete
skeleton FPS. Native stages include synchronization and are separate from Unity
render frame timings. Missing telemetry is unavailable, never fabricated zero.

The RK3588 follow-up discovers Mali devfreq clocks/load and named CPU/GPU thermal
zones. It preserves raw native details once, indexes batches in events and measures
flush wall time. Input frame-log sampling is a separate native Input change; copying
these C# helpers alone does not enable it. The helper observes the private RKNN route; it does not itself implement a backend.
Actual NPU activation follows the successful runtime profile, with per-core whole
**device** load, clock and named thermal zone. Run `RknpuHardwareTests` as well.
CPU-delivered timing snapshots do not reuse old Vulkan samples. `cpuReadback` in
pipeline.snapshot/timings includes actual pixel dimensions, bounded pending count,
completion/submission/rejection counters and GPU-request-to-main-thread-callback
wall time, normalize/submit/Blit CPU recording means. These cumulative values and
lastFrameId are separate from the current model result; do not subtract across
frames to invent precise pipeline gaps.
