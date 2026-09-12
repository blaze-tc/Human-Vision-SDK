# Release gates

Use PowerShell7 (`pwsh`) on this machine. Native scripts set up MSVC v143 and UTF-8
Ninja include dependencies. Android uses NDK23 (API24, ARM64); NDK21 lacks
the filesystem implementation required by ModelPack/Profile paths. On this
machine use `.venv-reference/Scripts/python.exe` for Python commands below.
Preserve dependency caches and user archives.

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
pwsh -File tools/test/run_upm040_import.ps1
```

Version locations: CMakeLists.txt numeric version, package_live_sdk.py VERSION,
package_upm.py imported VERSION, component metadata, installation/release docs.
Default artifacts in out/releases/0.4.0-preview.1:
HumanVisionSDK-0.4.0-preview.1.unitypackage, HumanVisionSDK-0.4.0-preview.1.zip,
com.blazetc.humanvision-0.4.0-preview.1.tgz, asset-sha256.json, README.md.
Unity tar GUID directories and gzip inner name archtemp.tar are compatibility
requirements. UPM data GUIDs must differ from StreamingAssets copies.

Record exact results in DEVELOPMENT_STATUS. Stage tracked changes plus explicitly
declared new SDK files (never user media/archives). Commit verified artifacts,
push main after checking remote ancestry, create annotated tag v0.4.0-preview.1,
create a draft GitHub Release in blaze-tc/Human-Vision-SDK, upload artifacts, download
to a separate verification directory and compare SHA-256. Verify remote main/tag
commits before publishing the draft. Do not publish the superseded preview.5 draft.

QNN optional code is not an enabled/link/device pass without authorized dependencies.
Windows/Android physical camera/RTSP, 1/2/4/6/8-person fresh complete FPS, accuracy,
latency and thermal acceptance belong to the user and must be listed as pending.
