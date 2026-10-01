# PC evaluation Task 1 — 2026-10-01

User-authorized bounded Windows demo, independent of incomplete Android acceptance.
Existing Windows ORT/DirectML or explicitly selected CPU runs body-only RTMO-t416
for capacity 1–8. This is not the Android YOLO graph or a hardware-only comparison.
No main merge or Release is authorized.

Fresh implementer and independent spec/code-quality review: PASS. Tests added
before behavior; missing policy/timestamp behavior and real Unity scene creation
failures recorded before fixes. Final Unity 2021.3.45f1 EditMode result:
`out/pc-demo/task1/unity-named.xml`, 2026-10-01 08:42:59–08:43:00 UTC,
5 tests, 5 passed, 0 failed. Tests cover capacity/backend selection, repeated held
sequence suppression, opt-in monotonic timestamps, cancelled preparation generations,
and actual creation of two unique scenes preserving an unsaved untitled scene.
The builder imports a named empty scene then opens it additively; Unity rejects
NewScene(Additive) beside an untitled scene and CreateScene outside Play Mode.

`powershell -File tools/package/compile_managed.ps1` final PASS, including Android
conditional Demo compilation; existing default video timestamp contract remains.
PC builder alone opts into acquisition timestamps. Scene drawing overrides existing
renderer widths to 3/5 without changing renderer code. Source reinitialization,
actual inference/visible skeletons and clean package import remain Task 3 acceptance.

Windows native build, from unchanged clean source base 0af0b10:
VsDevCmd v143 14.44 + Ninja Multi-Config Release, HV_USE_DIRECTML=ON,
HV_ENABLE_RTSP=ON, BUILD_TESTING=OFF, target humanvision: PASS (46 targets).
Fresh scoped DLL load plus HV_Create/HV_SubmitFrame/HV_RuntimeCreate/HV_RuntimeCopy/
HV_RuntimeGetDiagnostics/HV_RtspOpen/HV_RtspCopyFrame/HV_RtspClose exports: PASS.
SHA256 humanvision.dll: 8c5b9bb56f6bae8f0dc99d9d6d5cf172ab1b2edc62e8f54cc12b1383c2509180.
This load check does not certify DirectML model execution or FPS.

Next: dedicated PC-only UPM source/dependency closure, strict runtime index and
separate data GUIDs; then clean local import, real video/Windows build and pinned
remote Git import. Existing Android package, prior dirty work and caches retained.
