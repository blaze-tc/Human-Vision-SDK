# Human-Vision-SDK 0.4 Codex Master Instruction v2

Use `SDK_040_PLUGIN_ARCHITECTURE_ADDENDUM.md` as the architecture authority. If it conflicts with any `LEGACY_*.md` plan, the addendum wins.

## Required order

1. Freeze the existing Unity/C ABI public surface with compatibility tests.
2. Introduce Runtime Host + versioned C Plugin ABI.
3. Introduce capability registry, ModelPack manager and Profile manager.
4. Wrap the current legacy recognizer behind a Pipeline Plugin to prove Host independence.
5. Implement/refactor Backend Plugins (ORT/DirectML/NNAPI/QNN optional).
6. Implement RTMO, RTMPose TopDown and Hand Pipeline Plugins.
7. Implement common tracking, region, canonical skeleton and temporal services.
8. Keep Unity public integration semantic/model-independent.
9. Implement batched Kinect-style skeleton renderer and diagnostics.
10. Implement the complete maintenance documentation contract.
11. Run automated architecture/documentation/package verification.
12. Commit/push and publish `v0.4.0-preview.1`.

## Codex responsibility

Codex owns code, tests, compilation, packaging, documentation, commits and GitHub release.

## User responsibility

The user owns physical camera/RTSP/Android/1-2-4-6-8-person/FPS/latency/accuracy/thermal testing.

Never convert build success into a claim that hardware performance passed.

## Release blocker

Do not publish the release until all required `docs/maintenance/` files, component README files, generated component catalog and architecture guards exist and pass.

Required verification includes:

```powershell
cmake -S . -B build/windows-test -DBUILD_TESTING=ON
cmake --build build/windows-test --config Release
ctest --test-dir build/windows-test -C Release --output-on-failure

pwsh -File tools/package/build_live_native.ps1
pwsh -File tools/package/compile_managed.ps1

python tools/maintenance/generate_component_catalog.py --check
python tools/maintenance/check_architecture_boundaries.py
python tools/package/verify_package_isolation.py
python tools/package/package_live_sdk.py
python tools/package/package_upm.py
```
