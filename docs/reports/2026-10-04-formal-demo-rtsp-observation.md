# Formal Android Demo RTSP observation — 2026-10-04

The controlled RTSP run is **VALID at the manager observation boundary**:
**591 / 40 = 14.775 FPS**. Native coverage remains **INCONCLUSIVE**, and the
**30 FPS target is FAIL**. This is genuine H.264/TCP RTSP input through Android
MediaCodec/AHB/Vulkan, with a 25 FPS file publisher. It does not complete Task11,
sensor latency, physical semantics, motion or sustained performance acceptance.
Independent facts/quality review passed within this manager-measurement boundary;
see the [review and independent probe](../../.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/rtsp-facts-review/review.md).

Evidence root: `out/input/formal-android-oct04-v15-observer/rtsp-device-1939/`.
Read the [manifest](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/manifest.json),
[analysis](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/analysis.json)
and [raw records](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/observation.jsonl).

## Fixed window and measurement boundary

Run `fb135c521f3a45db9d969f6141540a5a` records MaxBodies 8, GPU/runtime enabled,
60.028703 seconds, 854 observations, 1,085 sources and 1,803 counter samples.
Overflow, interruption and copy-invalid flags are false. The fixed half-open
window is `10,000,000 <= elapsed_us < 50,000,000`: 10 seconds of warmup followed
by 40 seconds of measurement. Empty and partial-body results remain included;
body counts are never summed into FPS and no stable sub-window was selected.

| Metric | Recorded result |
| --- | --- |
| Complete manager observations / delivery FPS | 591 / **14.775** |
| Source publications / publication FPS | 723 / **18.075** |
| Body-count histogram | 0:71, 1:169, 7:350, 8:1 |
| Local publication-to-receipt age P50 / P95 | **99.899 / 133.676 ms** |
| Mapped manager source age P50 / P95 | 99.8985 / 133.675 ms |
| Maximum adjacent clock interval bound | 112 us |
| Public runtime drops / bridge drops / CPU readbacks | 0 / 0 / 0 |
| Manager validity / native coverage / 30 FPS target | VALID / INCONCLUSIVE / FAIL |

Full-run result sequences are 1..854 and source frame IDs are 2..1086. Source
metadata records source 3, generation 2, actual 1024x576, rotation 0 and mirror
false. Source clock domain 2 / ID `2783700026096848` is SourceLocalMonotonic
decode time; stream PTS is separate. The analyzer leaves source-observation age
null because this clock is not mapped to Unity/Input time. Publication age is
local Input publication to receipt, not sensor capture-to-display latency.

[Shared test settings](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/shared-test.json)
declare capacity 8 and regions disabled; the workload contains empty, single and
multi-body frames. The frozen `android-ncnn-vulkan` profile uses the local FP32
`yolov8n-pose-rectangle640x384-fp32-local` pack. Complete manager storage does not
prove anatomical joint validity, real hands or native FreshBodyFrames coverage.
Public ProcessedFrames is BodySequence. Native fresh/decoder/GPU-copy/GPU-import
and native readback counters remain unavailable/null; zero public deltas cannot
establish those native counters or whole-console cleanliness.

## Actual RTSP provenance and unchanged payload

[Host readiness](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/ready.json)
and [host probe](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/host-probe.json)
identify the original `E:/Project/Human Vision SDK/video-1.mp4`, H.264, 1024x576,
25/1 FPS. MediaMTX v1.12.3 serves `rtsp://127.0.0.1:64057/fixture`, using the
[TCP-only configuration](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/mediamtx.yml)
and an owned adb reverse. [Server logs](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/server-stdout.log)
record H.264 publishing and TCP reading. This controlled localhost stream does
not establish remote IPC network behavior. Publisher seek 37 seconds applies
at host publisher startup, not phone acquisition; later playback can contain
one person rather than seven.

[Startup logcat](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/startup-logcat.txt)
records `hardware_codec=c2.qti.avc.decoder`, `protocol=RTSP_TCP`, H.264, actual
1024x576 AHB external-format 506, logical YCbCr/sync-fd/AHB support, GPU color
submission/completion and Unity target publication with CPU image readbacks 0.
It also records 89 compressed startup packets dropped while waiting for a
keyframe. This startup count is not a measured-window decoder-drop counter.
The decoder's reported frame-rate 30 and the requested 1280x720/30 in
[RTSP settings](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/Rtsp-test.json)
do not override the host's 25 FPS source or measured 18.075 publication FPS.

| Artifact | SHA256 |
| --- | --- |
| Host and pulled installed APK, each 82,971,249 bytes | `36b6b189e70f608d767a7f891bcd1aa0a3209e0aae14d873efb399552997a0b9` |
| `lib/arm64-v8a/libhumanvision.so` | `fef5fda3952d21e68b40052cc8804adcd99bc9d961429daf868afd3650bbbd96` |
| `lib/arm64-v8a/libhumanvision_input.so` | `d3968a4f12d1cf7882c04312c7f0ad095c4a18b894846e2beafb758e9f582628` |
| `assets/HumanVision/Runtime/index.json` | `89703ad345f2b7a954b6711f8e65ffbd02e5f80cbf1c653d6815a53395ee3cfc` |
| Original video-1.mp4 | `e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8` |
| RTSP raw records | `61e2af19d9f436f3b594d091a9545d7c3a51217a056685c62574828b1721dad1` |

These APK/native/index hashes match the unchanged payload documented in the
[Camera observation](2026-10-04-formal-demo-camera-observation.md). This report
reran the raw analyzer, checked saved-analysis equality, hashed both APKs and
their native/index entries, and checked the original source-file hash. No native,
Input-library, model or profile modification is part of this observation.
Manifest commit `ff2a1fb5f487f972ca89dcbe116843fdc0f02c2f` is a declaration, not a
complete reconstruction of the already-built native library's source manifest.

## Visual evidence, exclusions and cleanup

The initial [live.png](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/live.png)
shows Opening/waiting for frame and Recognition error. Its retained seven-person
image is not successful RTSP preview or recognition evidence. Later
[result.png](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/result.png)
shows Streaming 1024x576, Ready 1/8 and an upright single-person skeleton/box.
[error.png](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/error.png)
is named “error” but shows the RTSP settings over Streaming/Ready 1/8. These
static screenshots do not prove continuous seven-body recognition, motion
following, left/right semantics, all-body accuracy or measurement-window timing.

[Screenrecord failure](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/screenrecord-failure.txt)
records an immediate nonzero result and no motion footage. The
[pre-RTSP Video export](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/pre-rtsp-video.jsonl)
is a Video run, not RTSP acceptance. The
[post-run transition export](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/after-run-transition.jsonl)
is interrupted after 1.694868 seconds with zero observations, and is excluded.
[Thermal before](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/thermal-before.txt)
and [after](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/thermal-after.txt)
both report status 0 and identical cached temperatures; these snapshots do not
establish sustained thermal performance or temperature evolution.

All four active phone settings files in
[settings-restored](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/settings-restored/)
match [settings-before](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/settings-before/)
byte for byte: shared capacity 4/four regions and RTSP empty URL are restored.
Backup files are not directory-identical: shared.json.bak differs and Rtsp.json.bak
is newly present. The owned reverse was removed; before/after reverse snapshots
are empty. [Owned-helper cleanup](../../out/input/formal-android-oct04-v15-observer/rtsp-device-1939/owned-helper-cleanup.json)
records MediaMTX PID 43248 and publisher PID 37256 exited, using saved executable
paths and start ticks. Cleanup was limited to the owned helpers and reverse.

## Reproduce the analyzer result

Run from the worktree root; the command exits 0 for manager VALID while the
output still reports native INCONCLUSIVE and target FAIL. This documentation
pass confirmed JSON equality with the saved analysis and preserved raw captures.

```powershell
$rtspPython = 'C:/Users/Tancheng/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $rtspPython -B tools/test/formal_demo_observation_analysis.py --manifest out/input/formal-android-oct04-v15-observer/rtsp-device-1939/manifest.json --records out/input/formal-android-oct04-v15-observer/rtsp-device-1939/observation.jsonl --output "$env:TEMP/formal-rtsp-analysis.json"
```

Native counter coverage, sensor time, physical joint/hand semantics, direction,
mirror and motion footage, and sustained performance remain
pending. No 30 FPS, whole-Task11, final physical acceptance or release claim follows.
