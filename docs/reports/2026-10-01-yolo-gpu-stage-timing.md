# Seven-person GPU stage investigation

The user reports that the eligible640x384 FP32 route basically follows actions
and requests higher FPS. The last integrated capture measured15.32 fresh
observations/s,15.20 count7 observations/s, and observed age P50/P95
114.19/134.67ms. Count7 is not a joint-correctness or independent-person oracle.
The >=25 complete-observation FPS gate remains unmet; target30 is unchanged.

## Rejected submission fusion

The first candidate combined first-role preprocessing and extraction before
the outer producer semaphore wait. Independent review found a blocking issue:
pinned ncnn `src/net.cpp:253-297` may submit and reset that command internally
during extraction. The cached actual Adreno660 rough score9 selects the32768
dispatch threshold. Vulkan support for every layer does not exclude this path;
the exact640x384 triggering layer was not measured.

The candidate was never installed. Two new regression cases reproduced the
producer-wait bypass. All behavior changes were reverted, retaining only
default-disabled sampled timing. The normal OFF native SHA is exactly the
accepted baseline:
`f759368c26cf9cc621a00113a50d9de439b259b74f2c045cecda91620ba479f0`.
No custom ncnn synchronization change, model precision change or FPS gain is
claimed. Existing R4 edits and their fixture remain preserved.

## Verification and instrumentation

The actual production Run/release bodies are compiled against GPU boundary
doubles:17 new producer-wait cases and8 preserved crop cases pass. The isolated
HEAD-only source patch also passes17 cases. These tests verify host boundary
behavior, not real Vulkan driver execution.151 focused native tests and the
Android API26/ARM64/1813 strong-import closure pass.

Timing is guarded by existing `HV_ANDROID_TOPDOWN_EVAL_TRACE`, sampled for the
first3 and every64 successful raw calls. Each record includes frame_id and
elapsed_us. It preserves producer semaphore consumption before extraction,
cached imports, external ownership release, quarantine and slot retirement.

- `import_preprocess_record`: first import/preprocess/packing recording.
- `preprocess_submit_wait`: first outer producer/preprocessing wait.
- `extract_download_elapsed`: extraction and download recording, including
  possible ncnn internal GPU flushes/waits; this is not pure CPU time.
- `inference_submit_wait`: outer inference/download wait.
- `dense_output_copy`: copying compact inference outputs, never camera input.
- `ownership_release_wait`: recording, submitting and completing AHB release.

The separate API26 trace build passes and has SHA:
`f82c67c8e771ab264dcf891ce44b8b540028dbb04ea524797b2ce5c79bb09989`.
Model,640x384 input contract, frozen numerical limits and public skeleton API
remain unchanged. No main merge, Release or physical acceptance is claimed.

## Integrated measurement

The authorized Unity2021.3.45f1 project was backed up and staged, refreshed with
zero errors, built through `HumanVision/Evaluation/Build Continuous Video Diagnostic`,
and restored its editor settings. APK runtime/model/native/video and installed
APK hashes matched the reviewed stage. APK SHA:
`5d185e33e655484a2e72f0184f4e4f4cfb20cbe8c205ef8903b67c597c1182a2`.
Commit `c4d54869a8135b3729bb55d9cfc872262a95e193` records this diagnostic task;
the built native includes preserved uncommitted R4 work as before.

```powershell
pwsh -NoProfile -File tools/test/collect_android_r4_video.ps1 -BuildDirectory out/android-yolo/eval-rectangle640-20261001-m3-safe-timing -RunLabel yolo640-stage-timing-75s -DurationSeconds 75
py -3 out/android-yolo/unity-preflight/analyze_observations.py out/android-yolo/eval-rectangle640-20261001-m3-safe-timing/device-yolo640-stage-timing-75s
py -3 out/android-yolo/unity-preflight/analyze_gpu_phases.py out/android-yolo/eval-rectangle640-20261001-m3-safe-timing/device-yolo640-stage-timing-75s
```

The warmed82.319s observation window includes collector overhead.1208 distinct
results give14.67 fresh FPS;1201 count7 results give14.59 FPS, with no partial or
empty observations and7 count8 observations. Observed age P50/P95 is
116.07/135.81ms. GPU-worker errors, copy/import errors and full-frame input CPU
readbacks are zero. Screen60 shows seven visible skeletons; this single image
does not establish temporal accuracy. The diagnostic is not a speed improvement
over the normal15.32 FPS baseline.
The fixed25FPS source cannot certify30 fresh observation frames/s.

19 warmed complete sparse phase samples give the following elapsed wall times:

| Phase | Mean ms | P50 ms | P95 ms |
| --- | ---: | ---: | ---: |
| Import/preprocess recording | 0.063 | 0.062 | 0.090 |
| Producer/preprocessing submit/wait | 2.764 | 2.340 | 4.550 |
| Extraction/download, including internal waits | 56.019 | 56.149 | 61.926 |
| Outer inference/download submit/wait | 4.835 | 4.747 | 5.700 |
| Compact output copy | 0.642 | 0.527 | 1.569 |
| External ownership release/wait | 0.895 | 0.722 | 1.734 |
| Sum of recorded phases | 65.217 | 66.734 | 70.434 |

Extraction dominates about86% of the recorded phase sum. These timings do not
separate CPU recording from internal ncnn GPU waits; they do show that saving
only the outer preprocessing wait cannot account for the improvement required
to reach25-30 FPS. The next bounded candidate is the official FP32 ncnn SGEMM
convolution route with Winograd disabled, unchanged model/geometry/precision.
It requires a separate frozen numerical/semantic gate before SDK integration;
speed must then be measured in this complete Unity GPU pipeline. No candidate
eligibility or speed result is claimed yet.
