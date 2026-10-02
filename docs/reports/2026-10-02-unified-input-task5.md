# Unified-input Task5: Android hardware input capability gate

2026-10-02. Base `e1f5c73d968290d514e240954e7469a47ab0c025`.
Review/commit status is recorded in DEVELOPMENT_STATUS and the SDD ledger.

## Native boundary and actual initialization

The independent API26/ARM64 input library receives compressed H.264 RTSP/TCP
packets and uses MediaCodec Surface output with AImageReader PRIVATE/GPU sampled
usage. The capability probe acquires three actual images asynchronously, borrows
their AHBs, takes explicit references, queries actual Vulkan properties and returns
the original acquire fences when retiring images. It performs no GPU sampling,
decoded-plane mapping, RGBA conversion, AHB lock or full-frame CPU readback.
No recognition library/model is loaded. Public Task3 ABI remains unchanged;
Windows decoding and old ABI behavior retain their regression tests.

The initial device gate genuinely failed: Unity's successful default Vulkan
device creation did not enable YCbCr, AHB or sync-fd support. The failed APK,
logs and original native bytes remain retained. With a recorded root ruling,
the independent preinitialization hook explicitly requests physically supported
required capabilities. API promotion/dependencies are checked; original Unity
extensions and feature chains remain intact. Only a successful creation matching
the actual Unity VkDevice and physical device proves logical enabled support.

Review found order-dependent admission of conflicting/duplicate YCbCr declarations.
A shared production validator now rejects duplicate relevant sTypes, sampler plus
Vulkan11 aliases, and a disabled single declaration before queries/device creation.
It reads actual Vulkan structures without modifying them. Valid absence can prepend
one supported feature; a valid enabled declaration passes through unchanged.

## Actual tests and device result

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
pwsh -NoProfile -File tools/test/collect_android_input_gate.ps1 -Gate Capabilities -Serial e7c07019 -Output out/input/task5-round1-device
py -3.13 tools/maintenance/check_architecture_boundaries.py
```

Final native build/API26 strong-import audit exit0. Windows CTest23/23 exit0:
the original8 input/legacy regressions,4 capability policy cases and11 declaration
cases. The declaration regression first produced6 failures/5 passes against the
extracted original production guard, then passed11/11 after the minimum fix.
These are actual Windows host binaries, not Android ELFs executed on the host.
Architecture/public-surface checks exit0/PASS. Locked FFmpeg and NDK23.1 sources,
headers, native closure and APK entries have recorded hashes; no toolchain upgrade.

Final actual Snapdragon888 gate exit0/PASS:
`out/input/task5-round1-device/20261002T0712229308194Z`.
Native SHA256:
`b485413ecbdc4a41d02ef682aad89df28315e9535deba855983df999b477b6bd`.
APK and pulled installed-base.apk SHA256 both:
`d854cee8d1e649aad45a95ac35036aa23fba286d212c12c4243d5b0ea50d0916`.

Actual hardware codec `c2.qti.avc.decoder`, controlled H.264/TCP640x360 source.
For each of three decoded images: AHB format2141391878, usage805372160,
VkFormatUNDEFINED, externalFormat506, formatFeatures9433217, sampled capability1.
Physical YCbCr/sync-fd and proven logical YCbCr/sync-fd/AHB are1. Original20 device
extensions become22; Vulkan1.1 supplies promoted prerequisites. YCbCr model3/range1,
chroma offsets1/1; sync-fd import/export supported. Each actual acquire-fd is-1,
explicitly already_complete. Every image return and decoder close records zero
active images/AHB references/owned fds. Owned helpers and ADB reverse are removed.

13 source and73 artifact hashes match the round1 freeze; all34 protected R4 files
remain unchanged. Original12 source/81 artifact evidence is retained, including15
mutable outputs copied before rebuilding. The round1 report's 'unchanged9 files'
is a prose counter typo:12 originals minus4 modified equals8 unchanged, plus1 new
helper makes13 current files. Authoritative maps and the five-file diff agree.

## Remaining gates

This is a bounded query-only capability probe, not production RTSP texture output.
GPU import/color, positive-fd waits, ownership transfer and the three-slot lifecycle
remain Tasks6/7. The enabled extension list lacks VK_EXT_queue_family_foreign;
physical support and actual logical enablement must be gated before FOREIGN_EXT
ownership/real sampling. AHB formats/usage are measured, not assumed RGBA or
TRANSFER_DST. No inference performance, sensor-age or thermal acceptance follows.
The test codec identity query uses API29 on this Android14 device; comprehensive
production codec selection on API26 is not qualified by that test probe.

Current camera raw skeleton remains about11FPS;25 interim/30 fresh complete
observation frames/s is unmet. User Unity project/caches are preserved.
No final physical acceptance, main merge or Release.
