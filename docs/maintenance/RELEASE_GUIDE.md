# Unity managed API/settings preview.5 update

This additive update uses `tools/package/release-preview5-authority.json` and
`--authority` for snapshot validation/build. SDK 0.4.0-preview.6 depends on Input
0.1.0-preview.4; existing preview.4 native/model/SDK managed bytes remain pinned.
See `docs/reports/2026-10-08-unity-sdk-api-settings.md` for exact passing evidence
and the broad-regression limitations. Build a fresh immutable directory, verify
real Input-first/SDK offline imports and remote Git packages, push main without
force after ancestry checks, annotate tag v0.4.0-preview.6, upload/download and
hash-check all eight draft assets, then publish as a prerelease.

# Release gates

## 0.4.0-preview.4 admitted snapshot

The preview.4 release consumes the byte-qualified two-package snapshot in `upm/`.
Do not reconstruct it from the unrelated development worktree or an older live
build directory. `tools/package/release-preview4-authority.json` binds the complete
SDK/Input file/meta closure, reviewed RTSP reopen fix and accepted half-size defaults.
The native/model bytes remain those of the qualified Q4 payload.

```powershell
py -3.13 tools/package/package_release_snapshot.py --verify-only
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --output out/releases/v0.4.0-preview.4-final
```

The output directory must be new. The eight public files are two `.tgz` archives,
`HumanVisionInput-0.1.0-preview.2.unitypackage`,
`HumanVisionSDK-0.4.0-preview.4.unitypackage`, README, asset index, source snapshot
and SHA256SUMS. Import Input first, then SDK. The two unitypackage GUID partitions
must be disjoint and their union must contain every reviewed asset byte exactly.
The historical combined unitypackage/ZIP remain local evidence, not public assets.
Both installation forms contain the same reviewed source/native/models.
Offline directory groups must match
Unity ExportPackage: folder `asset.meta` plus `pathname`, without a file `asset`.
Test real Unity imports; a serializer checking its own archives is insufficient.
`tools/test/ReleaseSnapshotImportCheck.cs` verifies installed runtime hashes, generates
the three demos and initializes the actual Windows CPU native/model route in a
separate project. Validate local/Git UPM and actual sequential imports of the two
downloaded unitypackages. Input-only compilation must succeed before adding SDK.

The existing package_live_sdk/package_upm/verify_package_isolation scripts below
describe the older preview.3 reconstruction. They are not authoritative for this
two-package preview.4 snapshot. Package versions, provenance and SHA256SUMS must
match the final commit; rebuild artifacts after that commit.

Merge/push only after checking origin/main ancestry. Create an annotated
`v0.4.0-preview.4` tag on the verified commit, a draft Release, upload all artifacts,
download into a separate directory and compare every SHA256 before publishing.
Record the actual user acceptance separately from the unmet 30 fresh complete
observation FPS goal and retained model evaluation/distribution markers.

## Historical preview.3 tooling

Use PowerShell7 (`pwsh`) on this machine. Native scripts set up MSVC v143 and UTF-8
Ninja include dependencies. Android uses NDK23 (API24, ARM64); NDK21 lacks
the filesystem implementation required by ModelPack/Profile paths. On this
machine use `.venv-reference/Scripts/python.exe` for Python commands below.
Preserve dependency caches and user archives.

This workstation marks Unity 2021.3 `Unity.exe` as `RUNASADMIN` in the current-user
compatibility registry. For hidden batch validation set
`$env:__COMPAT_LAYER='RunAsInvoker'` in that command's process before invoking the
Unity test/import scripts. This avoids a hidden UAC prompt without changing the
registry or the user's open Editor process.

```powershell
pwsh -File tools/test/run_native_tests.ps1
pwsh -File tools/package/build_live_native.ps1
pwsh -File tools/package/compile_managed.ps1
pwsh -File tools/test/run_unity040_tests.ps1
python tools/maintenance/generate_component_catalog.py
python tools/package/package_live_sdk.py
python tools/package/package_upm.py
python tools/maintenance/generate_component_catalog.py --check
python tools/maintenance/check_architecture_boundaries.py
python tools/package/verify_package_isolation.py
python tools/test/verify_upm_git_newlines.py
pwsh -File tools/test/run_upm040_import.ps1
```

Version locations: CMakeLists.txt numeric version, package_live_sdk.py VERSION,
package_upm.py imported VERSION, component metadata, installation/release docs.
Default artifacts in out/releases/0.4.0-preview.3:
HumanVisionSDK-0.4.0-preview.3.unitypackage, HumanVisionSDK-0.4.0-preview.3.zip,
com.blazetc.humanvision-0.4.0-preview.3.tgz, asset-sha256.json, README.md.
Unity tar GUID directories and gzip inner name archtemp.tar are compatibility
requirements. UPM data GUIDs must differ from StreamingAssets copies.

Record exact results in DEVELOPMENT_STATUS. Stage tracked changes plus explicitly
declared new SDK files (never user media/archives). Commit verified artifacts,
push main after checking remote ancestry, create annotated tag v0.4.0-preview.3,
create a draft GitHub Release in blaze-tc/Human-Vision-SDK, upload artifacts, download
to a separate verification directory and compare SHA-256. Verify remote main/tag
commits before publishing the draft. Do not publish the superseded preview.5 draft.

QNN optional code is not an enabled/link/device pass without authorized dependencies.
Windows/Android physical camera/RTSP, 1/2/4/6/8-person fresh complete FPS, accuracy,
latency and thermal acceptance belong to the user and must be listed as pending.
