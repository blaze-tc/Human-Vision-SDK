# Integrated YOLO device and arm-following findings

The recovered pipeline at fc7f436, staged with2502652, ran the existing
VideoPlayer/AHB/ncnn route on e7c07019. APK SHA256:
`bdf046330737cef3d80cf54da1fbd38c845404b72f73963d7b81e6befb7a7255`.
Native SHA256:
`f3ab016bbe1d5dda83142a0a8f07e1840fe6eecd8dd30bee5896ac5c25568289`.
Runtime selection is explicitly local FP32 square320, capacity8, pinned video1
from37s. Seven-library APK closure, four selected runtime-file hashes, installed
APK identity, video identity and Unity build/settings restoration all passed.

Actual capture:
`out/android-yolo/eval-square320-20261001-m3-retirementfix/device-yolo320-retired-seven-75s/`.
The five-second warmed observed window spans82.334s, including collector
overhead:1538 distinct source-frame/sequence observations,1128 with body count7,
402 partial, zero empty,8 with count8. Observed fresh FPS is18.68; the count7
subset is13.70FPS. A count7 alone does not certify independent person coverage
or accurate joints. Result age P50/P95 is70.56/100.95ms. There are zero GPU-worker
errors, copy/import/pose errors and full-frame input CPU readbacks. The actual
screenshots show upright visible bones. This is recovery, not acceptance.

The existing fixed minimum33.333ms GPU admission gap drops about24% of25FPS
VideoPlayer events under Unity frame-time jitter. After startup, bridge slot
drops stop increasing, so source admission is one independent throughput loss.
Native inference is around42ms in the captured HUD; fixing admission alone
cannot establish25FPS or joint accuracy.

User observations additionally reject the raised-arm following and jumping.
The overlay obtains current raw bodies, not the sampled/held smoothing output.
The unchanged immutable offline network outputs at video frame1500 were copied
to a new ignored analysis directory and rendered with the existing reference
decoder. Both square320 and square416 put many raised wrists near the torso.
The already FP32-eligible640x384 rectangle tracks the same visible raised arms
substantially better. This points to insufficient effective person resolution;
it does not establish a full temporal joint-accuracy or640 performance gate.

Evidence overlays:
`out/android-yolo/arm-observation-analysis/seven-square320/gpu-fp32-overlay.png`,
`seven-square416/gpu-fp32-overlay.png` and `seven-640/gpu-fp32-overlay.png`.
Existing archives were preserved without modification. No generated joint
replacement, confidence lowering, extra holding or renderer smoothing was used.

A bounded640 precision check using the existing runner and frozen limits failed
FP16-storage eligibility for both seven/one fixtures (out0 max0.3952/0.3920,
limit0.2). These outputs remain in separate immutable archives and are not used
by the runtime. The approved FP32 candidate remains the only eligible contract.

Next corrections remain within YOLO-M3: independently review the GPU admission
fix, then evaluate the already eligible higher-resolution input geometry with
an explicit data contract and real arm-following checks. FPS and completeness
must be measured separately; no main merge or Release is authorized.
