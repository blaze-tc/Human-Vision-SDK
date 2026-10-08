# Skeleton result FPS: dominant stage located, RK3588 layer trace ready

The supplied RK3588 session is limited mainly by the SDK processing chain, not
Unity's rendering rate. A long medium-quality phase averages175.608ms per result:
the single worker ceiling is about5.69 results/s, close to the measured5.366/s.
Low quality averages119.333ms, ceiling8.38/s, measured7.805/s. These are completed
whole-image results, including zero-body results; they are not complete skeleton
FPS per person. Detailed field grouping and output-bound repair are in the
[RTSP output report](2026-10-08-rk3588-rtsp-output-bounds.md).

Fresh GPU timestamps on the authorized connected OnePlus locate most model GPU
time in convolution. This corroborates the model cost on that device, but does
not establish the RK3588's per-layer GPU timings. The dedicated diagnostic APK
is built and tested so the user can collect that remaining hardware evidence in
one run and export it from HumanVisionSettingsDemo.

## What the code and field data establish

- Actual Android profiles select `backend.ncnn.vulkan` / `pipeline.yolo.pose`.
  The active packs use FP32, packing enabled, subgroup and all three FP16 options
  disabled. This route does not invoke RKNN/NPU. Device capabilities alone do not
  mean that this SDK is using them. ModelPackManager validates these options;
  changing them is a model/backend qualification task, not a UI-only setting.
- YoloGpuPipeline performs one full-image network run per claimed frame, followed
  by dense-output decode. MaxBodies and region filtering do not reduce the network
  graph size. A zero-person image still executes the network.
- GpuRuntimeHost drains to the latest ready frame and runs one asynchronous
  worker. Its idle sleep is2ms; there is no5-FPS sleep or fixed rate cap.
  InputAdapter advances preview before submission and does not hold preview until
  a skeleton result. Low result rate and input publication rate are distinct.
- In the field, input publication14–16/s exceeds completion4–8/s, and Unity~30/s
  exceeds both. Processing cost alone nearly accounts for the observed result
  throughput. Body-result age381–441ms at medium and292–301ms at low explains lag;
  age starts at local publication and includes later sampling/holding time.
- `pose_ms`/backend timing includes import, preprocessing, network execution,
  GPU waits and output work. It must not be labeled pure neural-network GPU time.
  `total_ms` additionally includes postprocessing/common services.
- Android RTSP ignored requested output bounds and published4K textures. Fix2
  repairs that managed allocation/binding bug. Same-phone A/B verifies the bound
  but leaves body-result age~149ms: a substantial latency improvement was not
  demonstrated. Original stream decoding is unchanged.

Neither field logs nor the phone test isolate camera/network/decode delay, prove
the exact preview.4-versus-new-scene regression, or establish hardware failure.
Four transient Apply/retirement errors are separate from stable-run throughput.

## Fresh same-phone GPU and raw-stage measurements

OnePlus LE2120 / Adreno660, Android14, actual existing PC RTSPServer/VLC H.264
1280x720 stream; requested640x480, actual output640x360, MaxBodies4, no regions.
Actual selected model profiles and analysis dimensions are confirmed in each
exported session. The later source delivered~10 new frames/s, unlike the earlier
uninstrumented Fix1/Fix2 A/B. Low/medium completion is therefore also input-limited
in this diagnostic run. Do not interpret its throughput as an uninstrumented
model ceiling or a regression. These phases have no sampled bodies; they prove
full-image network execution/timings, not motion accuracy or skeleton acceptance.

| Profile | Valid/warm sampled frames | GPU layer mean ms | Convolution mean ms / share | Preprocess submit+wait ms | Extract+download wall ms | Final submit+wait ms | Dense CPU copy ms | Ownership release ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Medium640x384 | 10/7 | 47.997 | 40.296 / 83.96% | 2.872 | 70.983 | 5.877 | 0.454 | 2.264 |
| Low512x288 | 10/7 | 44.838 | 38.312 / 85.44% | 3.925 | 63.739 | 7.113 | 0.328 | 3.386 |
| High960x576 | 11/8 | 75.823 | 63.119 / 83.25% | 2.695 | 102.996 | 8.633 | 1.012 | 1.651 |

Import/preprocess recording averages0.399/0.087/0.065ms respectively. Every warmed
sample measures all204 non-input graph layers. Begin/end IDs, query availability,
timestamp period/valid bits and submission records pass the existing strict trace
validator; native capture starts and has no overflow/error. The actual UGUI ZIPs
contain `device/native-*.log` with these records, not merely host-side logcat.

Layer timestamp sums exclude transfers and inter-layer gaps. Extract+download wall
time contains internal submits/waits. Their difference is not pure CPU overhead,
and overlapping wall intervals must not be added blindly. Timestamp queries and
logging add diagnostic overhead. No wait, precision, weights or synchronization
policy was removed or changed to create a faster result.

The independently revalidated historical same-OnePlus trace has11 warmed frames,
GPU43.914ms and convolution37.263ms (84.854%); it is historical corroboration only.
Its original log SHA256 is
`cd755bf0c907c3dd18c6268695750000758a60ec24980b78d3fb02455261324c`.

## Diagnostic implementation and verification

The test project's `AndroidNativeStageCapture` runs only when its build resource
sets `native_stage_trace=true`. A JNI-attached background thread reads own-process
Android logcat for `HV_TOPDOWN_NCNN`, `ncnn` and `HVInputGate`. It does not clear
system logs. It enqueues into the existing bounded diagnostic queue; native drops
have a separate counter and cannot displace Unity errors. Main-thread batches
redact/write/rotate `native-*.log` and include them in the existing ZIP export.
Disposal destroys its owned logcat subprocess to unblock reading. Normal Fix2
has this flag disabled. No gameplay API or native public ABI was added.

The current Fix1 source is compiled with
`HV_ANDROID_TOPDOWN_EVAL_TRACE=ON` and `HV_ANDROID_NCNN_EXECUTION_TRACE=ON`,
ARM64/API26/Release, RTSP disabled in SDK (independent Input owns RTSP), and
R4Parity/GpuGate/QNN/DirectML disabled. The retained2026-10-01 diagnostic ncnn
static cache was copied and all seven archives hash-verified; it was not rebuilt
or silently substituted for a newly built dependency. Runtime code was freshly
built against it. Exact configuration/build logs and CMakeCache are retained in
`build/android-rtsp-performance-trace`.

| Check | Fresh evidence |
| --- | --- |
| Diagnostic log RED/GREEN | Actual test-project DeviceDiagnosticSessionTests: RED9/11, two new failures; final GREEN11/11, zero skips, job4827b757. Redaction/ZIP and overflow/error preservation tested. |
| Trace validator tests | 13 PASS / 1 SKIP; missing positive dependency-cache fixture. Positive cache hash/patch wait-token checks separately run against the retained full source and pass. Not relabeled14/14. |
| Native audit | Android API26 ARM64; 504 strong imports resolved across dependency closure; exact original48 public exports preserved. |
| Android trace APK | Succeeded0errors9warnings, duration00:02:14.6916973; three actual grade sessions and UGUI native-log ZIP exports pass. |
| Guard failure retained | First trace build rejected a stale bridge SHA manifest. Manifest was updated to the audited diagnostic library identity; the guard remains enabled. |
| Final normal project integration | Restored normal Fix2; genuine bundled-video probe PASS, sequence310/users2, Unity/background logs, skeletons, CSV, redaction, clipboard and UGUI ZIP export. |
| Boundaries | Architecture/documentation, public surface and Input package checks PASS. |

Two initial locked-screen launches did not execute Unity/Awake and produced no
real diagnostic session; they are excluded. Only `medium-awake`, `low`, `high`
are evidence. After the successful run, normal Fix2 was reinstalled and original
phone settings were restored byte-for-byte; the app is stopped. The Unity project
also has its original Fix2 native library/package identity/build marker restored.
PC RTSPServer/VLC configuration was not changed.

## Delivery and remaining device gate

Local diagnostic APK:
`E:/UnityProject/Human-Vision-SDK-Test/Builds/HumanVisionSettingsDemo-PerformanceTrace.apk`,
272,280,629 bytes, SHA256
`657a94de7debe8ea516854d327c5a123455784533756457998021737af363544`.
Diagnostic native SHA256
`105f38d55ff63fe36a66b067dc808237844b5ac5b12eb7c406615518003fe107`.
Its39 model/profile/runtime entries and nine other native libraries equal normal
Fix2. Normal Fix2 APK SHA remains
`f4839c0c3d2d8e3ddbb4b156b00e34b374a74078a60540dcd44c4632e5a4066c`.

Receipts, raw per-grade sessions/ZIPs, strict `analyze_native.py`, and the guide
are in the user's `DiagnosticsVerification/PerformanceTrace-20261008`; credential
settings and pictures remain local and are not committed. RK3588 is not attached.
One medium-grade run with the original RTSP and recorded output selection can
now separate preprocessing, model GPU kernels, internal waits/output work and
ownership release on that actual device. Use normal Fix2 for smoothness/FPS A/B;
instrumented throughput is not performance acceptance. No new Release, NPU/FP16
optimization, full-hand acceptance or30 fresh complete skeleton FPS claim.
