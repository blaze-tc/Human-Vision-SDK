# Formal Android Demo Camera observation — 2026-10-04

The first Camera run remains **INVALID**. The second fixed-settings run is
**VALID at the manager observation boundary**, with **673 / 40 = 16.825 FPS**;
native coverage remains **INCONCLUSIVE** and the **30 FPS target is FAIL**.
This report documents the recorded baseline and independent facts review. It
does not complete Task11 or physical accuracy, hand, motion or thermal acceptance.

Evidence root: `out/input/formal-android-oct04-v15-observer/`.
The [independent review](../../.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/camera-facts-review/report.md)
and [recomputed facts](../../.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/camera-facts-review/probes/recomputed.json)
verify raw counts, sequences, source joins, percentiles, stage timings and APK
contents. The review's bounded Spec PASS / Quality PASS applies to the manager
measurement and artifact identity, not performance or Task11 completion.

## Fixed-settings run

Read [manifest](../../out/input/formal-android-oct04-v15-observer/camera-fixed/manifest.json),
[analysis](../../out/input/formal-android-oct04-v15-observer/camera-fixed/analysis.json)
and [raw records](../../out/input/formal-android-oct04-v15-observer/camera-fixed/observation.jsonl).
Run ID is `3dc199562de043b59d5dd8286c452f0c`; header capacity is MaxBodies 4,
GPU/runtime enabled. The 60.033027-second recording contains 978 observations,
1,779 sources and 1,805 counter samples. Overflow, interruption and copy-invalid
flags are false. All raw observations, including empty or partial results, are
retained; body counts are never summed into FPS.

The fixed half-open window is `10,000,000 <= elapsed_us < 50,000,000`
(10-second warmup, 40-second measurement). No stable sub-window was selected.

| Metric | Recorded result |
| --- | --- |
| Complete manager observations / delivery FPS | 673 / **16.825** |
| Source publications / publication FPS | 1,181 / **29.525** |
| Body-count histogram | 1:36, 2:622, 3:15; empty:0 |
| Local publication-to-receipt age P50 / P95 | **99.374 / 100.7746 ms** |
| Local source-observation-to-receipt age P50 / P95 | 99.487 / 100.8766 ms |
| Mapped manager source age P50 / P95 | 99.374 / 100.7747 ms |
| Maximum adjacent clock interval bound | 147 us; below the 5 ms analyzer limit |
| Public runtime drops / bridge drops / CPU readbacks | 0 / 0 / 0 |
| Public readback-error delta | 0 |
| Manager validity / native coverage / 30 FPS target | VALID / INCONCLUSIVE / FAIL |

Full-run result sequences 1..978 and source frame IDs 2..1780 are continuous;
window result sequences are 129..801. There are no regressing result frames,
missing source joins or non-streaming/error samples. Source metadata has one
epoch: source 2, generation 1, token 1, actual 1280x720, applied rotation 0,
mirror true, InputMonotonic clock domain 1 / ID 0. All 978 observations record
region assignments available and revision 1.

Post-run [shared settings](../../out/input/formal-android-oct04-v15-observer/camera-fixed/shared-after.json)
show MaxBodies 4, UseRegions true and four quarter-width regions. The raw schema
does not store the full region rectangles, so this snapshot does not establish
the entire window's rectangle configuration. Capacity 4 and mostly two detected
bodies describe a mixed-body workload; this is not single-person performance.

Reported stage P50/P95 values are detection 0/0 ms, pose 57.48379898/61.59811249 ms,
tracking 1.78140604/3.41526055 ms and total 59.45442963/64.03543854 ms. They are
public reported timings, not an independent GPU trace or causal diagnosis.
Public `inference_fps` is 0 throughout and is excluded from measured delivery FPS.
All 1,917 full-run bodies have stored 17/32/6 joint arrays and zero prediction
ages. Complete storage does not prove joint validity or anatomical accuracy;
the frozen profile disables hands, so six hand slots do not establish real hands.

## First run remains invalid

The original [Camera records](../../out/input/formal-android-oct04-v15-observer/camera/observation.jsonl)
and [analysis](../../out/input/formal-android-oct04-v15-observer/camera/analysis.json)
remain intact. Run `2844466dfdf641f98b039d2bee1b9441` used MaxBodies 8 and lasted
60.033575 seconds, with 944 observations, 1,689 sources and 1,798 samples.
Three geometry epochs, four region revisions, four result sequence gaps, two
regressing frame-0 results without source joins and 20 adapter-geometry error
samples invalidate source continuity and complete-result coverage.

The same 10..50-second window contains 630 raw rows (15.75 rows/s); eligible
retained manager delivery is 15.7/s. Neither is valid complete manager FPS.
The histogram is 0:60, 1:59, 2:351, 3:127, 4:33. Manager/native validity and the
30 FPS target are INVALID. Removing orientation/settings records or relabeling
frame 0 cannot recover acceptance. The lifecycle explanation remains a hypothesis
because deployed-native source correspondence is not fully established.

## Exact artifact identity and Camera provenance

Both host `player/HumanVisionFormal.apk` and pulled `installed-base.apk` are
82,971,249 bytes. APK inspection verifies all 11 runtime-index file hashes and
all 14 runtime/native entries are unchanged from the v14 APK. The observer resolves
SDK `user-packages-v15` and Input `user-packages-v13`; an unused Input v15 folder
and dirty working sources are not the measured Input payload.

| Artifact | SHA256 |
| --- | --- |
| Host and installed APK | `36b6b189e70f608d767a7f891bcd1aa0a3209e0aae14d873efb399552997a0b9` |
| `lib/arm64-v8a/libhumanvision.so` | `fef5fda3952d21e68b40052cc8804adcd99bc9d961429daf868afd3650bbbd96` |
| `lib/arm64-v8a/libhumanvision_input.so` | `d3968a4f12d1cf7882c04312c7f0ad095c4a18b894846e2beafb758e9f582628` |
| `assets/HumanVision/Runtime/index.json` | `89703ad345f2b7a954b6711f8e65ffbd02e5f80cbf1c653d6815a53395ee3cfc` |
| SDK asset index | `5be394acf0e207ccb8b2c0d3ae1d6888804501cfd456982f074b89f3db5f915f` |
| Recorder source | `9f3a71c20a920a35497b6e32171bd2913d11c5b3e5be4b42fb812d4d1a36d33d` |
| Android profile | `5cd72fca9ea22a276bf94bb279777ea687488eee89a5308f432ee48edc68185e` |
| YOLO modelpack metadata | `7fd006c3f54edaadad79d3afe911096b0a2b8d77a81171aa8a8a29aae59df337` |
| YOLO NCNN param | `908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905` |
| YOLO NCNN bin | `6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9` |
| First-run raw records | `e93c08a904bccc124f5742e08c386018cb9571308d31a1e96a6f2579d9a33405` |
| Fixed-run raw records | `afbf02c21916aee44ce89dd0a3f5ac83f8206dc704188a578db36fdecfe28663` |
| First-run Camera identity dump | `cce93c7e25fd3b4cfb65366f30f117891fdd98851eafcb4784fe35a0ffe47a7e` |
| Fixed-run `camera-current.txt` | `85d4993ed1e378b1ae849984a79d4645b8c26a7a7ad23785e89a04a52c046079` |
| First-run source configuration | `ad715636e9c5fb0287e5fc7bb4a7bd2c46a2bd4eedde5d1bec46464b385a6889` |
| Fixed-run `WebCamera-after.json` | `3218ab27ce51e9da41a35669550aa57d2f3f9654570dbc7ab534c9c8fd0131f2` |

The frozen local FP32 profile `android-ncnn-vulkan` binds `pipeline.yolo.pose`,
`yolov8n-pose-rectangle640x384-fp32-local` and `backend.ncnn.vulkan`, with fallback
false and hands disabled. Both measured manifests declare commit
`ff2a1fb5f487f972ca89dcbe116843fdc0f02c2f`; that declaration does not reconstruct
the already-built native library's complete build-source manifest. No new
native, model, profile or production-source fix is part of this report.

[camera-current.txt](../../out/input/formal-android-oct04-v15-observer/camera-fixed/camera-current.txt)
records active Camera ID 1 for the official Demo, PID 18157 matching `pid.txt`;
HAL static metadata labels ID 1 Front. The earlier `camera-connected.txt` has
no active client and is not the qualifying identity snapshot. HAL physical
orientation 270 differs from applied rotation 0 recorded by the source.
[WebCamera-after.json](../../out/input/formal-android-oct04-v15-observer/camera-fixed/WebCamera-after.json)
requests Camera 1, 1280x720 at 30 FPS with mirror true. It is post-run; no second-run
pre-run configuration snapshot exists. Requested FPS is not a measured source
rate. Manifest `source_rate_hz` is null and sensor capture time is unverified;
`source_cannot_establish_30_fps=false` only reflects the absence of a numeric cap.

## Limits and reproducible verification

[live.png](../../out/input/formal-android-oct04-v15-observer/camera-fixed/live.png)
was captured roughly 184 seconds after control-start, outside the recording.
[current-2.png](../../out/input/formal-android-oct04-v15-observer/camera-fixed/current-2.png)
shows the user with raised arms, skeleton/region overlays and Ready 1/4 after a
later drawing-style change. These static images do not prove continuous motion,
historical window timestamps, all-body tracking, left/right semantics or the
earlier line-3/point-9 style.

Local ages are Unity/Input publication or observation to receipt, not sensor
capture-to-display latency. Public `ProcessedFrames` is BodySequence, not native
V2 FreshBodyFrames. Native fresh/GPU/decoder counters remain unavailable/null;
zero public deltas do not establish native counters or whole-console cleanliness.
Observer overhead, sustained thermal performance, 1–8-person physical accuracy,
real hand acceptance and genuine RTSP qualification remain open.

Run these commands from the worktree root; analyzer outputs go to temporary
files, leaving the recorded analyses unchanged. The first exit code is 2
(INVALID), the second 0 (manager VALID, target FAIL). The probe exits 0 and checks
independent arithmetic plus saved-analysis and identity closure.

```powershell
$cameraPython = 'C:/Users/Tancheng/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $cameraPython -B tools/test/formal_demo_observation_analysis.py --manifest out/input/formal-android-oct04-v15-observer/camera/manifest.json --records out/input/formal-android-oct04-v15-observer/camera/observation.jsonl --output "$env:TEMP/formal-camera-analysis.json"
& $cameraPython -B tools/test/formal_demo_observation_analysis.py --manifest out/input/formal-android-oct04-v15-observer/camera-fixed/manifest.json --records out/input/formal-android-oct04-v15-observer/camera-fixed/observation.jsonl --output "$env:TEMP/formal-camera-fixed-analysis.json"
& $cameraPython -B .superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/camera-facts-review/probes/recompute.py
```

On this documentation pass the same probe was executed with its final output
write replaced in memory by a JSON-normalized equality assertion against the
existing `probes/recomputed.json`. It passed both raw hashes, both saved analyses,
fixed-run coverage/ages/stages, APK/native/index/model closure and all 14 unchanged
v14 entries. Only this report was created; original captures and review outputs
were preserved.
