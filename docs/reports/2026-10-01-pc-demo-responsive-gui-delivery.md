# PC Demo responsive GUI delivery - 2026-10-01

PC evaluation package0.4.0-pc.2 scales the Demo GUI with resolution and credible
DPI, offers serialized uiScale, constrains safe-area bounds and scrolls controls
vertically. Individual source/backend controls reserve44referenceunits each;
long paths no longer enlarge scroll content or clip Stop. Panel collapses to
one settings button. Recognition, public API, native DLLs and models unchanged.

## Verification

Implementation045005a9df484923b3f3663b9e7e16be0ef1f684; preceding GUI corrections
d10f753 and4ab071d preserved. Real Unity EditMode23/23, package11/11, managed
compilation and architecture/public-surface checks PASS. Independent scoped
spec/quality review PASS; actual GUI validation below is separate.

Root rebuilt Win64 Player using isolated scratch project:
`pwsh -NoProfile -File out/pc-demo/start-unity.ps1 -Project <worktree>/out/pc-demo/gui-responsive/UnityProject -Log <worktree>/out/pc-demo/gui-responsive/gui-build-round2.log -Method PcGuiCaptureBuild.Run`.
Build result: Succeeded,0errors,0warnings. Original user's dirty PC scene retained.

- Actual Player1920x1080,DPI120: final-round2-1920x1080/gui.png and gui-evidence.json.
- Actual fixed Editor GameView3840x2160: screen-editor-final-3840x2160/gui.png
  and gui-evidence.json, via same scratch project's PcGuiEditorCapture.Run.
  A prior standalone3840 request was monitor-clamped1920, so it is not used
  as4K evidence. Camera.Render omits IMGUI; all final images include actual OnGUI.
- Actual narrow Player400x600,DPI120, Sky logical window322x511:
  final-round2-400x600/top-rtsp-cpu.jpg, bottom-stop.jpg, collapsed.jpg.
  Individually readable source/backend rows, complete Stop, wrapped labels,
  no horizontal overflow. Root pointer selection WebCamera->RTSP->CPU observed
  in scratch component states.jsonl; overlaytoggle true->false and collapse/
  expand true->false->true observed. Bottom controls reached by vertical scroll.

Captures live under ignored out/pc-demo/gui-responsive. Earlier failed compile,
overflow and compressed-row evidence retained; fixed by separate commits. Tests
prove layout policies; actual captures/pointer states establish rendered behavior.
This validation intentionally stops recognition to isolate controls and does not
remeasure unchanged inference FPS or certify camera/RTSP hardware performance.

Final package asset-sha256.json SHA256:
5d171e338408855f98916eea957b3f4bac006826c254e0cbed4d03273016474e;
153indexed assets. Dedicated PC branch delivery only; no main merge or Release.
Next delivery gate: fresh remote Git SHA import and all packaged hash validation.
