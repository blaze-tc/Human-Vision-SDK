# PC evaluation Task 3 local verification — 2026-10-01

Independent spec/code quality review: PASS. A new reviewer thread was refused by
agent thread limit; an available independent non-implementing reviewer performed a
fresh bounded PC task review, recomputing evidence and viewing the actual render.
No review checks were skipped. Source/native/API/model/renderer boundaries preserved.

Actual clean project: `out/pc-demo/local-import-reviewed-final`, Unity2021.3.45f1,
launch09:12:59 UTC, observations started09:13:22.5769875 UTC. User's original open
project was not modified. Windows Direct3D11 Unity graphics reports RTX2060;
requested and actual model backend are DirectML, body.onnx DirectML -> DirectML.
This is the existing Windows RTMO-t416 route, different from Android YOLO.

Real video1 SHA256 e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8,
1024x576,25FPS, starts37s. Sample log has417 distinct sequences AND source frames:
414 count-seven and3 count-eight positive observations in20.0506936s.
Arrival FPS=(417-1)/20.0506936=20.74741195; source age P50/P95=104.1299/176.4375ms
(order statistic). All417 sampled positive observations are presentation-eligible
and have real overlay vertices/upright checks. Collection excludes empty observations;
this report does not establish zero empty observations. A screenshot rendered the
same live Canvas/preview/overlay/camera into1920x1080 because batch Editor does not
present Game view: seven visible aligned skeletons/boxes/IDs, viewed independently.
This proves bounded demo display, not full anatomical accuracy or30FPS acceptance.
25FPS input cannot establish30 fresh complete observations/s.

`pc-demo-acceptance.json`: Stop clears manager, source and preview; missing path
rejected; explicit CPU capacity1 restarts with actual CPU and real video2 inference.
`pc-demo-installation.json`: seven installed runtime hashes and independent GUIDs
verified, nativeSHA8c5b9bb56f6bae8f0dc99d9d6d5cf172ab1b2edc62e8f54cc12b1383c2509180,
indexSHA0f8cc29e4c8ecf28cacea6d37682077420bff69720f071bbf5319334df02da33.
Earlier green3 module-path snapshot confirms localUPM paths of all three inference
libraries with identical binaries; it is not mislabeled as the final-run snapshot.

`pc-demo-build.json`: Win64 Mono player Succeeded,09:14:11.7190869UTC,
17.2533024s,208312253bytes,0warnings/0errors; eight native DLLs present and player
humanvision.dll hash equals package/pinned current clean native build. Standalone
execution and camera/RTSP hardware acceptance remain user tests; Editor real-video
execution was automated. No claimed same-model PC-versus-Android performance win.

Observed clean-import compile RED: SceneControls required DiagnosticsText from
HumanVisionHud.cs. Minimal closure correction includes this existing standard HUD
source without attaching it; Android development gate remains excluded. No shared
source behavior changed. Native diagnostics use an unrelated steady-clock epoch for
sampling and use a source-sequence FPS estimate; PC HUD filters those age/state/count/
RawFPS fields and displays its own monotonic source age and unique-arrival FPS.
Each new policy defect recorded actual UnityRED5/6 thenGREEN6/6. Latest:
`out/pc-demo/task1/unity-tests/fps-green.xml`,09:17:33–34UTC,6passed/0failed.
Final managed Runtime/Demo/Editor/Android conditional compilePASS;
`py -3.13 tools/test/test_pc_demo_package.py` root final11/11PASS4.580s;
public surface/architecture checksPASS. Final display-only RawFPS line filter and
installation documentation updates postdate full GPU run; inference/native/source
clock/model/data are unchanged. Full tests not falsely attributed to later edits.

Final151-asset package manifestSHA:
83b7674524b03fc71ec8fc50bf60e4d9bb8f4f754f9c0017964e2175df8d8e28.
Only dedicated codex/unity-pc-demo branch publication is authorized. Fresh immutable
remote Git import remains the next gate; no main merge or Release.

Reproduce local automated run (new scratch directory; local video paths are in harness):
`pwsh -File tools/test/run_pc_demo_acceptance.ps1 -EvidenceName <new-safe-name>`.
Launcher returns process identity; inspect fresh pc-demo-pass.txt, installation,
acceptance/build JSON and unity.log. Process-scoped RunAsInvoker bypasses this host's
RUNASADMIN setting without editing the registry or existing Unity processes.
