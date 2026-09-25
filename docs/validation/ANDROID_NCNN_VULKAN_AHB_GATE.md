# Android NCNN Vulkan AHB gate — device acceptance pending

This is the Milestone B7 development gate for the existing
`HumanVisionCameraDemo` scene. The generated gate component uses the live
camera, the production Unity Vulkan producer, its persistent three-slot AHB
ring and the B5 ncnn backend's cached AHB import, GPU preprocessing and
explicit FP16 pack4 conversion. It emits no skeleton. The test-only input
contract is generated at `out/android-gpu-gate-runtime/input-contract.json`;
there is no detector, pose model, model output, or substitute production
ModelPack. The strict production NCNN build validator still rejects missing
Milestone C assets. The gate symbols exist only in an Android native build
configured with `HV_ANDROID_GPU_GATE=ON`; the default is OFF.

## Build and artifact

From the repository root:

```powershell
pwsh -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo
```

The script builds Android ARM64 API 26 native code with the gate flag, copies
it into an ignored Unity verification project, builds a Development/IL2CPP
player with Vulkan first and `android-ncnn-vulkan` metadata, then checks the
APK for ARM64 native libraries. It prints the APK SHA-256. It never creates or
commits a fake production NCNN model pack.

Current host build: Unity 2021.3.45f1, Android API 26 minimum, ARM64,
IL2CPP, Vulkan, Development. APK:
`out/android-gpu-gate-runtime/humanvision-gpu-bridge-gate.apk`.
The host build establishes compilation and packaging only. Re-run the command
before device acceptance and record its fresh hash with the Git commit.

## Device collection

The user runs this on the Snapdragon 888 target, with USB debugging enabled:

```powershell
pwsh -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b
```

During the ten minutes, show portrait, landscape-left and landscape-right;
pause/resume and background/foreground the app; tap **Restart camera**.
The collector installs and starts the APK, saves full logcat, device identity,
the exact APK hash, the current commit and a machine-readable report. A
`PASS_CANDIDATE_REQUIRES_USER_REVIEW` result is only an automated screen;
the user reviews and returns `report.json` and `logcat.txt` before B7 can close.
The collector now requires progress in both imported and converted counters,
`error=<none>` on every status line, a selected path consistent with the
actual AHB and Vulkan image usage, and no native or Unity fatal log. It also
requires portrait and both landscape orientations, pause/resume, explicit
background/foreground focus events, a camera restart request, and a subsequent
source lease resumption after both pause and restart. If any evidence is absent,
the report is `FAIL`; the device run can be repeated after correcting the
capture sequence. The `tools/test/test_android_gpu_bridge_gate_analysis.ps1`
fixture exercises these rejection cases on the host.

The collector captures unfiltered `main`, `system` and `crash` logcat
buffers, so libc and crash-dump fatal records remain visible alongside Unity
status. Every status carries a logcat epoch timestamp. A pass candidate now
requires status within 30 seconds of capture start and end, at least 570
seconds between first and last status, and no status gap over 60 seconds.
Imported and converted counters must increase again in the last 90 seconds
and within 30 seconds after each pause or camera-restart recovery marker.
The gate escapes nonempty errors onto one line; a missing clean sentinel or
any nonempty error fails the screen. These checks expose stalled or truncated
collections, while the user still inspects raw device behavior.
An error-severity Unity log or a gate `InvalidOperationException` also fails
the screen, including source-lease, submit and startup exceptions. Ordinary
Unity warning lines do not fail on their own.

Review the raw log for:

- `AHardwareBuffer_describe` actual width, height, layers, format, usage and
  stride, plus `VkAndroidHardwareBufferFormatPropertiesANDROID` concrete and
  external format and feature values;
- `vkGetPhysicalDeviceImageFormatProperties2` result, external memory features,
  compatible handle mask and rejection reasons for each candidate attempted;
- selected blit or color-attachment path and proof it matches the measured
  source usage/features and AHB image usage;
- nonzero, exact Unity/ncnn device and driver UUID pairs, `consumer`
  sampled-only image usage (`4`), increasing imported/converted counts, slot
  drops and any lifecycle errors;
- the gate component's source guard against `AsyncGPUReadback`, `GetPixels`
  and `ReadPixels`, plus device-side converted-frame progress. These are
  combined evidence; neither source inspection nor a counter alone proves
  zero CPU readback across every dependency.

The Revision 2 B2 probe first allocates sampled-only AHB and selects blit if
that measured candidate passes. It allocates the sampled + color-output
candidate only when blit fails. A color candidate absent after successful
blit is **N/A by design**, not a missing measurement. If blit fails, both
candidate query results and rejection reasons must appear. A selected blit
without actual `TRANSFER_DST` support, a selected color path without actual
color-attachment support, no viable path continuing initialization, a UUID
mismatch, a render-thread inference wait, or a full-frame CPU readback fails
the gate.

## Acceptance record

| Item | State |
|---|---|
| Host native and Unity build | Pass; see `docs/DEVELOPMENT_STATUS.md` |
| APK hash and commit | Record fresh values after final B7 commit |
| Device model / OS / GPU / driver | Pending user evidence |
| Actual AHB format/usage/features and chosen path | Pending user evidence |
| 10-minute slot, drop and lifecycle record | Pending user evidence |
| Orientation, pause/resume, foreground/background, restart | Pending user evidence |
| Final B7 acceptance | **Open** |

No detector/pose conversion, Milestone C, or D work is authorized by this
host build.
