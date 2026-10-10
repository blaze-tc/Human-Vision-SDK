# MediaPipe / current SDK OnePlus comparison

The user approved a disposable same-device model/pipeline experiment after
observing a gap against MediaPipe. This follow-up measures alternatives before
any production algorithm decision. It does not promote a new SDK model or ABI.

1. Pin official Lite and Full task bundles and MediaPipe Tasks Android. Preserve
   the installed SDK APK/native/model identity and user settings.
2. Select a continuous action segment from the user's StreamingAssets video.
   Record clip, decoded pixels and hashes. Use the same source content and
   capacities 1/4, explicitly recording actual returned people.
3. Validate the real Android task, worker/GPU ownership and input lifetime. Test
   the analyzer's duplicate/nonfinite/count/warmup guards before collecting data.
4. Serially measure CPU/GPU, Lite/Full and capacities 1/4 on OnePlus. Count fresh
   completed results, real returned bodies, task wall time, input preparation,
   one-second result windows and system thermal status. Keep failed/pilot runs.
5. Run the current SDK CPU/Vulkan on the same clip and capacities. Restore exact
   preferences after each run; retain source/profile/native stages and hardware
   diagnostics. Do not concurrently run inference or change thermal policy.
6. Separate application throughput from task execution and input-path differences.
   No ground-truth accuracy, complete hand schema, stable identities, RTSP
   end-to-end latency or RK3588/NPU performance acceptance follows from this test.
7. Publish the measured report and local reproducible evidence. Recommend the
   next route from identified measurements while preserving the user observed
   visual difference as a separate acceptance requirement.

The selected 45-65 s clip contains seven visible people. Capacities 1/4 do not
constitute acceptance of footage with exactly one/four people. Both routes loop
the same twenty-second clip and discard warmup. MediaPipe uses predecoded raw
RGBA; the SDK uses MP4 decoding. This is a controlled content comparison, not a
matched input-chain or equivalent model/operator benchmark.

8. Following the user report that MediaPipeUnityPlugin skeletons are visibly
   smoother, qualify the actual plugin version/sample and async texture path.
   The Android Tasks Bitmap VIDEO probe does not stand in for that benchmark.
   Pin reference sources, keep current measurements, and measure fresh callbacks,
   body continuity, source age and preview cadence separately on the same device.
   Do not change the active Unity project solely to fit a newer reference version.
