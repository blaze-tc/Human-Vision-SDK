# Camera diagnostic and unified input proposal — 2026-10-01

Status: bounded camera orientation repair software-reviewed and installed; upright direction and basic following confirmed by user. Full-body production/30FPS acceptance remains FAIL.
Unity standalone RTSP/unified input design approved; detailed implementation plan written, awaiting review, not implemented.

## Retained failed attempt

Initial real-device camera APK SHA-256:
`b7269c63f6a21376e3c7e1f9730f8e1ca4d9cdcf2459578671d42cdfe7f15681`.
Separate package `com.blazetc.humanvision.cameratest.oct01` preserves the accepted video APK.
Actual front WebCamTexture1280x720, requested30FPS, initial rotation180 / vertical mirrorfalse.
Run1 source activation1790856834.277; rotation changed180→90 after approximately13seconds.
Native rejected unsupported16:9 landscape geometry; collector failed and retained incomplete
capture with log SHA `ae2b3866df3a6c4ebb3a95985b3b5dd6c114b9d7d337e16b7868b4517332107d`.
No60-second performance acceptance can be derived from this run.

Later direct phone screenshot showed upright preview and vertically inverted skeleton.
Code trace ruled out double application of rotation metadata: bridge metadata controls slot
contract equality/invalidation. The working video route explicitly normalizes GPU source rows
while retaining a separate upright preview. The live camera route lacked that GPU-only row
normalization. Repair uses a reused linear submission RenderTexture and separate preview.
Native models/AHB/import/sync protocol remain unchanged. Device run2 screenshots15/30/45 confirm upright head/torso after repair; user confirmed direction correct and following basically usable. Feet/lower legs were outside the frame, so this is not complete-body acceptance.

## Automated evidence

Focused Unity RED:2/2 failed before missing row normalizer/lease fix.
Focused Unity GREEN:3/3 passed, including asymmetric2x2 GPU blit diagnostic and preview/source
geometry contract. Routing/admission GREEN:12/12 passed separately. Diagnostic readback occurs
only in the Editor regression fixture, not Android production camera code.
Independent spec compliance/quality review PASS for Unity camera fix; UPM GPU migration excluded. UPM LiveSource restored exactHEAD bytes and its bridge preserved before-edit bytes because the existing UPM runtime is CPU-only. Both overlay default values were updated, but no UPM GPU readiness/publication is claimed.

## User-requested visibility adjustment

Compared with the currently running3px lines and9px point diameters, installed camera build uses
9px lines and27px point diameters. SDK overlay defaults use the same values. This is appearance
only, with no inference model/renderer architecture change.

## Real camera run2 and sustained low-FPS observation

Build actual exit0 and installSuccess. APK SHA `ff7e2cc0b348826ea16c1343b9ce5e431340ed0d033d3825ca5f38bc6c9210de`. All7native library hashes,4selected runtime files and embedded index match the original same-native640 baseline; no MP4 is packaged. Native remains `60d847e993db2e8a446f5a6807240be95229c695e20a7040db21474cfab5cb42`; current tested snapshot includes existing uncommitted R4 changes, and a clean source commit alone is not asserted to reproduce this native artifact.

Run2 capture60s completed with actual1280x720 frontcamera, rotation0/verticalMirrorfalse. Rawlog SHA `bdef45a1f54dfd8484f4b7b770ac7b69d6f83646cc562aabf8d6ab2ce47169f8`. Fixed40s after10s warmup:582 unique observation frames=14.55FPS, including554one-body frames=13.85FPS and28empty frames. P50/P95 UnityObserved result age102.807/165.584ms. Stats native562/38.558s=14.5754FPS; source1161/38.558=30.1105FPS;209no-slot drops. Copy/import/runtime errors and full-frame CPU readbacks0. Independent facts reviewer recomputed identities/metrics.

User subsequently reported raw-body approximately11FPS. Retained post-capture PID20654 log confirms native rolling fresh_fps11.02542→10.88767→10.38672, detector attempts equal each fresh frame, bodies1. Screenshot45 shows approximately89ms inference. The camera is not the30FPS bottleneck; the current640x384FP32 whole-image model still runs per observation regardless of body count. No causal heat/clock attribution is made: after app exit thermal status0, GPU sensors44.5/52.4C and battery42.3C were reported, and GPU clock history is not established. Screen45 clips right/lower HUD text, to cover with the requested responsive shared Demo UI.

>=25/30FPS remainsFAIL. Input architecture work is not claimed to fix model inference cost. No further device profiling run or unapproved model/native optimization was performed.

## Approved architecture and pending execution plan

[Unified-input proposal](../superpowers/specs/2026-10-01-unity-unified-input-and-standalone-rtsp-design.md),
commit `5a94798`, independently reviewed for source-copy retirement versus inference lifetime
and readback/error counter semantics. User approved design and requested planning; latest three-scene/common-settings request incorporated. [Implementation plan](../superpowers/plans/2026-10-01-unity-unified-input-and-standalone-rtsp.md) awaits written review before product implementation.
Independent RTSP preview must not initialize models/Runtime; Android RTSP uses a separate
hardware decoder/Vulkan producer with no full-frame CPU decode/readback fallback.
Current WebCamTexture ages are UnityObserved, not sensor-verified. The R4 sensor gate and30
fresh complete observations/s acceptance remain unpassed.
