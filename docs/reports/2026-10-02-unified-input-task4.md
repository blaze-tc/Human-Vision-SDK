# Unified-input Task4: independent Unity RTSP source

2026-10-02. Base `99a19c09af32d21818ef48a07620118992f4b5ed`.
Task4 supplies the Windows Editor RTSP source and controlled real-stream acceptance.
Fresh review and commit status are recorded in DEVELOPMENT_STATUS and the SDD ledger.

## Delivered behavior

The independent input package consumes all eight Task3 C exports, without models or
the recognition SDK. RtspFrameSource publishes actual dimensions, generation,
frame sequence, independent native decode clock and PTS; managed publication time
uses a separate clock. Successful exact-sequence CopyRgba metadata is authoritative.
Unknown color remains Unknown. Native top-left rows are flipped once, and optional
display mirror is applied once through the shared GPU normalizer.

Windows uses reused CPU RGBA upload buffers and Unity Texture objects. Close cancels
without waiting for inference or network-worker termination. A persistent bounded
retirement queue preserves native handles through ReleaseBUSY and survives source
disable/destroy, including when no Texture exists. Consumer source-copy fences
still control texture retirement. Missing plugins and invalid settings fail with
actionable errors; credentials are omitted from public errors.

Review found a real allocation-failure double-free path during buffer growth.
Replacement allocation now succeeds before old storage is released; failure keeps
the old owner intact until Close clears the pointer and capacity. A controlled
allocator test exercises the actual Fail/Close path without exhausting the heap.
The original implementation recorded two frees; the fix records exactly one,
including repeated Close. Delegates are initialized once, not per frame.

## Actual validation

```powershell
pwsh -NoProfile -File tools/test/run_rtsp_fixture.ps1 -Platform Windows -Output out/input/task4
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -Output out/input/task4-regression-core
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase UnitySources -Output out/input/task4-regression-unity
py -3.13 tools/maintenance/check_architecture_boundaries.py
```

Final RTSP receipt:
`out/input/task4/20261002T0611575963412Z-7afbef5b918a4cfb831e6e33e4016860`.
Runner exit0; EditMode3/3, PlayMode4/4, separate empty-native project1/1;
each Unity process exited0. Core11/11 and UnitySources6 EditMode +12 PlayMode
regressions passed before the review fix. The two shared-file final changes only
normalize line endings; retained byte-equivalence proof justifies not repeating
those29 cases solely for newline changes. Architecture checks exit0/PASS.

The fixture encodes actual approved video-1 content with asymmetric corner markers
to640x360 H.264, then publishes real RTSP/TCP through pinned MediaMTX v1.12.3.
Tests verify mirror/nonmirror pixels, no-model preview, one-second server outage
and recovery, source generations, stale-image clearing, actual stalled native
worker retirement, and missing-library errors. FFmpeg fixture-tool8.1.1 is hash
pinned but upstream-archive reconstruction is unqualified; it is not shipped.
The native DLL remains the qualified Task3 binary `9a3cb144...`; full hashes,
official server provenance and actual cached test dependency contents are retained
in the source manifests. All ten owned helper identities were absent afterward.

Final raw frame evidence: requested1920x1080, actual640x360; managed generation2→3,
native generation1→5, Close0ms. During the bounded outage,9788 control ticks ran;
maximum observed batchmode render interval1.582ms. These are Editor control checks.
After warm-up,50 fresh upload publications over2s measured0 managed bytes around
the actual production Update call, with coroutine/test setup excluded. Publication
age6867us is managed publication-to-observation only, not camera capture latency.

The allocation-failure regression first failed with actual duplicate cleanup,
then passed targeted1/1 and the final8-case suite. Original RED, round0 reports,
round1 diff and55 exact source/artifact hashes remain in the SDD ledger. Final
logs no longer warn about package mixed line endings; retained external test
framework obsolete-API warnings remain and are not claimed absent.

## Limits

This task does not qualify Android hardware decode/AHB/Vulkan, physical IPC,
Windows player packaging, UDP/H.265, sustained performance, or skeleton acceptance.
Android tasks5–7 and package/demo delivery tasks8–11 remain separate gates.
Camera raw skeleton output remains approximately11FPS;25 interim/30 fresh complete
observation frames/s is unmet. Source preview speed is not skeleton throughput.
All34 preexisting protected R4 files, caches and the user Unity project remain.
No main merge or Release.

Root finalization removed only an extra empty line at fixture-script EOF to pass
Git whitespace checks; executable bytes are unchanged. Reviewed round1 manifests
retain their original snapshot. Final script SHA256:
`24cefbb58da9b7fae7c48feeb8d1ea4e8b67058f9adac7fb51fa0403334bb130`.
