# RK3588 latency diagnosis and bounded RTSP GPU output

The new user field session confirms that Fix1 now imports frames and produces
genuine RK3588 skeleton results. Medium quality completes about4–5 results/s,
with body-result age about381–441ms; low quality completes about8/s at292–301ms.
RTSP publishes about14–16 images/s while Unity renders about30 FPS. These measured
rates explain slow updates, but do not locate every network/decode/GPU delay.

This managed maintenance fix addresses a demonstrated configuration bug:
Android RTSP allocated three full decoded-size GPU targets despite the requested
output bounds. It now uses the existing GPU color converter to produce bounded,
aspect-preserving targets. The connected OnePlus verifies output and recognition;
it does not demonstrate a material result-age improvement. RK3588 performance
retest remains the active gate. No new release or hardware performance acceptance.

## Field evidence and limits

Session `20261008-113039-734-9ebdd9fa` has4586 CSV rows and6583 events. Exclude the
first stopped hour and group by source/generation/Running+Streaming/counter resets.
Events are sorted by elapsed time and record ordinal, not rotated file suffix.
Runtime metadata comes from nonempty streaming snapshots; stop snapshots clear
the profile while retaining the previous source identity.

RK3588/Mali-G610, Android13, Unity2021.3.45f1, Vulkan; marker RK3588-Fix1-20261008.
All five successful phases publish3840x2160 despite lower resolution requests.
The table uses whole-phase unique counter deltas; timing distributions discard
the first5s and result age includes only samples with a body.

| Source | Profile analysis | Duration s | Publication FPS | Complete-result FPS | Total median ms | Body-result age median/P95 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 2 | Medium640x384 | 38.810 | 14.996 | 4.690 | 183.695 | 441.342/533.314 |
| 6 | Low512x288 | 101.910 | 15.602 | 7.673 | 121.256 | 301.286/383.873 |
| 10 | Low512x288 | 268.167 | 13.958 | 7.805 | 117.736 | 292.392/373.358 |
| 14 | Medium640x384 | 20.397 | 13.384 | 4.265 | 179.015 | 395.870/548.386 |
| 18 | Medium640x384 | 273.396 | 13.903 | 5.366 | 173.690 | 380.740/485.772 |

GPU bridge submitted/imported counters advance together, device/driver UUIDs match,
backend.ncnn.vulkan is initialized and there are no worker/pose validation errors.
Legacy Native FPS=0 and CSV dropped=0 omit GPU-path telemetry and must not be read
as no inference/no dropping. Ready-slot overwrite counters do not alone establish
an invalid generation or unbounded queue. Result age starts at local publication;
camera-to-display latency is not measured. Source decode PTS and InputMonotonic
timestamps are different clocks. Completed results can contain no body and are
not per-person complete skeleton acceptance.

Four Apply attempts fail while the prior owner is retiring, followed by successful
attempts. This lifecycle issue is recorded separately and is unchanged here.
Historical preview.4 smoothness used the same RK3588/RTSP but the Sensory Setting
scene. Input Android code/native bytes compare equal across published versions;
the output-bound defect also predates this change. The exact smoothness regression
still needs matching scene/model/input workload measurements.

Raw logs, video, credential-bearing settings and pictures remain in the user's
test project. Sanitized Chinese analysis and reproducible Python analysis are
under `真实设备日志` and `DiagnosticsVerification/RK3588-Latency-20261008`.

## Bounded implementation

- `AndroidRtspGpuSource` stores validated requested maxima and selects target size
  before allocation/binding on each actual native geometry generation.
- Integer aspect comparison avoids float rounding standard16:9 sizes down by one
  pixel. Never upscale, stretch or exceed bounds; invalid dimensions reject.
- Existing GPU conversion writes directly to the selected output dimensions.
  No CPU readback, decoder reconfiguration, native ABI or model changes.
- New-generation Unity logs distinguish decoded/requestedMaximum/publishedOutput.
  Preview, adapter and skeleton image coordinates follow actual published geometry.
- The settings builder upgrades the existing resolution hint as well as new hints.
  Source/UPM code and user-guide mirrors match. Android behavior is explicitly
  staged in local Fix2; published preview.6/Fix1 still publish source dimensions.

4K-to720p output contains1/9 the original pixels, but this is a workload reduction,
not a measured9x speedup. Encoded transport and MediaCodec still decode the original
stream; choose an actual camera substream to reduce those workloads.

## Fresh verification

| Check | Result and receipt |
| --- | --- |
| RED | 14/14 expected failures before SelectOutputSize existed; `20261008-211938-424-results.xml` |
| Input plugin-present | 54/55 PASS; 14 new bounds cases green; `20261008-212145-215-results.xml`. MissingInputPluginIsActionable fails because the native plugin is present in this fixture. |
| Correct plugin-absent lane | 15/15 PASS, zero skips: missing-plugin contract + repeated14 bounds cases; `rtsp-output-absent-results.xml`. All55 unique Input cases pass across their applicable fixtures. |
| Settings affected | 23/23 PASS, zero skips; `20261008-214029-115-results.xml` |
| Compilation | Editor and Android managed compile PASS in verification projects |
| Actual HumanVisionSettingsDemo | Genuine bundled video, native sequence30/users1; skeleton, Unity logs, CSV, redaction, copy/export PASS; `DiagnosticsVerification/real-video-probe.txt` |
| Actual Android player | Single Settings scene, ARM64 IL2CPP/Vulkan, Succeeded0errors8warnings, 00:02:16.0455182; original Editor.log success excerpt retained as `RK3588-Fix2/android-build-success-excerpt.log`. The root android-build.txt was subsequently replaced by the separate diagnostic build. |
| APK integrity | 47 native/model/profile entries byte-identical with Fix1; only managed/IL2CPP output changes; `RK3588-Fix2/apk-audit.json` |
| Boundaries/packages | Architecture/documentation, public surface and Input package checks PASS |

Unity receipts are in `out/sdk-api-verification`. The prior379/379 native suite and
48-export closure evidence belong to Fix1; native code/bytes are unchanged here
and those native tests were not rerun. Existing unrelated broad-regression limits
remain. Do not relabel the intermediate54/55 run as55/55 in one fixture.

Commands:

```powershell
pwsh -NoProfile -File tools/test/verify_unity_sdk.ps1 -Package -ProjectSuffix '-rtsp-output' -Filter HumanVision.Input.Tests.AndroidRtspOutputSizeTests
pwsh -NoProfile -File tools/test/verify_unity_sdk.ps1 -Package -ProjectSuffix '-rtsp-output' -Graphics -Filter HumanVision.Input.Tests
pwsh -NoProfile -File tools/test/verify_unity_sdk.ps1 -Package -ProjectSuffix '-rtsp-output' -Graphics -Filter HumanVision.Tests.HumanVisionSettingsDemoTests
```

The absent lane is the existing isolated `out/sdk-api-verification/input-absent`
project with no Plugins directory. Unity runs EditMode with testFilter
`HumanVision.Input.Tests.RtspSourceTests.MissingInputPluginIsActionable;HumanVision.Input.Tests.AndroidRtspOutputSizeTests`.
Use the actual Python3.13 executable for `tools/maintenance/check_architecture_boundaries.py`,
`tools/package/check_public_surface.py`, and
`tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input`.

## Connected-device comparison and delivery

User authorized the connected device test using the already running PC RTSPServer
and VLC. The device is OnePlus LE2120, Android14/Adreno660, not RK3588. Sequential
Fix1/Fix2 installs use the same PC H.2641280x720 stream, medium640x384 model,
MaxBodies4, no regions and requested640x480. Settings are backed up/restored;
Fix2 remains installed with the original auto-start=false setting. PC server/player
configuration was not changed.

| Metric | Fix1 | Fix2 |
| --- | ---: | ---: |
| Actual output | 1280x720 | 640x360 |
| Active seconds | 227.710 | 292.621 |
| Unity median FPS | 60.091 | 60.122 |
| Publication whole-phase FPS | 22.485 | 28.826 |
| Complete-result whole-phase FPS | 15.177 | 15.952 |
| Body-result age median/P95 ms | 148.584/185.119 | 148.741/182.856 |
| Warmed body samples | 216 | 52 |
| Pipeline errors | None | None |

The geometry log and actual CSV confirm640x360 output; genuine Fix2 joint records
confirm recognition. Its final screenshot has no person and is not overlay proof.
Publication means differ, but human coverage/network fluctuations differ between
sequential runs. Neither substantial latency improvement nor RK3588 smoothness
acceptance follows from this comparison. Fresh hand/30FPS requirements remain open.

Local APK: `E:/UnityProject/Human-Vision-SDK-Test/Builds/HumanVisionSettingsDemo-RK3588-Fix2.apk`,
272,330,313 bytes; SHA256
`f4839c0c3d2d8e3ddbb4b156b00e34b374a74078a60540dcd44c4632e5a4066c`.
SDK native SHA256 remains
`d86a6acd5356ca1c7b39c6b277b7fc910db82a38a6353ccb90a87301f8283960`.
Embedded local SDK/Input versions are0.4.0-preview.6.rk3588fix2 /
0.1.0-preview.4.rk3588fix2; PackageCache is untouched. RK3588 retest should first
keep the original URL/model and select720p, verify the output log, then separately
compare low quality and an actual720p camera substream.

The later root-cause investigation and dedicated per-layer diagnostic APK are
documented in [skeleton FPS bottleneck](2026-10-08-skeleton-fps-bottleneck.md).
Normal Fix2 remains restored in the test project and on the connected phone.
