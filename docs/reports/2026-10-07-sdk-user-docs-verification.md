# SDK code-first user guide verification — 2026-10-07

Scope: first-use code fragments, [one-object starter](../user-guide/examples/SdkBasicUsage.cs)
and [optional Hierarchy menu](../user-guide/examples/Editor/SdkBasicUsageMenu.cs).
The [beginner guide](../user-guide/FIRST_INSTALL.md) explains Start, asynchronous
resource preparation, initialization, camera binding, body/joint reads and teardown.
SDK/Input remain `0.4.0-preview.4` / `0.1.0-preview.2`; package implementation and
release assets are unchanged.

## Latest checks

| Check | Result | Scope |
| --- | --- | --- |
| Unity2021.3.45f1 isolated project compilation | PASS | Minimal starter, two optional display examples and Editor menu compile |
| Hierarchy creation action | PASS | Editor menu method creates a starter with required SDK components; no Canvas |
| Saved starter | PASS | Starter persists after scene save/reopen; no UI references to bind |
| Runtime preparation | PASS | Packaged resource staging and asynchronous Prepare complete |
| CPU initialization/data access | PASS | windows-pc-cpu capacity1 initializes; profile, zero bodies before input and allocated body array checked |
| Shutdown | PASS | Initialized Manager shuts down |
| Markdown links/anchors | PASS | All user-guide/navigation/report links checked |
| API member coverage | 179 names checked | Selected SDK public names appear in API reference; parameter descriptions are source-reviewed |
| Architecture / component catalog / whitespace | PASS | Documentation guards and diff checks |

The final run used the final starter source with input error logging. Source
hashes and results are in [verification evidence](../user-guide/evidence/new-project-verification.json).

## Reproduce

From the repository root, choose a fresh project path (setup never resets an
existing project). Requires licensed Windows Unity2021.3.45f1 and Python.

```powershell
py -3.13 tools/docs/create_user_guide_probe.py --project out/sdk-code-first-new-project
$unityEditor = 'D:/Developer/2021.3.45f1/Editor/Unity.exe' # Set to your installation.
& $unityEditor -batchmode -nographics -projectPath "$PWD/out/sdk-code-first-new-project" -executeMethod SdkDocsProbe.Run -logFile "$PWD/out/sdk-code-first-new-project.log"
# Wait for the result file and Unity exit; successful launch alone is not proof.
py -3.13 tools/docs/verify_sdk_user_docs.py --probe-result out/sdk-code-first-new-project/docs-probe-result.json
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/maintenance/check_architecture_boundaries.py
git diff --check
```

Sources: [setup](../../tools/docs/create_user_guide_probe.py),
[Editor probe](../../tools/docs/SdkDocsProbe.cs),
[document checker](../../tools/docs/verify_sdk_user_docs.py).
The probe uses local SDK/Input packages; released Git/offline package integrity
remains covered by the [release report](2026-10-05-preview4-release-verification.md).

The probe invokes the menu creation method in the Editor, prepares resources and
initializes the runtime directly. It does not click a physical context menu,
enter camera Play Mode, produce a detected-body fixture, render skeletons, build
EXE/APK or measure hardware performance. Empty body access before input verifies
data availability, not inference. Device/30FPS/real-hand gates remain unchanged.
