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
remain unchanged. Real Unity APK stage/installation and measured phase results
are pending. No main merge, Release or physical acceptance is claimed.
