# Symptom-first debugging

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
[RKNN offline/device tools](../../tools/models/rknn/README.md) and the
[verified offline report](../reports/2026-10-09-rknn-offline-validation.md).
The fixed non-quantized model passed three PC simulator controls; INT8 was rejected.
The standalone device probe records input delivery, run call, output wait, release,
runtime/driver and raw-output errors with no CPU fallback. Repeated static model runs
are not fresh SDK skeleton FPS. OnePlus SM8350 cannot validate the Rockchip route;
physical RK3588 driver/model timing remains the next gate before mode integration.
