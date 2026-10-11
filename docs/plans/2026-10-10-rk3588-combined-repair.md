# RK3588 combined field repair

User requests one combined delivery for low skeleton throughput, RTSP stalls,
APK bundled-video failure, and low observed NPU load; do not ask for incremental
device tests. Field evidence: session-20261010-080649-482-b1084e4c.

1. Test-first Android APK video extraction/cache/cancel and actionable errors.
   Stream bytes to app-private file; preserve independent Input playback.
2. Test-first separate source-copy completion from CPU readback completion,
   bounded owned snapshots, identity/resize fences and safe fallback.
   Never release a live source before its actual GPU copy fence completes.
3. Test-first RKNN reusable input-memory candidate with same-frame runtime
   numerical/performance qualification and automatic legacy fallback. Preserve
   model bytes and public ABI. No unverified normalization/native-layout guess.
4. One test-project automatic fixed-video/RTSP comparison, reporting actual
   completion, source publication, age, hardware, qualified input path and errors.
5. Focused/full affected regressions, ARM64 build, OnePlus video/RTSP CPU/Vulkan
   runtime and four-resolution regression. Seal one APK and all receipts.

Driver-specific RGA/native decoder-to-NPU interoperability cannot be certified
offline. Do not silently activate an untested path, promise 20–25 FPS, inflate
NPU utilization, or publish private model/vendor artifacts in a public Release.
Record concrete completion and remaining physical acceptance truthfully.

## 2026-10-10 model-readback candidate verification

- Unity 2021.3.45f1 / D3D11 EditMode test job `06aa0328`: 0/3 passed.
  All candidate RGB bytes were zero. The shader lacked the `_MainTex`
  texture property required for the Blit source/dimension binding.
- Added that property; unchanged strict complete-RGB comparison then passed
  3/3 in job `911a6ada` for 640x360, 1024x576 and 1280x720 inputs resized
  to 512x288. No error tolerance was introduced.
- Actual leaf evidence: test project
  `DiagnosticsVerification/Dual-20261009/EditorTests/`, UTC
  `2026-10-10T10:09:40.6021919Z`.
- This is an isolated GPU pixel test. The model-size readback candidate is
  not yet wired into production submission; Android pixel qualification,
  skeleton accuracy, lifecycle tests and RK3588 throughput remain open.

## Integrated software verification

- Final native CTest: 420/420, including the qualified-input failure regression
  covering set/run/get/release failures after
  qualification, requiring same-frame fallback to the original RKNN context.
- Unity actual leaf results: InputAdapter 22/22 (`348943b2`), model RGB/gate 4/4
  (`6736e115`), archive/cache/stream/cancel 6/6 (`a00d96db`), device diagnostics
  51/51 (`e64305f6`), per-person deduplication 1/1 (`58c96160`). Do not count
  earlier cached test runs that contained only the project root.
- ARM64 API-26 audit passed, 506 strong imports resolved; 50 public HV/UnityPlugin
  symbols equal the previous private binary. New private native SHA256:
  `26d94dba9100275790e7088593bf78e7a853ca0ea14c69b1dfbf63668396277b`.
- Independent snapshots and model-size submission are integrated. Full RGB
  equality is required on three device frames; a failed gate keeps baseline.
  Accepted pixel dimensions and minimum result frame change together; reconnect
  invalidates outstanding old-generation results immediately. 4:3 keeps the
  original preprocessing route instead of stretching to the model aspect ratio.
- Stream APK video to app-private disk, with completion receipts, explicit
  cancellation and a separate bounded 90-second extraction budget.
- Same-package comparison and `persons.jsonl` are installed into SettingsDemo;
  final Android build/device execution is tracked separately, not inferred from
  these editor tests. RGA assessment: `docs/maintenance/RK3588_RGA_ASSESSMENT.md`.

## Additional actual-device findings and regressions

- First OnePlus run exposed an unregistered `persons.jsonl` writer. Added the
  stream whitelist entry, preserving arbitrary-path rejection; new export test
  failed `6c0da98d`, passed `cd24954c`. Existing session tests 12/12 `c77c567c`.
- Rebinding now invalidates pending CPU frame identities in `Detach`. Regression
  `848c2645` passes; existing adapter 22/22 `568ae2ed` after final callback change.
- Device dual-readback fixtures exposed expired request status being read after
  waiting for the slower peer. Unity documents a fulfilled request being disposed
  on the following frame and then reporting `hasError`:
  https://docs.unity3d.com/2021.3/Documentation/ScriptReference/Rendering.AsyncGPUReadbackRequest.html
  Both the diagnostic probe and production bridge now capture success/error in
  each callback. NativeArrays remain owned until both completions. Regression
  `c22cc1c3` explicitly waits until the successful request reports disposed/error,
  then verifies the cached success still submits exactly one frame. Probe coroutine
  `2e319112` passes all six RGB/mirror fixtures in the editor.
- The automatic comparison applies the configured overlay style after direct
  initialization, matching the regular Apply action without saving preferences.
- Intermediate OnePlus session `session-20261010-114654-155-9a329705` demonstrates
  1770 RTSP preview publications/60.002s, real 103043218-byte APK video extraction,
  CPU/Vulkan input phases, per-person files, and cancellation restoring the draft.
  It is not the final performance acceptance: probe lifetime/style findings above
  required another build. No collector errors after the stream registration fix.
- Combined analysis unit tests 2/2:
  `python -m unittest discover -s tools/benchmark -p test_analyze_combined_benchmark.py -v`.

## Settings interaction and FPS follow-up

- Actual OnePlus capture confirms the disabled dropdowns were the active automatic
  comparison lock. Cancellation restored the computation dropdown (captured open).
  Add explicit lock instructions to the left status, label the cancellation action
  `停止对照 / 解锁设置`, leave diagnostic file actions accessible, and release the
  owned CanvasGroup lock from both normal completion and OnDisable.
- Vulkan's left HUD previously read legacy native FPS fields, which were zero while
  the separate diagnostic observer recorded real updates. New managed observations
  count distinct input publications and accepted source-frame results, including
  empty results; labels explicitly say input/new-result FPS, not per-person FPS.
  SDK and settings logs expose the same values and retain separately named native fields.
- Counter regression `2575772b` passed one actual leaf (duplicate/older frames,
  stalled window, source reset). Disable-lock regression `c4583b2f` passed one leaf.
  The first lock test `2debac4c` failed because EditMode does not dispatch normal
  MonoBehaviour lifecycle callbacks; the corrected harness invokes OnDisable explicitly.
- OnePlus session `session-20261010-125040-886-c824db2b` completed the full first
  comparison at 13:06:25 UTC (six exact RGB fixtures, video/RTSP CPU/Vulkan and
  300-second video Vulkan). A later repeat was cancelled at 13:10:27 UTC. Analyzer
  reports zero collector/Unity errors and zero data-quality warnings. Full results
  are retained under `out/rk3588-combined-repair/oneplus-comparison.json` and
  `oneplus-pipeline.json`; this is not RK3588 or stable per-person acceptance.
