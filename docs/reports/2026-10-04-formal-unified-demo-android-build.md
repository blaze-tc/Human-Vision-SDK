# Formal unified Demo Android player build

After guard commit `9d75379`, a fresh isolated Unity2021.3.45f1 project built the
official Video, Camera and RTSP scenes with immutable local package v13. It uses
the approved YOLO640 FP32 Android profile, current GPU bridge/native libraries
and enabled MeshRenderer points/LineRenderer bones. No private diagnostic
renderer, forced adapter Tick, runtime scheduling change or backend fallback.

## Build and artifact evidence

Project: `out/input/formal-android-oct04-v13`.

Actual command: Unity `-batchmode -quit -force-d3d11 -buildTarget Android
-projectPath <project> -executeMethod FormalAndroidBuild.Build
-logFile <project>/unity.log`. The launch/exit receipts and complete log remain
in that project. Result: Succeeded, process exit0, build errors0/warnings0.

- APK: `player/HumanVisionFormal.apk`, 82,947,397 bytes.
- SHA256: `4a521d82de38377c2fbd997f7b2732baeda94041f53edd67b5b08be7300ac868`.
- Package: `com.blazetc.humanvision.formaldemo.oct04` (separate test application).
- API26 minimum, ARM64 only, IL2CPP, manual Vulkan-only graphics APIs.
- Android manifest runtime mode and profile: `android-ncnn-vulkan`.
- Autorotation enabled for portrait and both landscape directions.
- All11 indexed runtime files verified directly from APK, index hash
  `89703ad345f2b7a954b6711f8e65ffbd02e5f80cbf1c653d6815a53395ee3cfc`.
- SDK native `fef5fda3952d21e68b40052cc8804adcd99bc9d961429daf868afd3650bbbd96`;
  Input native `d3968a4f12d1cf7882c04312c7f0ad095c4a18b894846e2beafb758e9f582628`.
  Both match the qualified package byte for byte.

`qualified-apk.json`, `apk-badging.txt`, `apk-manifest-tree.txt` record closure
and compiled manifest evidence. BuildReport's329,588,580 bytes describe its
build content accounting, not APK file length. The independent review is
`.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/android-build-contract-correction/formal-build-review.md`.
It passes actual build/deployment closure only.

## Physical run pending

Installation on connected Snapdragon888 device was attempted but remains
pending behind the device lock screen. The user has been asked to unlock and
allow installation. No installed APK hash, device initialization, video
skeleton observation or performance acceptance is claimed.

The public Video mode requires a readable MP4 path. Prepared test settings in
`device-settings/` select the user's original `video-1.mp4`, MaxBodies8, regions
disabled and9/27-pixel line/point style in this separate app. These settings
have not been pushed to the locked device. Original video SHA256 is
`e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`.
The source is25FPS and cannot establish30 fresh complete observations/s.

After unlock: verify installed APK bytes, push the original source and public
mode settings, open Video and check image/skeleton alignment; then validate
Camera and real RTSP using the same official scene navigation. Raw observations,
coverage, clocks and age need valid additive recording evidence. Existing
invalid private-harness measurements remain invalid. Human motion and final
30FPS physical acceptance remain open; no main merge or Release.

The user's current PC project remains intact and is playing the official Video
scene. A fresh v13 screenshot and sampled15s record are retained in
`production-correction-user-integration-v13/` under the ledger; these are local
appearance/diagnostic evidence, not Android acceptance.
