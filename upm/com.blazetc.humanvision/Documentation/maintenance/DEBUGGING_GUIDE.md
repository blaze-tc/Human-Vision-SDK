# Symptom-first debugging

## Android startup and preview

For SDK 0.4.1 / Input 0.1.1, check `PreviewError`, input state and SDK `LastError`
separately. Confirm installed native library identity against the APK and verify
actual backend in the running session. A cold-start fault requires startup logcat,
the crash buffer, any accessible tombstone and the matching ELF BuildIds.

For RK3588, compare three cold starts using the same RTSP stream, Neural/Low and
matching local resources. Run independent input with preview on, independent
input with preview off, then compatible input with preview on. The preview-off
case skips Unity's Android Vulkan live bridge. Capture source progress, first
texture where enabled, valid skeleton results and restart behavior. [Startup probe instructions](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/tools/test/NPU_STARTUP_DIAGNOSTICS.md)

The Runtime Prefab Acceptance Demo can capture a JSONL snapshot while waiting
for input, on error or during recognition. Record native decode, live publication,
Unity texture updates, completed inference and valid skeleton delivery separately.
Device sensor availability is reported with each reading. [Demo instructions](../user-guide/UNITY_RUNTIME_PREFAB.md)

## OnePlus performance follow-up

See [the measured CPU/Vulkan report](../reports/2026-10-09-oneplus-performance.md).
Android ORT CPU uses a bounded intra-op pool (up to four); accelerator pools
remain unchanged. Capture the PID-scoped HumanVisionCpu initialization line in
native logs before interpreting CPU usage. Distinguish Low512x288 from
Medium640x384 and RTSP published dimensions; compare source/generation groups
after warmup. Completion polling reads a signaled Vulkan fence without waiting,
while cache/pipeline destruction remains on render callbacks. A root-suite-only
UnitySkills test result is zero executed leaf tests, not passing evidence.

| Symptom | First evidence | Inspect / focused test | Likely cause; do not change first |
|---|---|---|---|
| Unity FPS drops | render FPS, allocations, GPU readback | Unity Demo/Runtime; managed tests | main-thread copies or per-object drawing; not model weights |
| Camera smooth, skeleton slow | raw body FPS, age, inference ms | pipeline/backend; RuntimeSession | expensive inference or fallback; not camera orientation |
| Detector slow | stage timings, actual provider | pipeline.simcc/backend; SimccPlugin | full detection cadence or unsupported EP; not overlay smoothing |
| Pose cost grows with people | inference ms, configured capacity | pipeline choice/profile; Profile | TopDown per-person cost; not renderer |
| IDs swap | raw positions, track IDs | services/body_services; CommonServices | crossings/association ambiguity; not model filenames |
| Wrong region | region revision, normalized preview coords | region_mask/BodyServices; CommonServices | orientation/ROI mismatch or old revision; not backend |
| HandTip/Thumb missing | valid wrist/elbow, hand job FPS, own timestamps | hand pipeline/HandRequests; SimccPlugin/CommonServices | crop confidence, shared budget or occlusion; never fabricate tips |
| Wrong topology | semantic IDs, valid/derived flags | pipeline mapping or renderer parents | index mapping or wrong parent; not camera decoder |
| Result age too high | source timestamp vs monotonic now, dropped frames | Host/composition; RuntimeHost | slow jobs, mixed clocks or input backlog; not relabel timestamps |
| QNN requested, CPU actual | diagnostic fallback string | backend/profile; BackendFactory | unsupported custom ORT/QAIRT/model; not claim acceleration from build |
| Android builds, phone slow | actual provider, raw FPS, thermal trace | backend/profile | build cannot prove hardware speed; not increase smoothing |
| RTSP differs from WebCam | source status, input orientation/readback | native/input and Demo/Live | reconnect/decode/clock geometry; not tracking first |

The HUD refreshes four times per second and separates render/raw body/hand job
rates. It reports raw, tracked and sampled body counts, the observation-period
EWMA, adaptive render hold, sample age/state and separate input/body drops. The
hand worker is shared; the configured limit is per hand. Sampled output predicts
for at most 25 ms, then holds the filtered body until the adaptive 300-800 ms
render window expires. Hand endpoints retain their independent 200 ms validity.
For runtime initialization inspect the index extraction/hash error first. UPM source
model metadata must have independent GUIDs from StreamingAssets copies.

Per-session diagnostics label the profile, pipeline, requested/actual backend and
last tensor inference duration. RTMO adds raw/accepted detections and maximum score;
TopDown adds detector execution cadence and pose-person cost. These are execution
timings, not proof of accelerator graph coverage.

For the actual Human-Vision-SDK-Test settings scene, the optional project diagnostics
add a public Downloads session folder, hardware samples and sparse six-stage native
wall timings. See [device performance logs](../user-guide/DEVICE_PERFORMANCE_LOGS.md)
for availability/status fields and measurement boundaries. Analyze a plain folder
with `python tools/benchmark/analyze_settings_device_log.py SESSION OUTPUT --warmup 30`.
Do not interpret missing GPU/NPU metrics as idle, GPU render-frame time as utilization,
or the difference between mismatched SDK/native frame samples as CPU overhead.

For the user-approved RK3588 acceleration experiment, start at
[RKNN offline/device tools](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/tools/models/rknn/README.md) and the
[verified offline report](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/docs/reports/2026-10-09-rknn-offline-validation.md).
The fixed non-quantized model passed three PC simulator controls; INT8 was rejected.
The standalone device probe records input delivery, run call, output wait, release,
runtime/driver and raw-output errors with no CPU fallback. Repeated static model runs
are not fresh SDK skeleton FPS. OnePlus SM8350 cannot validate the Rockchip route;
physical RK3588 driver/model timing remains the next gate before mode integration.
