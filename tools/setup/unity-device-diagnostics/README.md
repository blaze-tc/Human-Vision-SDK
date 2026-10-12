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

The combined repair adds **一键自动对照** to the persistent diagnostics bar.
It tests GPU pixel fixtures, independent RTSP preview, bundled video and RTSP
recognition in one installation. Identified RK3588 devices run NPU baseline,
qualified candidate and repeated baseline; other devices explicitly skip RKNN
and compare CPU/Vulkan. A final 300-second local-video phase checks sustained
behavior. Each phase has a 15-second warm-up; normal measurements last 60 seconds.
Cancel restores temporary environment switches and the prior runtime/draft;
saved settings are never written. Logs remain in the same copied session folder.

`persons.jsonl` records stable-ID windows, real fresh observations, complete
COCO-17 observations, source/generation and local result age. Duplicate frames,
repeated observations and predicted/derived joints cannot increase complete
counts. Known absent tracks emit zero, and 64-track retention evictions are
explicit. `benchmark.phase` markers delimit measured intervals; never include
warm-up, restoration or pixel fixtures in FPS. Existing global event FPS is
still distinct from these per-person rates. Candidate labels are requests:
inspect `input_path` and `cpuReadback.modelReadback` for the actual qualified or
fallback path. No RK3588 target FPS is certified by the OnePlus run.

`python tools/benchmark/analyze_combined_benchmark.py SESSION OUTPUT_JSON` summarizes
only completed `MEASURE BEGIN/END` intervals from `persons.jsonl`. Repeated baseline
phases remain separate; cancelled phases cannot become passing measurements.
Known absent tracks retain zero windows. Spatial accuracy requires inspection of
the recorded video; complete COCO-17 is not a full 32-joint accuracy certificate.

## Actual inference-instance counters (2026-10-11)

Visible status: 推理实例：busy / total（忙碌 / 总数）. Every `pipeline.snapshot`
contains `inferenceInstances.busy`, `total`, `fallback`, `scope`, sampled at the
statistics cadence (one second default). They count actual created body-pipeline
instances, not requested instances or NPU cores. Busy includes pipeline waits;
0/1 can be a normal idle instant. Stopped is 0/0; unavailable is -1 and is displayed
explicitly. CPU, Vulkan and RKNN share this definition. Native diagnostic keys:
`Inference instances=`, `Active inference instances=`, `Instance pool fallback=`.
Run `InferenceInstanceTests` and inspect actual device records; this is not usage.

## Single-instance following update (2026-10-11)

Production composition now uses one body instance for CPU, Vulkan and RKNN. The
experimental instance button and benchmark multi-instance phases are removed.
`presentation` logs actual input/submitted/result/display and bracketing pose
frame IDs, actual delay, and display mode. Derived display interpolation never
increases raw fresh-result or per-person counters. The source publication FPS
is measured after Unity receives a new usable texture; it is not camera-rated
FPS or a count of Unity redraws. Local video-1.mp4 is 25 FPS.

Presentation snapshot pixel checks cover four sizes and both mirror states.
Run PresentationPoseHistoryTests for identity, timeout and low-rate continuity.
See docs/reports/2026-10-11-instance-pool-following.md for device evidence and
unverified acceptance boundaries.
