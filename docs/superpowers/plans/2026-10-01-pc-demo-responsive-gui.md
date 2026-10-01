# PC Demo resolution-aware GUI fix

User observed unreadably small GUI at high PC resolution and authorizes
resolution matching, with uGUI allowed if needed. Fix only Demo controls.
Keep native/model/public API/recognition/rendered skeleton behavior unchanged.
Do not modify user's project without backup. Worktree codex/unity-pc-demo.
No main merge or Release. Continue GPT-6.1 Sol medium implementation/review.

### Task 1: Readable, responsive PC Demo controls and package update

Own Demo/PC/HumanVisionPcDemo.cs and focused layout helper if needed,
HumanVisionPcDemoPolicyTests.cs, tools/package/package_pc_demo.py VERSION,
generated upm/com.blazetc.humanvision.pc-demo and installation/report docs.
Root owns scratch UI capture harness and actual Unity runs.
IMGUI may be retained if reliable; no need to replace whole demo with uGUI.
Use reference-space/DPI-aware sizing, a serialized reasonable UI-scale override,
large consistent buttons/toggles/textfields/slider and readable wrapped labels.
Avoid per-OnGUI GUIStyle allocations. Scale drawing AND mouse/touch coordinates
consistently, restore incoming GUI.matrix/color/style state after drawing.
Fit current safe area and resizable window at720p/1080p/1440p/2160p, portrait
and smallwindows; use bounded scrolling so all controls/diagnostics remain
reachable and collapsed panel fits. No whole-screen translucent blocker.
Preserve source/backend/path/capacity/start-stop/preview/overlay functionality.
Regression RED/GREEN should test layout bounds and monotonic/resolution/DPI
scaling, invalid or implausible DPI fallback, finite scale validation and
smallviewport scrolling. Use actual Unity EditMode tests for these policies.
Update package to0.4.0-pc.2 using existing deterministic generator and exact
unchanged eight-DLL/native/model receipt. Run managed compile, package tests,
architecture/public-surface checks. Root will verify actual rendered controls
at1080p/4K and narrow window, then independent review gates publish to same
PC demo branch with immutable Git URL. No need to rerun unchanged inference FPS.
Write full report to plan ledger workspace/task-1-report.md with commands,
files/commits/REDGREEN and limitations; return compact status.
