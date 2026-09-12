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

The HUD separates render/raw body/hand job rates. The hand worker is shared; the configured limit is per hand;
sampled output is interpolation/prediction and not additional observed skeletons.
Raw data can be retained with timestamps; sampled points expire after200ms.
For runtime initialization inspect the index extraction/hash error first. UPM source
model metadata must have independent GUIDs from StreamingAssets copies.

Per-session diagnostics label the model asset, requested/actual backend and last
tensor inference duration, so detector/body/hand costs can be distinguished. These
are execution timings, not proof of accelerator graph coverage.
