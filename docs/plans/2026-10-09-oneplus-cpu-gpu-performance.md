# OnePlus CPU / Vulkan performance milestone

Direct user authorization: optimize connected OnePlus CPU and GPU inference,
use the running RTSP computer camera, and evaluate faster models where useful.
This is the sole active follow-up milestone. Existing ABI, asynchronous latest
frame ownership, truthful backend reporting and model precision gates remain.

1. Capture identified existing APK CPU/video and Vulkan/RTSP baselines; keep
   computer camera and repeatable video measurements separately attributed.
2. Measure real ORT CPU tensors with 1/2/4 threads on the phone, comparing
   all output shapes/values. Evaluate existing smaller model alternatives before
   acquiring new weights. Do not promote failed precision candidates.
3. Trace RTSP converter completion/publication scheduling. Add regression tests
   before production changes; preserve fence proofs and asynchronous shutdown.
4. Build native affected targets, run focused/full native tests and boundary
   guards; synchronize hash-bound embedded resources, compile actual Unity and
   build an identified APK. Run CPU and Vulkan same-source comparisons, then
   sustained runs for selected improvements. Record failures as well as gains.
5. Update usage, performance report and status. Hardware and full hand acceptance
   on RK3588 remain separate. Do not claim a target passed from render FPS.

## Converter publication acceptance (before implementation)

Same-camera / same-model candidate, warm30s and >=120s capture: publication mean
>=29.0/s, no pipeline errors, completed-result mean>=20/s, and mean local result
age no more than5ms above CPU4 APK control101.913ms. This tests an incremental
pacing/latency candidate, not stable per-person20–25 acceptance or end-to-end
camera latency. Baseline camera publication28.371/s FAILS the29.0 criterion.
Capture and compare the actual image/bones and graceful fence/lease retirement;
if the improvement is not established, retain the CPU-only verified artifact.
No GPU queue wait in a metadata getter; only nonblocking real-fence status is
allowed. Destruction and target reconfiguration stay on render callbacks.

## User clarification 2026-10-10

Skeleton continuity is adequate in the user's current use case. The primary
objective remains higher actual fresh skeleton recognition FPS, not visual
smoothing or higher preview/render FPS. Focus on model execution and its waits,
then qualify faster execution/model candidates against preserved recognition
quality on the same OnePlus/video. Keep per-person results, source timestamps,
actual returned bodies, thermal state and stage timing in the acceptance record.
The separate seven-person/capacity1 empty-result finding remains a regression
case; it is not a substitute for the requested model throughput optimization.
