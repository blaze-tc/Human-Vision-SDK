# Android 0.4.0-preview.3 device benchmark

This benchmark compares the isolated CPU, XNNPACK and NNAPI execution providers.
It is a user-run physical-device acceptance check; desktop and Android cross-builds
cannot establish Snapdragon 888 frame rate, latency, thermal behavior or accuracy.

## Fixed test conditions

Use the same OnePlus 9 Pro (Snapdragon 888 / SM8350), camera, orientation, person
positions, lighting and 30-second measurement interval for every run. Keep hands
disabled. Let the device cool to the same approximate temperature before each run.
A run fails if the requested backend cannot initialize or `Actual backend` differs
from the requested backend.

Run only these six combinations:

| MaxBodies | Pipeline | Profile |
| ---: | --- | --- |
| 2 | TopDown | `android-cpu-nohands` |
| 2 | TopDown | `android-xnnpack-nohands` |
| 2 | TopDown | `android-nnapi-nohands` |
| 4 | RTMO | `android-cpu-nohands` |
| 4 | RTMO | `android-xnnpack-nohands` |
| 4 | RTMO | `android-nnapi-nohands` |

## Results to record

Copy these HUD values near the end of each 30-second run:

| Pipeline | Profile | Raw body FPS | Result age ms | Body pre/infer/post ms | Requested / actual backend | Dropped input / body frames | Sampled bodies | Flicker during 30 s |
| --- | --- | ---: | ---: | --- | --- | --- | ---: | --- |
| TopDown | CPU | pending | pending | pending | pending | pending | pending | pending |
| TopDown | XNNPACK | pending | pending | pending | pending | pending | pending | pending |
| TopDown | NNAPI | pending | pending | pending | pending | pending | pending | pending |
| RTMO | CPU | pending | pending | pending | pending | pending | pending | pending |
| RTMO | XNNPACK | pending | pending | pending | pending | pending | pending | pending |
| RTMO | NNAPI | pending | pending | pending | pending | pending | pending | pending |

For TopDown also record detector inference milliseconds, detector FPS, pose total
milliseconds and pose person count. For RTMO record raw/accepted detections and
maximum detection score. Record the profile and pipeline labels from the HUD so a
misconfigured run is visible in the evidence.

## Decision rules

- Select the fastest successful backend separately for TopDown and RTMO.
- If XNNPACK is fastest, do not force NNAPI as the Android default.
- If NNAPI is fastest, keep NNAPI.
- If the best RTMO run remains below 15 raw body FPS or above 180 ms result age,
  move the next version directly to the documented QNN HTP work.
- If raw body FPS reaches 15 or more while result age remains above 180 ms, inspect
  camera readback and latest-frame transport next.
- Optimize hands only after the body baseline is selected.

The adaptive render hold may prevent sampled output from disappearing between slow
observations. It does not raise raw body FPS and must not be reported as inference
performance.
