# PC responsive GUI Task 1 — 2026-10-01

## Scope and implementation

Isolated worktree `codex/unity-pc-demo`, base 5accffb. Bounded user-requested PC
GUI fix; no inference/public SDK/renderer/native/model changes. Native receipt
`out/pc-demo/native-inputs.json` retained unchanged; eight-DLL provenance and all
model/profile source hashes equal base package. No original user Unity project
or Android project/worktree operated. No main merge, push or Release performed.

PC GUI now uses 1280x720 reference units: automatic scales1/1.5/2/3 at
720/1080/1440/2160p. Finite DPI72–384 adds density scaling, unknown/implausible
DPI falls back to resolution. Serialized `uiScale` defaults1, validated/clamped
.75–2. Small window effective scale is limited by width/320 and height/160.
Safe-area bounds convert Unity bottom-left safe pixels into GUI top-left pixels.

A privately cloned skin is cached once and destroyed with the component.
18-unit wrapped labels,44-unit buttons/toggles,42-unit textfields,36-unit slider
and26x32 thumb,22-unit scrollbar; narrow choices stack vertically. Panel is at
most520referenceunits wide. Expanded content including errors/status/diagnostics
uses a vertically scrolling viewport bounded below its header. Collapse keeps
only64referenceunits of settings header, with shorter titles in narrow windows.
GUI.matrix transforms drawing and Unity's IMGUI event hit testing together;
Event.mousePosition is intentionally not transformed again. Finally restores
incoming matrix,skin,color,backgroundColor,contentColor,enabled, without mutating
shared skin styles. All existing source/backend/path/capacity/start-stop/preview/
overlay controls retained. Collapsed diagnostics remain accessible by expansion.

## RED/GREEN and commands

Tests were added before policy implementation, copied into existing isolated
`out/pc-demo/task1/unity-tests` project. Unity2021.3.45f1 ran real EditMode tests
with process-scoped __COMPAT_LAYER=RunAsInvoker, Start-Process -WindowStyle Hidden;
no global compatibility settings changed. Preserved old logs.

Command arguments: -batchmode -nographics -projectPath <isolated project>
-runTests -testPlatform EditMode
-testFilter HumanVision.Tests.HumanVisionPcDemoPolicyTests
-testResults <XML below> -logFile <matching .log>.

RED `out/pc-demo/gui-responsive/layout-red.xml`:14tests,5passed,9failed with
expected missing PcGuiLayout assertion. GREEN layout-green.xml:14/14passed.
Final layout-final.xml:14/14passed after tiny-window fit/viewport refinement.
Cases cover720/1080/1440/2160p,portrait720x1280,320x240,inset safe-area bounds,
expanded/collapsed panels,resolution/DPI monotonic scaling,invalid NaN/infinite
DPI and overrides,clamped preferences,and bounded small scroll viewport.
Existing acquisition clock/configuration/generation/fresh-observation tests pass.

`powershell -File tools/package/compile_managed.ps1`:PASS (Runtime,Demo,Editor,
Android conditional Demo); final output gui-responsive/managed-final.log.
Existing serialized-field CS0649 warnings retained.
`py -3.13 tools/test/test_pc_demo_package.py`:11/11PASS using real payloads.
`py -3.13 tools/package/package_pc_demo.py --native-inputs out/pc-demo/native-inputs.json`:
PASS,153indexed assets; repeated final generation byte-identical manifest.
`py -3.13 tools/maintenance/check_architecture_boundaries.py`:PASS.
`py -3.13 tools/package/check_public_surface.py`:PASS.
`git diff --check`:PASS.

## Package and remaining gate

Updated deterministic generator and PcDemoAcceptance expectation to0.4.0-pc.2;
regenerated dedicated PC UPM and installation docs only. Asset manifest SHA256:
cb116e94439c99bd99b6665458e124f424d35c0eec28c3b45afedac5e4a1a7f4.
Source helper metadata has a separate GUID from generated UPM metadata.

Root owns actual Windows Player captures at1080p/4K/narrow windows, interaction
checks and independent review before push/pinned Git delivery. Policy/compile
success does not assert actual rendered/pointer behavior or hardware performance.
No rerun of unchanged inference FPS. Root's scratch capture project points to
updated local dedicated UPM. No release/publication acceptance claimed here.
