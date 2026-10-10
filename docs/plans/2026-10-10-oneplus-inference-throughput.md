# OnePlus actual inference throughput follow-up

Direct user instruction: continue improving true skeleton inference FPS; visual
continuity is adequate. Stable 20-25 fresh body results/s remains the near target.
This is the sole active follow-up, continuing the October9 performance plan.

1. Reuse verified baselines, rejected FP16/INT8 and SGEMM/local-memory evidence.
   Inspect official pinned NCNN convolution paths and existing timestamp traces.
2. First compare FP32 Winograd23 versus the existing mixed Winograd23/43 policy
   on identical real tensors, serial OnePlus runs, explicit thermal state and
   output shapes/values. Test candidate rejection before implementation.
3. Promote only a numerically valid candidate with a repeated meaningful model
   time gain. If promising, qualify a closed ModelPack execution contract without
   changing the public Unity/C ABI, model weights, input dimensions or identities.
4. Build native and the actual SettingsDemo APK; same video, MaxBodies4, Low,
   CPU/GPU truthful selection, fixed source and warmup. Count new results containing
   people and separate returned-person rate from each-person complete-joint FPS.
   Compare normal, candidate, normal; preserve settings and native ownership waits.
5. If Winograd23 does not win, retain the measured rejection and test bounded GPU
   submission batching separately using copied pinned NCNN sources. No wait may
   be removed, no device score falsified, and diagnostic FPS is not acceptance.
6. Run focused regressions and architecture/asset guards; update performance
   report, maintenance README and status; commit/push only verified scope. No
   RK3588/NPU, full-hand, model accuracy or stable target claim from shell timings.

Neither smoothing, interpolation, repeated rendering nor empty result throughput
counts toward skeleton FPS. Each experiment has a unique local evidence directory.
