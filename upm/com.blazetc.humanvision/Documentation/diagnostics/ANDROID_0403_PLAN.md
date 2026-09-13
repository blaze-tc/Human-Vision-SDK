# Android 0.4.0-preview.3 diagnostic plan

## Frozen device observations

The user measured the current `v0.4.0-preview.2` package on a OnePlus 9 Pro
(Snapdragon 888 / SM8350, Android 14):

| Pipeline | Observed result |
| --- | --- |
| TopDown, 1-2 people | Raw body 9.7-15.5 FPS; body NNAPI 34-55 ms; detector NNAPI 59-84 ms |
| RTMO, configured for 4 people | Raw body 5.8 or 3.9 FPS; NNAPI 170 ms or 280-281 ms; result age 299 or 397 ms |
| Hand endpoint pipeline | NNAPI 217-366 ms; 0.8-2.5 jobs/s |

The HUD could report one or two raw/tracked/visible bodies while sampled body
count periodically became zero. The current `BodyServices::Sample()` rejects a
body or joint older than 200,000 microseconds. That fixed 200 ms presentation
gate is shorter than observed RTMO result age, so presentation expiry is a
confirmed cause of the disappear/flicker symptom. It does not explain the slow
model execution and must not be used to conceal that performance problem.

## Frozen runtime baseline

- Android ONNX Runtime remains pinned to `onnxruntime-android` 1.23.0 with the
  repository SHA-256 lock.
- Static plugin registration currently installs the CPU ORT backend first, then
  body and hand pipelines. Android optionally registers the accelerated ORT
  backend after the required plugins, followed by the optional QNN backend.
- `auto` currently resolves through the profile/backend registry and may fall
  back to CPU. It remains the shipping default during this diagnostic patch.
- Raw observations are source-timestamped inference results. They are never
  extended by render-hold behavior.

## Work in this patch

1. Separate the 25 ms motion prediction horizon from an adaptive render hold and
   from body-track loss timing.
2. Add CPU, NNAPI and XNNPACK no-hands Android benchmark profiles which request
   exactly one provider each.
3. Add an explicit XNNPACK backend using four XNNPACK intra-op threads while all
   ORT sessions keep sequential execution and one inter-op thread.
4. Add bounded-rate diagnostics that distinguish raw inference performance,
   tracking, presentation hold, requested provider and actual provider.
5. Add a demo profile override that reinitializes the runtime when changed.
6. Produce Android ARM64, native, managed, architecture and package evidence for
   `v0.4.0-preview.3`; leave physical-device acceptance to the user.

## Explicitly outside this patch

- Model-family replacement, INT8/QDQ conversion and ONNX Runtime upgrade.
- MediaPipe, YOLO, MoveNet, segmentation, renderer redesign, foot models, RTSP
  redesign, GPU zero-copy and NEON optimization.
- Changes to the V1 C ABI, canonical skeleton/joint schema, Unity public gameplay
  API, plugin ABI table layouts, GUIDs or Windows DirectML behavior.
- QNN execution or packaging. QNN work is documentation for a later authorized
  QAIRT/custom-ORT build only.
- Claims that Android FPS, latency, accuracy or thermal acceptance passed without
  new measurements from the user.

## Decision rule for device results

The three forced profiles must be tested with the same camera, model selection,
body limit, scene and measurement interval. A requested provider that cannot be
created is a failed run; it must never be relabeled as that provider after CPU
fallback. QNN remains deferred if the best RTMO provider reaches at least 15 raw
body FPS and at most 180 ms result age on the target device.
