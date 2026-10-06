# SDK user documentation verification — 2026-10-07

Scope: four SDK guides for a **new Unity project**, two standalone first-use
scripts and documentation verification tooling. SDK/Input remain
`0.4.0-preview.4` / `0.1.0-preview.2`. Runtime/package implementation and release
assets are unchanged. [Guide entry](../user-guide/README.md).

## Fresh checks

| Check | Result | Scope |
| --- | --- | --- |
| Unity `2021.3.45f1` clean project | PASS | Two copied examples compile using local SDK/Input/UGUI packages; no application framework dependency |
| SDK scene generation | PASS | Camera, Video and RTSP demos generated and included in build scenes |
| Manual first-use scene | PASS | Canvas/RawImage/FitInParent/Overlay built, saved and reopened; serialized references and required components checked |
| Runtime data | PASS | Install Packaged Models and asynchronous Prepare complete |
| Windows CPU lifecycle | PASS | TryInitialize with prepared root, profile windows-pc-cpu and capacity1; active profile verified; Shutdown verified |
| Local Markdown links/anchors | 126 PASS | User guides, new report and navigation entries checked |
| Selected public member coverage | 179 names checked | Names checked against SDK source; this is a symbol-presence check, not a claim that every parameter was automatically reviewed |
| Component catalog / public surface / architecture boundaries | PASS | Maintenance documentation checks |
| Diff whitespace / scope | PASS | No changes to upm, native or runtime implementation |

Two isolated projects passed; the final run used the repository's published
probe setup script and the final copies of both examples. Results and source
hashes are in [new-project verification evidence](../user-guide/evidence/new-project-verification.json).

## Reproduce the non-hardware probe

Run from the repository root. The setup refuses to overwrite an existing project;
choose a fresh output path. Requires installed Windows Unity `2021.3.45f1`, its
license, Python and access to bundled UGUI packages. The actual SDK packages are
local file dependencies for this check; Git/offline release installation evidence
remains in the [preview.4 release report](2026-10-05-preview4-release-verification.md).

```powershell
py -3.13 tools/docs/create_user_guide_probe.py --project out/sdk-user-guide-new-project
$unityEditor = 'D:/Developer/2021.3.45f1/Editor/Unity.exe' # Set to your installation.
& $unityEditor -batchmode -nographics -projectPath "$PWD/out/sdk-user-guide-new-project" -executeMethod SdkDocsProbe.Run -logFile "$PWD/out/sdk-user-guide-new-project.log"
# Wait for Unity to exit and inspect docs-probe-result.json; shell launch alone is not proof.
py -3.13 tools/docs/verify_sdk_user_docs.py --probe-result out/sdk-user-guide-new-project/docs-probe-result.json
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/maintenance/check_architecture_boundaries.py
git diff --check
```

Sources: [fresh-project setup](../../tools/docs/create_user_guide_probe.py),
[Editor probe](../../tools/docs/SdkDocsProbe.cs),
[documentation checker](../../tools/docs/verify_sdk_user_docs.py).

The probe builds and validates scene references in batch mode, invokes Prepare
and initializes the CPU runtime directly. It does **not** enter camera Play Mode,
render a visible skeleton, build an EXE/APK or measure device inference. There
are no new physical-device, per-person30FPS or genuine-hand acceptance claims.
Historical SDK measurements retain their original limitations in the
[platform guide](../user-guide/PLATFORM_TEST_RESULTS.md).
