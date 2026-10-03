# Task10: independent input package and three unified Demos

The reviewed candidate provides Camera, Video and RTSP scenes, common navigation,
shared recognition configuration and separately persisted source settings. The
input package previews sources without loading SDK inference libraries or models.
Task10 final package and evidence qualification passes scoped spec and code-quality
review. Overall skeleton performance acceptance remains outstanding.

The Demos reuse the existing skeleton overlay and public body/region API. Thickness
uses the 1280x720 display reference rather than source texture dimensions. Common
controls use uGUI scaling, safe areas, scrolling and collapsible settings. Region
editing blocks pointer events originating on visible controls. Regions retain the
approved post-inference assignment; there is no CPU image mask or claim of reduced
fixed-model inference cost. A separate GameObject/LineRenderer renderer and strict
image-region cropping are not implemented by this task.

## Verification and repaired regressions

Focused settings/analysis checks pass 3/3 in each qualified package layout;
installer/runtime package checks pass 8/8 after meaningful failure cases. Real
input-preview PlayMode checks pass 2/2. Source clock mapping passes 7/7; style and
reference-width checks pass 8/8 and 9/9 respectively. Real public facade joint,
event and detach checks pass 1/1. Existing failure artifacts remain preserved.

Actual clean imports exposed missing CanvasRenderer admission, different CPU
timestamp epochs, lost facade subscriptions, diameter/radius confusion and pointer
events leaking through controls. Their bounded fixes preserve the V1 ABI, source
age and generation semantics. Unknown or invalid clocks are rejected; the code
does not label an old result fresh or extend prediction to disguise low FPS.

Package generation uses the reviewed SDK UPM payload as authority. The offline
layout includes the exact native-symbol audit and retains model/profile/hash
validation. Source-owned SDK folder metadata and separate RuntimeData GUID domains
survive regeneration. The actual export regression passes 9/9 after recorded
failures. FFmpeg has one owner, the independent input package. All runtime modes
remain explicit; NCNN failure does not switch to ORT.

## Final native input recovery

The old production input APK failed at its 17th new AHB object after 1,317
completed GPU copies, approximately 72.8 seconds. That precise failure is retained;
the cause of a still older 15-second failure remains unknown.

The fixed cache supports 64 imported object identities and fails explicitly at
65. Cache snapshots and the known-reader notification registry share that domain.
Known notifications reserve identity before import and coalesce without allocation.
Unknown notifications use a separate bounded counter. GPU fence retirement,
ownership transfer and sync-fd handling remain intact. No whole-frame CPU readback,
per-frame import creation, LRU replacement or backend fallback is introduced.
API26 ARM64 cache and failure storage increase by 9,600 bytes and the fixed registry
adds 1,600 bytes. Opaque driver-resource memory has not been measured.

The actual old-cache 17-object test fails before repair; final focused native
checks pass 15/15. Fresh source spec and code-quality reviews pass. Android API26
ARM64 production exports remain 42; Windows exports remain 8. The unchanged
Windows target reused qualified objects, while Android was rebuilt.

## Final builds and physical evidence

The immutable final candidate contains 1,221 files. Its source map SHA256 is
`af2188cd71b8927ca1b452316ef51b59efefce8af323397abc666f0c449b1563`.
The scoped change map contains 195 entries including 20 migrated payload deletions,
with no undeclared paths. Original unrelated R4 edits and caches are preserved.

Actual independent Input Android and Combined Windows/Android builds exit zero.
The final Combined APK is 58,146,139 bytes, SHA256
`a7dc61490b2b266892db391426a0c5f964ba012024d396226846e43f054549c0`.
Its eight required native libraries, API26/ARM64 metadata and explicit NCNN profile
match the retained audit. The SDK inference binary remains unchanged. The final
Input APK is 32,981,642 bytes, SHA256
`3641cc0af86de958c55bdf54494a43e3a692f4b636a213057e63746faef037dd`.
It has six required native libraries and no SDK inference/model payload. Installed
and pulled APK bytes match. Input native SHA256 is
`d3968a4f12d1cf7882c04312c7f0ad095c4a18b894846e2beafb758e9f582628`.

On Snapdragon 888, the independent Input APK completes two real controlled RTSP
runs with completion spans 194.859 and 119.213 seconds. The second run follows
public Stop, Mirror and Start operations. Submit/complete sequences are continuous
1..4650 and 4651..7526. Published prefixes are 1..4649 and 4651..7525: each Stop
suppresses the terminal completion, rather than publishing a retired frame.

Each Stop balances cache extra-reference acquisition/release and imported GPU
objects (15/15, then 14/14), with cache/registry live, reserved and pending counts
zero. Source views, pipelines, descriptor pools and three target views balance.
Separately, decoded image leases, AHB leases and owned fd counts reach zero.
There is no first cache/copy/decoder failure in these runs. The owned publisher,
logger and USB reverse are removed and the app is left stopped.

Physical exposure is only 15/14 imported object pointers, not a device proof of
17/64 simultaneous objects or underlying allocation IDs. The 17/64 behavior is
covered separately by host tests. Rotation is zero in these landscape runs and
has not received physical rotation acceptance. Unknown callback counts are Stop
snapshots: AImageReader deletion can defer its destructor, so they do not prove
universal callback quiescence. Client ownership counts do not measure all opaque
NDK/driver ownership.

This fixture is 640x360 at 25 source FPS. Request 30 shown in the HUD is source
configuration, not achieved input FPS or skeleton FPS. Input-only evidence contains
no model inference and cannot establish 30 complete fresh observations/second.

## Current open Unity project

The authorized `E:/UnityProject/Human-Vision-SDK-Test` imports immutable final local
packages from `out/input/task10-user-packages-v9`. Manifest and resolved lock both
identify them. The scene is HumanVisionVideoDemo, actual VideoPlayer input is
1024x576 and current public facade/recognition snapshots show four sampled bodies
and four users in the existing four configured regions. Result age in the captured
sample is 260.333 ms. Actual 1280x720 screenshot shows upright video, colored bones,
readable navigation/settings/status and numbered region borders. Console errors
are zero. Actual 4K was qualified earlier on the unchanged UI surface.

These snapshots prove current binding and visibility, not smooth motion or a
sustained performance benchmark. The user's existing source settings and regions
are retained. The private Editor acceptance helper is excluded from shipped
packages and production APKs.

## Reproduction and retained evidence

Use a fresh output directory for each build:

```powershell
pwsh -NoProfile -File tools/test/task10_verify.ps1 -Kind Combined `
  -SourceRoot out/input/task10-stage3-combined-candidate `
  -Output out/input/task10-stage3-combined-windows -Action Windows
pwsh -NoProfile -File tools/test/task10_verify.ps1 -Kind Combined `
  -SourceRoot out/input/task10-stage3-combined-candidate `
  -Output out/input/task10-stage3-combined-android -Action Android `
  -RuntimeStage out/input/task10-combined-android-schema-Combined/Assets/StreamingAssets/HumanVision/Runtime
```

The explicit local Android runtime stage contains 21 indexed files with index
SHA256 `d4cd23e68f3a9a3ea496e32201a3b8e82c9d273a495c97752aeb1abdf7ca2526`.
It is a retained local evaluation fixture, not a new default model or public
weight qualification. Its original staging command parameter is unknown. The
packaged RuntimeData index has 23 entries and is distinct from this stage.

Raw device evidence: `out/input/task10-ahb-stage3-device/continuous-logcat.txt`
SHA256 `a0029922c7600b0ece413be54b771b8cc2e89b5fb3eaac392c32aa4ceb5fccac`;
summary SHA256 `e309225f35b0f931af06faf8cb420a143f0e95a62d811b9f9dbe2f79243a273e`.
Full frozen sources, failures, primary platform references, scoped reviews,
build audits and protected-file receipts remain under the plan's ignored
`.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/task10/` ledger.
Seven Unity-created Input folder metadata files are recorded separately from the
original 148-file Input source map; the original map and tgz are not rewritten.

Final local exports are in `out/input/task10-stage3-final-export`: offline
unitypackage SHA256 `ab526424276ef6eb95bf1b959a45e1d3b24c2d7e63dcd44f0c11f23bfbe66b0b`,
ZIP `ff2f0ef5dd54661498d7462f98dcc7a858acf4efd05ece9ffa4df80cd24f70ef`,
SDK tgz `e4974b34b25c720eb7b84b1c15f3aaf7dd310e128d986d19c93d41e0993c2261`
and independent Input tgz
`7c7c09abe18f5f4b1c68e26ce7e2e351ab95f670da1fbff7975d52123d8e3538`.
The offline package contains 176 unique asset GUIDs; the UPM pair has 147 SDK and
62 Input GUIDs with no collisions. All 23 packaged runtime-index entries match.
The final Input tgz contains 112 files including 12 folder metadata files, all
equal to the full frozen source. Artifact generation does not publish a Release
or qualify a previously missing physical acceptance gate.

Task11 must collect source-separated fresh complete observations, age, drops,
thermal status, direction and actual motion follow-through. The 25/30 FPS goals
remain FAIL, and final user physical acceptance remains outstanding. No main
merge or Release is authorized by this task.
