# Unified input Task1: contracts and source-copy retirement

Base: `9c56d34`, isolated branch `codex/android-ncnn-vulkan-implementation`.
Approved design and implementation plan are in `docs/superpowers/`.

The new independent `com.blazetc.humanvision.input` package defines source state,
actual texture geometry, applied orientation, generation/frame identity and a
bounded source-copy lease protocol. It has no SDK, model, ORT or ncnn dependency.
All Unity resource operations belong to the owning main thread. Source close
retires textures after outstanding source-copy fences, without accepting an
inference-completion fence. These tests use controlled fences, not physical GPU
acceptance evidence; producers and the native bridge remain later tasks.

Fresh review identified two timestamp omissions. The approved design takes
precedence over the plan's abbreviated field list: publication always uses an
independent managed input Stopwatch clock, and source observation/decode time has
its own explicit domain and clock identity. Stream PTS is separate. The source
gate rejects negative or decreasing publication timestamps across generations,
allows equal microseconds and preserves the last valid frame on rejected input.
Native decode time must not be subtracted from managed publication time without
an explicit tested clock mapping. Nothing claims sensor-capture timing.

Validation command:

```powershell
pwsh -NoProfile -File tools/test/run_input_tests.ps1 -Phase Core -Output out/input/task1-clock
```

The initial implementation RED had missing types. A duplicate texture destroy
ownership regression then failed before its fix. Timestamp regression RED ran
10 tests with 3 failures, followed by an explicit missing-clock-API compile RED.
The final actual Unity 2021.3.45f1 run exited 0 with **11/11 PASS**:

`out/input/task1-clock/20261001T1329198562346Z-20bd3fc5f01c4eb389a564cfebd01a45/summary.json`.

Final raw XML SHA-256:
`96AC60B0823FB2C3B327DE4B81945EBC9D444661B8823BF0924FBC3CB3368860`.
Runtime DLL SHA-256:
`673514F42980AFE83AE6394ABE9D7C11B4F85938D5F57A3DF8D262ADFBD26FE2`.
Fresh independent scoped review: **Spec PASS / Quality PASS**, both timestamp
findings addressed. The receipt records raw log/test DLL hashes and exact named test selection; root
independently checked all four hashes. Zero tests or stale XML cannot pass.

No user Unity project, device, backend, model or preexisting R4 source changed.
The existing measured Android single-person rate remains approximately 11 FPS;
input-contract completion does not close the 25/30 fresh-observation FPS gate.
