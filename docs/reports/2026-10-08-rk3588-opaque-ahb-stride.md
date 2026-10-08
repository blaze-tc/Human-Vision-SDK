# RK3588 field diagnosis and Settings Demo retest build

The supplied HumanVisionSettingsDemo session has real RTSP publications but zero
accepted/submitted/processed frames and zero native/managed result sequences.
The GPU-only AHB descriptor reports stride=0; the old bridge requires a positive
CPU row pitch and rejects it before actual Vulkan import probes. This maintenance
patch lets the measured opaque layout reach the existing Vulkan checks. Physical
RK3588 recognition and smoothness remain unverified until the retest APK runs.

## Field evidence

The 20261008-080240-033-84e39f07 session contains 611 events, 481 snapshots and
481 performance rows. The 232 active Running/Streaming samples span 235.168 s:
published frame 4 to 3627, actual 3840x2160, 3623 publications / 235.168 s =
15.406 FPS. One-second windows range 7.979–24.745 FPS. Every active sample has
acceptedFrame=-1, submitted=processed=nativeSequence=sdkResultEvents=0;
223 health samples are PublishedButNotSubmitted.

Runtime diagnostics repeatedly report `GPU bridge status -6`, `stride=0`,
`failed: actual.stride`, requested ncnn Vulkan, actual backend uninitialized and
zero detector executions. Subsequent zero producer/consumer facts are unexecuted
probes, not evidence that the driver lacks every listed capability. No video
playback, camera-to-display latency, packet/decoder/GPU stage or actual Unity FPS
was measured in the supplied session. Publication age excludes transport latency.

The original log folder and screenshot remain in the test project. Sanitized
analysis is in `真实设备日志/analysis-20261008.json` and its Chinese companion.
No credential-bearing raw device logs are committed to the SDK repository.

## Bounded changes

- Candidate admission, temporary Android probes and persistent slot creation
  accept measured stride=0. Nonzero stride below width remains rejected.
  Dimensions/layers/format/usage and real Vulkan properties, capability queries,
  creation/import/bind/view on both devices remain mandatory; failure rolls back.
  No invented stride, CPU fallback, model/threshold change or fake result.
- Three focused host regressions cover opaque descriptors on both copy paths,
  reaching actual probe operations, failed imports and persistent resource cleanup.
  The original 48 public HV/Unity exports and V1 ABI are preserved.
- User-requested Settings capture geometry now uses four dropdown presets:
  640x480/1280x720/1920x1080/3840x2160. FPS and each mode remain independent;
  existing nonpreset settings retain a visible saved option. Editor API upgrades
  existing scene/prefab controls while preserving their other references.
- `SetBundledVideos` accepts real project-provided file URLs, survives refresh,
  resolves an empty custom path to the selected video and permits switching an
  old custom path back to even a single bundled item. Android cannot enumerate
  APK StreamingAssets using Directory.GetFiles.
- Actual test scene/prefab include project diagnostics, explicit Android target
  60 FPS and a build-generated Resources video catalog. Added telemetry records
  Unity frame count/FPS, sample-window maximum frame gap and target FPS alongside
  publications and new skeleton events. None is a per-person FPS acceptance claim.

The standard repository UPM native/model closure is preserved; the retest project
uses a local embedded SDK version `0.4.0-preview.6.rk3588fix1` with the newly built
Android native library and matching metadata. PackageCache is untouched. Input,
model/profile and Windows plugin bytes remain unchanged; managed Settings UI,
samples and documentation are updated in source and package. This is a local
retest build, not a new published GitHub release or device acceptance.

## Fresh verification

| Check | Evidence/result |
| --- | --- |
| Native regression RED | Three new tests fail before production changes: out/rk3588-stride-20261008/native-red.log |
| Affected native GREEN | `(Ahb\|UnityVulkan\|AndroidGpu)` filter, 118/118 |
| Full native Release | 379/379 CTest, 0 failures |
| Android native | NDK 23.1.7779620, ARM64 API26, Release; actual final SHA below |
| Native closure | 506 strong dynamic imports resolve against closure/API26 stubs; exact original 48 public exports |
| Resolution RED/GREEN | Source 0/4 -> packaged 22/22 settings, no skips |
| Video catalog RED | Missing registration contract: 0/1 expected failure |
| Final managed affected | Packaged Settings 23 + project diagnostics 9 = 32/32, 0 failed/skipped; Editor and Android managed compile PASS |
| Existing sample upgrade | Isolated Unity Editor API load/save preserves SDK/View, fields and four choices; no width/height text fields |
| Actual test project | Real StreamingAssets/video-1.mp4 selected via UGUI, native result sequence22/users1; copy paths/export ZIP/redaction/skeleton/CSV verified; visual video, skeleton, resolution dropdown and catalog caption checked |
| Actual Android player | HumanVisionSettingsDemo only, ARM64 IL2CPP/Vulkan, Succeeded, 0 errors/8 warnings, 00:03:17.7802558 |
| Delivered APK audit | Exact native SHA, 47 preserved model/profile/Input library entries, one scene/one ABI, real video and build/catalog resources present |
| Boundaries | Architecture/documentation and public surface PASS |

Commands: `pwsh -NoProfile -File tools/test/run_native_tests.ps1` (and the focused
filters above); `pwsh -NoProfile -File tools/test/verify_unity_sdk.ps1 -Package
-ProjectSuffix '-rk3588-fix1' -Filter
'HumanVision.Tests.HumanVisionSettingsDemoTests|HumanVision.TestProject.Tests.DeviceDiagnosticSessionTests'`;
`python tools/maintenance/check_architecture_boundaries.py` and
`python tools/package/check_public_surface.py`. Python uses the actual Python3.13
executable on this host, not its Windows Store alias.

An intermediate combined Unity verification project had duplicate diagnostics
assemblies and produced no test result; the failed log is retained at
out/sdk-api-verification/20261008-174116-198-unity.log. A fresh isolated suffix
ran the complete 32-case filter successfully. Unity-generated sample files have
normal empty-field trailing spaces; whitespace checking excludes generated scene
and prefab/metadata data. Existing broad legacy regression limits remain unchanged.

The Android build reuses pinned ncnn archives from the read-only verified cache.
Every copied archive matches the original receipt. Only CRLF/LF provenance bytes
differ; exact normalized equality is checked. `original-build-receipt.json` is
retained and the derived receipt explicitly records the reuse/EOL normalization.
No ncnn code, patches or flags were changed and its libraries were not rebuilt.
Final core flags disable RTSP/Input compilation; the existing Input package owns
RTSP. The initial RTSP-enabled preliminary build is superseded by this closure.

## Delivery and retest

`E:/UnityProject/Human-Vision-SDK-Test/Builds/HumanVisionSettingsDemo-RK3588-Fix1.apk`:
272,323,305 bytes; SHA256
`f9c71aeb8c8f042044a03fff87d452731643f4e72213e16aa5bc2a57c575d561`.
Native SHA256:
`d86a6acd5356ca1c7b39c6b277b7fc910db82a38a6353ccb90a87301f8283960`.

Included video: H.264/yuv420p, 1024x576, 25 FPS, 731.12 s, 103,043,218 bytes,
stored without APK compression; SHA256
`e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`.
APK/resources/ZIP/build receipts and readable guide are under the test project
DiagnosticsVerification/RK3588-Fix1 and HumanVisionSettingsDemo-实机测试说明.md.

Install this APK, retain prior app data, and first try the same RTSP with one person
for 1–2 minutes. Check whether submitted/processed/results advance and export ZIP.
Then compare an actual camera-configured H.264 720p/25–30 FPS substream; choosing
the Settings dropdown alone cannot rewrite the RTSP camera stream. Also select
Video/video-1.mp4 and export a separate ZIP. Target60 FPS requests do not guarantee
rendering or inference throughput. Neither this desktop test nor the APK build
closes physical Vulkan import, skeleton accuracy, real hand or 30-fresh-FPS gates.

Primary references: [Vulkan AHB contract](https://docs.vulkan.org/spec/latest/chapters/memory.html#memory-external-android-hardware-buffer),
[Unity targetFrameRate](https://docs.unity3d.com/2021.3/Documentation/ScriptReference/Application-targetFrameRate.html),
[Unity StreamingAssets](https://docs.unity.cn/cn/2021.3/Manual/StreamingAssets.html).
