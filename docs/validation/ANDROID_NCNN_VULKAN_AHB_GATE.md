# Android NCNN Vulkan AHB gate — accepted on Snapdragon 888 (2026-09-25)

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
APK against the recursive ELF `DT_NEEDED` closure of `libhumanvision.so`.
The check resolves Android system libraries against the API 26 NDK sysroot,
copies required third-party libraries from `out/live-deps`, fails on missing
dependencies, and verifies the APK library bytes against their source hashes.
It prints the APK SHA-256. It never creates or commits a fake production NCNN
model pack.

Current host build: Unity 2021.3.45f1, Android API 26 minimum, ARM64,
IL2CPP, Vulkan, Development. APK:
`out/android-gpu-gate-runtime/humanvision-gpu-bridge-gate.apk`.
The host build establishes compilation and packaging only. Re-run the command
before device acceptance and record its fresh hash with the Git commit.

## Device collection

The user authorized direct ADB collection on the Snapdragon 888 target. The
accepted run used USB debugging and this command:

```powershell
pwsh -NoProfile -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b -Serial e7c07019
```

During the ten minutes, show portrait, landscape-left and landscape-right;
pause/resume and background/foreground the app; tap **Restart camera**.
The collector installs and starts the APK, saves full logcat, device identity,
the exact APK hash, the current commit and a machine-readable report. A
`PASS_CANDIDATE_REQUIRES_USER_REVIEW` result is only an automated screen;
the raw `report.json` and `logcat.txt` were also inspected before accepting B7.
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
The gate emits the complete B2 measurement once per source generation as
separate `HV_GPU_GATE probe generation=N` lines. A source lease token prevents
an old measurement from being logged while the next source is pending, even
when the two measured descriptions are identical. The collector requires one
complete probe block after each `source rebuilding generation=N` marker and
before that generation's first configured status. A rejected blit candidate remains visible
when the color-attachment fallback succeeds. Routine status lines carry
`error=<none>` while the selected bridge and consumer run; producer, bridge,
configuration and consumer failures remain in the error field or raise a gate
exception. The collector permits `result=1/path=0` with zero AHB fields and
UUIDs while the initial measurement is pending. Later pending measurements
require an explicit `HV_GPU_GATE source rebuilding` marker and must recover
to a configured path within 30 seconds. An unmarked path-zero fallback or a
non-pending path-zero status fails. The selected nonzero path must match the
actual contract and UUIDs. The gate escapes nonempty errors onto one line; a
missing clean sentinel or any nonempty error fails the screen. These checks expose stalled or truncated
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
| Host tests and APK build | Native 196/196, Unity EditMode 75/75, analyzer fixtures 37/37, architecture guard and seven-library APK closure pass; see `docs/DEVELOPMENT_STATUS.md` |
| APK hash and tested commit | SHA-256 `42de1691606e76e99aec8db75ece8ca7ec20c3ead1c54cea6ac068610fef88ce`; `147ed1d5d0f1c9eab3e18145a0b8077e80e405f6` |
| Device model / OS / GPU / driver | OnePlus 9 Pro `LE2120`, Android 14, SM8350 Snapdragon 888, Adreno 660, Vulkan 1.1.0 `[512.530.0]`; ADB serial `e7c07019` |
| Actual AHB format/usage/features and chosen path | Six generations measured format `1`, actual usage `0x100`, format features `0xFFD83`; path `1` (`vkCmdBlitImage`) selected from actual support; producer image usage `6`, ncnn consumer sampled/read-only usage `4` |
| Device identity | Nonzero Unity/ncnn device UUIDs equal (`43510000050000009402500014009402`); driver UUIDs equal (`03000000000000000000000000000000`) |
| Ten-minute lifecycle | `10.003` minutes, 2,136 status lines, maximum status gap `7.678` seconds, six complete per-generation probe blocks, final submitted/imported/converted `11992/11991/11991`, `noSlot=6`, `generationDrops=14`; zero status errors or native/Unity fatals |
| Orientation and recovery | Portrait, Unity `Landscape` (left alias), LandscapeRight; pause/resume, focus background/foreground and camera restart all recorded, followed by renewed import/conversion progress |
| Final B7 / Milestone B engineering gate | **Accepted** after automated 23/23 checks and raw-log review; production skeleton/device performance acceptance remains in Milestones C/D |

Evidence: `out/device-gates/milestone-b/report.json` and
`out/device-gates/milestone-b/logcat.txt` (local ignored artifacts). The report
result is `PASS_CANDIDATE_REQUIRES_USER_REVIEW`; it is recorded here as accepted
only after the separately inspected raw log showed 2,136 status lines, six
generation-specific blit probes, rising imported/converted counters near the
end, and no gate error or fatal. The report's `selected_paths=[0,1]` includes
the permitted initial pending measurement (`result=1/path=0`) before the
measured blit configuration. The sampled-only AHB producer/consumer probes
reported successful external image queries and imports on every generation.
The color-attachment candidate was not allocated because the measured blit
candidate passed; that candidate is N/A for this device run.

The first ten-minute collection at `0b0b201e5deef5aa6cfe28eff20553bf56234a02`
is preserved under `out/device-gates/failed-slot-busy/`. It failed
`pending_source_measurements_recover` and `no_status_error` after transient
slot-publication contention produced `path=0` and `error=Unity Vulkan slot
transition/publication is pending recovery`. Commit `147ed1d` repaired that
race. The accepted rerun has no such error.

The gate does not load detector or pose models and emits no skeleton, so these
figures do not establish 30 complete observation frames/s, end-to-end pose
age, or multi-person tracking. The gate component's source guard excludes
`AsyncGPUReadback`, `GetPixels` and `ReadPixels`, and device conversion
progress confirms the configured GPU path in this test; the combined evidence
is the B7 bridge criterion rather than a claim about all future dependencies.
Milestone C is next; no Release or main merge is authorized by this gate.
