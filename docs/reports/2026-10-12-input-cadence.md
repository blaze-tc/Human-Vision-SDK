# 2026-10-12 RTSP input cadence investigation

This round targets input scheduling. It does not change model weights, analysis
size, inference instance count, smoothing, or the public frame identity contract.
RK3588 is not connected; OnePlus verification cannot certify its performance.

## Field evidence and limits

RK3588 session `session-20261012-055106-833-f03c4133`, excluding startup: input
13.286 FPS, processed 12.924 FPS, fresh result events 12.536 FPS. Mean backend
47.46 ms, RKNN input_set 0.244 ms, run 42.35 ms, output_get 4.329 ms. This is
actual RKNN execution, not an inferred CPU fallback. The pipeline is fed a
3840×2160 decoded stream, publishes 1280×720, and analyzes 512×288.

The native log samples acquired-image geometry for the first three images and
then every 64 images. Excluding the first three lines, 71 periodic samples imply
18.644 acquired images/s from their timestamps (`64*(71-1)/elapsed`). This is
an estimate assuming no missing sampled log lines, not remote camera FPS. The
GPU completion sequence rate is about 13.694/s. These observations justify
investigating both output acquisition and subsequent conversion scheduling.
They do not establish the network sender's actual output cadence or isolate
GPU shader execution from host observation delay.

## Implemented changes

* A scoped output worker drains MediaCodec and acquires latest AImages independently
  of TCP packet reads. Each pass is bounded (first dequeue up to 2 ms, subsequent
  dequeues nonblocking, maximum 16 outputs, then a cancellable 1 ms wait).
  All teardown/reconnect paths join before codec/reader destruction. The compressed
  packet bound, latest pending image, existing GPU fences and three output slots remain.
* A second render opportunity at Unity end-of-frame can observe a fence that
  was not ready at Update, without waiting another complete Unity frame.
  It neither publishes duplicate frames nor adds a converter/model instance.
  Pause, close and disable stop this path.
* Returning a superseded pending image cannot cancel an output slot owned by a
  distinct active image awaiting the Unity queue callback.
* Once-per-second native logs separate packet/input/output/image counts from
  conversion completions, busy/empty callbacks, pending replacements and ring
  drops. Timing fields explicitly include scheduling/queue/observation delay.
  These are captured in the existing session folder by the native log collector.
* Settings distinguish requested camera capture size, RTSP postdecode output
  upper bound and video-file dimensions. Preview and analysis consume the RTSP
  output; the model size is independently selected by model quality.

## Automated checks

`pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -RunTests`:
50/50 host input tests; Android ARM64 build succeeds. The new output-worker test
covers an image becoming ready without another input packet, bounded polling,
repeated joins and exceptions. New end-of-frame lifecycle test fails before
implementation and passes afterwards in the actual Unity test project. Existing
deferred-open lifecycle tests passed 18/18 before the managed change; Settings
tests passed 26/26. Evidence under `out/input-fps-20261012/`.

`py -3 tools/maintenance/check_architecture_boundaries.py`: public surface and
architecture/documentation checks pass. No release acceptance is claimed.

## OnePlus measurements

The same PC webcam RTSP stream was independently captured for 30 s: 874 video
packets over approximately 30.05 s, consistent with about 29 FPS. Packet counts
are evidence for this capture, not a universal one-packet-per-frame assumption.
Baseline APK, low Vulkan, last 30 windows: published input 28.47 FPS, fresh
results 27.32 FPS, Unity 59.15 FPS.

The first independent-output candidate acquired 29–30 images/s. A later hot
window published 19.45 FPS / fresh results 18.47 FPS / Unity 42.93 FPS; GPU
conversion samples were about 23–24 FPS. Android thermal status was 3. It is
invalid to treat this hot window and the earlier baseline as a controlled speed
comparison. This candidate did not yet contain the end-of-frame opportunity.

Final APK `b27129e1…` completed its automatic comparison in session
`session-20261012-081025-184-5c4f5958`. All 14 GPU snapshot/model-RGB checks passed
with zero differences. The updated deferred-open fixture passed 19/19 (Unity
callback receipt `20261012-080947-861.json`, including the new render test).

| Final candidate condition | Published input FPS | Fresh result events FPS | Unity FPS |
|---|---:|---:|---:|
| Initial low-Vulkan RTSP, thermal status 0, last 30 windows | 28.370 | 27.546 | 59.470 |
| RTSP preview only, 1779 unique publications / 60.010 s | 29.645 | no model | — |
| Local video / CPU, 60 s | 25.015 | 7.002 | 60.056 |
| Local video / Vulkan low, 60 s | 24.847 | 24.361 | 58.394 |
| RTSP / CPU, 60 s | 29.609 | 5.607 | 60.106 |
| RTSP / Vulkan low, later hot 60 s | 20.208 | 18.760 | 43.130 |
| Local video / Vulkan low, later hot 300 s | 24.656 | 18.604 | 41.887 |

The camera mostly viewed an empty chair/room. Occasional false person detections
were observed; RTSP result events must not be called validated person-skeleton FPS.
The actual multi-person video played and displayed skeletons. Four sustained
tracks in the short video Vulkan phase had 24.05–24.12 fresh observations/s and
18.74–20.36 complete COCO-17/s, local result age means 60–62 ms. These rates do
not certify every joint or track across the full clip. The 300 s run includes
video loops and selection/identity changes; per-track zero-result windows remain
in `final-benchmark.json` rather than being hidden by rendering/interpolation.

The hot RTSP Vulkan window independently measured acquired images 29.990/s,
GPU conversion completions 23.426/s, published frames 20.208/s. Render events
87.2/s versus Unity 43.1/s confirm the second scheduling opportunity executes.
Mean decode-to-submit was 14.08 ms and submit-to-observed-fence 34.24 ms; neither
is isolated shader duration. Android thermal status 3 was observed. The output
worker continues receiving 30/s, but GPU/host scheduling and observation still
lose cadence under this load. The earlier one-event hot candidate was 19.45
published FPS; differing temperature/order prevent claiming this is a controlled
speed improvement. Initial performance is also essentially the same as baseline.

Manual stop/restart and HOME/foreground resume restored RTSP publication and
inference. The RTSP size label and explanatory text were visually verified on
the phone. No Unity Error/Exception categories or non-startup pipeline errors
were found in the comparison logs; the five measurement phases completed.
This is functional regression and stage-location evidence, not acceptance of
stable 20–25 complete skeleton FPS or RK3588 throughput. No model-accuracy or
false-positive fix was made in this input-only round.

## Candidate package / current verification boundary

APK build succeeded (0 errors, 8 warnings); all 47 indexed runtime assets verified.
Input binding hash matches the actual packaged library. Installation on OnePlus succeeded.
APK SHA256: `b27129e19630ba970d97f3252873e2883312e166868b35efced9ea2135031f38`.
Input native SHA256: `0639a50ecbe765bda84a4dbc6616bd9f5fcedbf51839e2dfb7c1588650511457`.

The user unlocked the phone and final-candidate verification above completed.
Do not publish this as a performance-accepted release. Evidence, raw session,
screenshots and analysis are copied to the test project's
`DiagnosticsVerification/Input-Cadence-20261012/`. The same tested APK is available
as `Builds/HumanVisionSettingsDemo-InputCadence-20261012.apk`.
