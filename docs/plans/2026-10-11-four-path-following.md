# Four-path following optimization

User authorized all four changes and connected OnePlus verification on 2026-10-11.
This extends the native-I/O follow-up; visual following, not render-frame counts,
is the outcome. Existing source-frame identity and accuracy gates remain binding.

1. Supply/preview: newest prepared frame admission, bounded overlap, explicit
   source identity and age. Preview never waits for inference.
2. RKNN: explicit cache ownership candidate and native output conversion;
   independent baseline fallback. Evaluate bounded independent instance scheduling
   (one pending latest frame, monotonic publication) as a separate experiment;
   do not assume three cores imply threefold speed.
3. Model: reproduce a graph-layout candidate from the pinned weights and run
   unchanged simulator/pose gates. Extend reproducible calibration/quantization
   tooling where evidence justifies it; failed candidates never replace the model.
4. RGA: implement a separately gated path with runtime capability checks,
   supported AHardwareBuffer import, crop/color/stride/fence ownership and bounded
   reusable destination buffers. No arbitrary fd/layout guessing. Unsupported
   devices retain the existing preview and analysis path.

For each component: add a failing behavior test, implement, run focused tests,
build affected targets, record evidence. Then integrate in HumanVisionSettingsDemo,
build one APK and test on the connected OnePlus (video and RTSP, CPU/Vulkan,
controls and skeleton following). Record skipped RK3588-specific tests explicitly.
Model/RGA/multi-instance performance and sustained 20–25 real results/person/s
remain RK3588 hardware gates, never inferred from OnePlus or unit tests.
